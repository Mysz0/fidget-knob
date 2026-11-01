import SwiftUI
import XCTest
@testable import FidgetKnob

final class KnobProtocolTests: XCTestCase {
    func testCRC32MatchesFirmware() {
        // Same check value the firmware's native test pins.
        XCTAssertEqual(KnobProtocol.crc32(Data("123456789".utf8)), 0xCBF4_3926)
    }

    func testFramesReassembleToPayload() {
        let payload = Data((0..<1000).map { UInt8($0 % 251) })
        let frames = KnobProtocol.artworkFrames(kind: .jpeg, payload: payload, maxFrame: 182)

        XCTAssertEqual(frames.first?.prefix(4), Data([0x01, 0x02, 0xE8, 0x03]))
        var rebuilt = Data()
        var expectedOffset = 0
        for frame in frames.dropFirst().dropLast() {
            XCTAssertEqual(frame[frame.startIndex], 0x02)
            XCTAssertLessThanOrEqual(frame.count, 182)
            let offset = Int(frame[frame.startIndex + 1]) | Int(frame[frame.startIndex + 2]) << 8
            XCTAssertEqual(offset, expectedOffset)
            let chunk = frame.dropFirst(3)
            rebuilt.append(chunk)
            expectedOffset += chunk.count
        }
        XCTAssertEqual(rebuilt, payload)

        let end = frames.last!
        XCTAssertEqual(end.count, 5)
        XCTAssertEqual(end[end.startIndex], 0x03)
        let crc = end.dropFirst().enumerated().reduce(UInt32(0)) { $0 | UInt32($1.element) << (8 * UInt32($1.offset)) }
        XCTAssertEqual(crc, KnobProtocol.crc32(payload))
    }

    func testOversizedPayloadIsRefused() {
        let big = Data(count: KnobProtocol.maxArtworkBytes + 1)
        XCTAssertTrue(KnobProtocol.artworkFrames(kind: .jpeg, payload: big, maxFrame: 182).isEmpty)
    }

    func testNowPlayingParses() {
        let raw = Data("Polyphia\u{1F}Remember That You Will Die\u{1F}Playing God".utf8)
        let np = KnobProtocol.parseNowPlaying(raw)
        XCTAssertEqual(np, NowPlaying(artist: "Polyphia", album: "Remember That You Will Die", title: "Playing God", playing: true))
    }

    func testEmptyNowPlayingIsNil() {
        XCTAssertNil(KnobProtocol.parseNowPlaying(Data("\u{1F}\u{1F}".utf8)))
        XCTAssertNil(KnobProtocol.parseNowPlaying(Data("garbage".utf8)))
    }

    func testRGB565LittleEndian() {
        // Pure red -> 0xF800 -> bytes 00 F8.
        XCTAssertEqual(KnobProtocol.rgb565LE(red: 255, green: 0, blue: 0), [0x00, 0xF8])
        XCTAssertEqual(KnobProtocol.rgb565LE(red: 0, green: 255, blue: 0), [0xE0, 0x07])
        XCTAssertEqual(KnobProtocol.rgb565LE(red: 0, green: 0, blue: 255), [0x1F, 0x00])
    }
}

final class KnobSettingsTests: XCTestCase {
    func testDefaultsEncodeLikeFirmware() {
        // Firmware's encodeSettings(KnobSettings{}) - version 1 then defaults.
        XCTAssertEqual(KnobSettings().encoded, Data([1, 12, 2, 1, 2, 1, 2, 1, 3, 1, 0, 0, 0, 0, 1, 1, 1, 0, 255, 214, 160, 46, 168, 84, 6, 16, 100, 0]))
    }

    func testLookRoundTrips() {
        var s = KnobSettings()
        s.spritePack = 4
        s.palette = 2
        s.effect = .drift
        let back = KnobSettings(data: s.encoded)
        XCTAssertEqual(back?.look, KnobLook(pack: 4, palette: 2, effect: .drift))
        // Eleven bytes from older firmware: the original look.
        XCTAssertEqual(KnobSettings(data: s.encoded.prefix(11))?.look, KnobLook())
    }

    func testScreenAndCustomColoursRoundTrip() {
        var s = KnobSettings()
        s.palette = KnobSettings.customPalette
        s.customWhite = [10, 20, 30]
        s.customAccent = [200, 100, 0]
        s.screenOffSeconds = 0
        s.dimAfterSeconds = 30
        s.dimPercent = 40
        s.brightness = 70
        let back = KnobSettings(data: s.encoded)
        XCTAssertEqual(back?.customWhite, [10, 20, 30])
        XCTAssertEqual(back?.customAccent, [200, 100, 0])
        XCTAssertEqual(back?.screenOffSeconds, 0)
        XCTAssertEqual(back?.dimAfterSeconds, 30)
        XCTAssertEqual(back?.brightness, 70)
        XCTAssertEqual(back?.look.colours.name, "Custom")
        XCTAssertEqual(Color(rgb: [10, 20, 30]).rgbBytes, [10, 20, 30])
    }

    func testTwistRoundTrips() {
        var s = KnobSettings()
        s.musicTwist = .tracks
        s.lightsTwist = .lamps
        s.twistSensitivity = .high
        s.twistReverse = true
        let back = KnobSettings(data: s.encoded)
        XCTAssertEqual(back?.musicTwist, .tracks)
        XCTAssertEqual(back?.lightsTwist, .lamps)
        XCTAssertEqual(back?.twistSensitivity, .high)
        XCTAssertEqual(back?.twistReverse, true)
        let old = KnobSettings(data: s.encoded.prefix(14))
        XCTAssertEqual(old?.musicTwist, .volume)
        XCTAssertEqual(old?.twistReverse, false)
    }

    func testParseRoundTrip() {
        var s = KnobSettings()
        s.screenOffSeconds = 30
        s.sleepMinutes = 10
        s.musicTap = .next
        s.musicShake = .previous
        s.lightsShake = .nextLight
        s.overlayOn = false
        s.overlaySeconds = 6
        XCTAssertEqual(KnobSettings(data: s.encoded), s)
    }

    func testParseRejectsOtherVersions() {
        XCTAssertNil(KnobSettings(data: Data([2, 12, 2, 1, 2, 1, 2, 1, 3])))
        XCTAssertNil(KnobSettings(data: Data([1, 12])))
    }

    func testNowPlayingCarriesPlayingFlag() {
        let np = KnobProtocol.parseNowPlaying(Data("A\u{1F}B\u{1F}C\u{1F}1".utf8))
        XCTAssertEqual(np?.title, "C")
        XCTAssertEqual(np?.playing, true)
        XCTAssertEqual(KnobProtocol.parseNowPlaying(Data("A\u{1F}B\u{1F}C\u{1F}0".utf8))?.playing, false)
        // Older firmware sent three fields: treat as playing.
        XCTAssertEqual(KnobProtocol.parseNowPlaying(Data("A\u{1F}B\u{1F}C".utf8))?.playing, true)
    }

    func testTitleOverlayPayload() {
        let payload = TitleRender.mask(title: "Playing God", artist: "Polyphia")
        XCTAssertNotNil(payload)
        let p = payload!
        let w = Int(p[p.startIndex]), h = Int(p[p.startIndex + 1])
        XCTAssertLessThanOrEqual(w, 220)
        XCTAssertLessThanOrEqual(h, 96)
        XCTAssertEqual(p.count, 2 + w * h)
        XCTAssertTrue(p.dropFirst(2).contains { $0 > 200 }, "text should have solid pixels")
    }

    func testTitleOverlayHandlesAnyScript() {
        XCTAssertNotNil(TitleRender.mask(title: "Żółć 東京 🔥", artist: "Ąę"))
    }
}
