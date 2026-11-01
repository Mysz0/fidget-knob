import CoreBluetooth
import Foundation

/// What the knob reports as playing (raw strings from the iPhone's media
/// service, forwarded by the knob).
struct NowPlaying: Equatable {
    var artist: String
    var album: String
    var title: String
    var playing: Bool = true

    /// Identifies the track, ignoring play/pause - a pause is not a new cover.
    var trackKey: String { "\(artist)\u{1F}\(album)\u{1F}\(title)" }
}

/// The knob's own Bluetooth protocol. Byte formats must match
/// `firmware/lib/knob_music/knob_protocol.h` exactly.
enum KnobProtocol {
    static let service = CBUUID(string: "6B6E6F62-0001-4000-8000-000000000001")
    static let mode = CBUUID(string: "6B6E6F62-0002-4000-8000-000000000001")
    static let coverStyle = CBUUID(string: "6B6E6F62-0003-4000-8000-000000000001")
    static let nowPlaying = CBUUID(string: "6B6E6F62-0004-4000-8000-000000000001")
    static let artwork = CBUUID(string: "6B6E6F62-0005-4000-8000-000000000001")
    static let settings = CBUUID(string: "6B6E6F62-0006-4000-8000-000000000001")
    static let command = CBUUID(string: "6B6E6F62-0007-4000-8000-000000000001")
    static let status = CBUUID(string: "6B6E6F62-0008-4000-8000-000000000001")
    static let scan = CBUUID(string: "6B6E6F62-0009-4000-8000-000000000001")
    static let lampState = CBUUID(string: "6B6E6F62-000A-4000-8000-000000000001")
    /// Live state of the current desk mode (timer, dice, photos).
    static let modeState = CBUUID(string: "6B6E6F62-000B-4000-8000-000000000001")

    static let maxArtworkBytes = 24 * 1024

    enum ArtworkKind: UInt8 {
        case mosaic = 1
        case jpeg = 2
        case title = 3
        case homeConfig = 4
        case photoJpeg = 5    // [slot][JPEG]
        case photoMosaic = 6  // [slot][23x23 RGB565]
    }

    static let photoSlots = 5
    /// Photos are kept a little smaller than covers: they go to the knob's
    /// flash, and its memory is tightest in Photos mode.
    static let maxPhotoBytes = 20 * 1024

    /// CRC-32 (IEEE, as zlib) - the knob rejects any image that does not match.
    static func crc32(_ data: Data) -> UInt32 {
        var crc: UInt32 = 0xFFFF_FFFF
        for byte in data {
            crc ^= UInt32(byte)
            for _ in 0..<8 {
                crc = (crc >> 1) ^ (0xEDB8_8320 & (0 &- (crc & 1)))
            }
        }
        return ~crc
    }

    /// Splits an image into begin / data / end frames no longer than `maxFrame`.
    /// Returns nothing for payloads the knob would refuse.
    static func artworkFrames(kind: ArtworkKind, payload: Data, maxFrame: Int) -> [Data] {
        guard !payload.isEmpty, payload.count <= maxArtworkBytes, maxFrame > 3 else { return [] }
        let count = UInt16(payload.count)
        var frames = [Data([0x01, kind.rawValue, UInt8(count & 0xFF), UInt8(count >> 8)])]

        let chunk = maxFrame - 3
        var offset = 0
        while offset < payload.count {
            let end = min(offset + chunk, payload.count)
            var frame = Data([0x02, UInt8(offset & 0xFF), UInt8(offset >> 8)])
            frame.append(payload.subdata(in: payload.startIndex + offset ..< payload.startIndex + end))
            frames.append(frame)
            offset = end
        }

        let crc = crc32(payload)
        frames.append(Data([0x03, UInt8(crc & 0xFF), UInt8((crc >> 8) & 0xFF),
                            UInt8((crc >> 16) & 0xFF), UInt8(crc >> 24)]))
        return frames
    }

    /// "artist<US>album<US>title". Nil when it carries nothing to look up.
    static func parseNowPlaying(_ data: Data) -> NowPlaying? {
        guard let text = String(data: data, encoding: .utf8) else { return nil }
        let parts = text.split(separator: "\u{1F}", omittingEmptySubsequences: false).map(String.init)
        guard parts.count == 3 || parts.count == 4,
              !(parts[0].isEmpty && parts[1].isEmpty && parts[2].isEmpty) else { return nil }
        // Firmware before the playing flag sent three fields.
        let playing = parts.count == 4 ? parts[3] == "1" : true
        return NowPlaying(artist: parts[0], album: parts[1], title: parts[2], playing: playing)
    }

    /// One RGB565 pixel, little-endian, as the knob's mosaic grid stores it.
    static func rgb565LE(red: UInt8, green: UInt8, blue: UInt8) -> [UInt8] {
        let v = (UInt16(red & 0xF8) << 8) | (UInt16(green & 0xFC) << 3) | UInt16(blue >> 3)
        return [UInt8(v & 0xFF), UInt8(v >> 8)]
    }
}

/// What the knob reports about itself (knob_protocol.h, KnobStatus).
struct KnobStatus: Equatable {
    enum Wifi: UInt8 { case off = 0, connecting = 1, connected = 2, failed = 3 }
    var mode: UInt8
    var wifi: Wifi
    var rssi: Int
    var hubConnected: Bool
    var ssid: String

    init?(data: Data) {
        let b = [UInt8](data)
        guard b.count >= 6, b[0] == 1, b.count >= 6 + Int(b[5]) else { return nil }
        mode = b[1]
        wifi = Wifi(rawValue: b[2]) ?? .off
        rssi = Int(Int8(bitPattern: b[3]))
        hubConnected = b[4] != 0
        ssid = String(decoding: b[6..<(6 + Int(b[5]))], as: UTF8.self)
    }

    /// 0-4 bars, as iOS draws WiFi strength.
    var bars: Int {
        switch rssi {
        case ..<(-85): return 1
        case ..<(-72): return 2
        case ..<(-60): return 3
        default: return 4
        }
    }
}

/// A WiFi network the knob can see.
struct WifiNetwork: Identifiable, Equatable {
    var id: String { ssid }
    let ssid: String
    let rssi: Int
    let secured: Bool

    static func parse(_ data: Data) -> [WifiNetwork] {
        String(decoding: data, as: UTF8.self)
            .split(separator: "\u{1E}")
            .compactMap { record in
                let f = record.split(separator: "\u{1F}", omittingEmptySubsequences: false).map(String.init)
                guard f.count == 3, let rssi = Int(f[1]) else { return nil }
                return WifiNetwork(ssid: f[0], rssi: rssi, secured: f[2] == "1")
            }
    }
}

/// Small writes to the knob (knob_protocol.h, Command and LampState).
/// The knob's countdown timer as it reports it.
struct TimerState: Equatable {
    enum State: UInt8 { case idle = 0, running, paused, done }
    var state: State
    var left: Int    // Seconds.
    var length: Int  // Seconds.

    init?(data: Data) {
        let b = [UInt8](data)
        guard b.count >= 6, b[0] == 2, let state = State(rawValue: b[1]) else { return nil }
        self.state = state
        left = Int(b[2]) | Int(b[3]) << 8
        length = Int(b[4]) | Int(b[5]) << 8
    }

    /// Fraction of the time still to go.
    var fractionLeft: Double { length > 0 ? Double(left) / Double(length) : 1 }
}

enum DiceKind: UInt8, CaseIterable, Identifiable {
    case d6 = 0, d20, coin, yesNo, d100
    var id: UInt8 { rawValue }
    var title: String {
        switch self {
        case .d6: "d6"
        case .d20: "d20"
        case .coin: "Coin"
        case .yesNo: "Yes / no"
        case .d100: "d100"
        }
    }
    func text(_ result: Int) -> String {
        switch self {
        case .coin: result == 1 ? "Heads" : "Tails"
        case .yesNo: result == 1 ? "Yes" : "No"
        default: "\(result)"
        }
    }
}

struct DiceState: Equatable {
    var kind: DiceKind
    var result: Int  // 0 = not rolled yet.
    var rolling: Bool

    init?(data: Data) {
        let b = [UInt8](data)
        guard b.count >= 5, b[0] == 4 else { return nil }
        kind = DiceKind(rawValue: b[1]) ?? .d6
        result = Int(b[2]) | Int(b[3]) << 8
        rolling = b[4] != 0
    }
}

/// The photos on the knob, as it reports them.
struct PhotoState: Equatable {
    var present: UInt8  // Bit per slot.
    var current: Int
    var mosaic: Bool
    /// Bumped by the knob each time it stores a photo.
    var saved: UInt8

    init?(data: Data) {
        let b = [UInt8](data)
        guard b.count >= 4, b[0] == 6 else { return nil }
        present = b[1]
        current = Int(b[2])
        mosaic = b[3] != 0
        saved = b.count >= 5 ? b[4] : 0
    }

    func has(_ slot: Int) -> Bool { present & (1 << slot) != 0 }
}

enum KnobCommands {
    static let scanWifi = Data([1])
    static func photoDelete(_ slot: Int) -> Data { Data([5, 1, UInt8(slot)]) }
    static func photoShow(_ slot: Int) -> Data { Data([5, 2, UInt8(slot)]) }
    static let photoToggleView = Data([5, 3])
    static let powerOff = Data([6])

    static func timerLength(seconds: Int) -> Data {
        let v = UInt16(max(15, min(3 * 3600, seconds)))
        return Data([3, 1, UInt8(v & 0xFF), UInt8(v >> 8)])
    }
    static let timerToggle = Data([3, 2])
    static let timerReset = Data([3, 3])
    static func diceKind(_ kind: DiceKind) -> Data { Data([4, 1, kind.rawValue]) }
    static let diceRoll = Data([4, 2])

    static func setTime(unix: UInt32, utcOffsetMinutes: Int16) -> Data {
        let o = UInt16(bitPattern: utcOffsetMinutes)
        return Data([2, UInt8(unix & 0xFF), UInt8((unix >> 8) & 0xFF), UInt8((unix >> 16) & 0xFF),
                     UInt8(unix >> 24), UInt8(o & 0xFF), UInt8(o >> 8)])
    }

    static func lampState(_ lamp: HubLight) -> Data {
        let hue = UInt16(max(0, min(359, lamp.hue)))
        var d = Data([lamp.isOn ? 1 : 0, UInt8(max(1, min(100, lamp.level))),
                      UInt8(hue & 0xFF), UInt8(hue >> 8), UInt8(max(0, min(100, lamp.saturation * 100)))])
        d.append(Data(lamp.id.utf8))
        return d
    }
}
