import XCTest
@testable import FidgetKnob

final class KnobLinkProtocolTests: XCTestCase {
    func testStatusParses() {
        let raw = Data([1, 0, 2, UInt8(bitPattern: -61), 1, 4] + Array("Home".utf8))
        let s = KnobStatus(data: raw)
        XCTAssertEqual(s?.wifi, .connected)
        XCTAssertEqual(s?.rssi, -61)
        XCTAssertEqual(s?.hubConnected, true)
        XCTAssertEqual(s?.ssid, "Home")
        XCTAssertNil(KnobStatus(data: Data([1, 0])))
    }

    func testScanListParses() {
        let raw = Data("Home\u{1F}-50\u{1F}1\u{1E}Cafe\u{1F}-80\u{1F}0".utf8)
        let nets = WifiNetwork.parse(raw)
        XCTAssertEqual(nets.map(\.ssid), ["Home", "Cafe"])
        XCTAssertEqual(nets[0].rssi, -50)
        XCTAssertTrue(nets[0].secured)
        XCTAssertFalse(nets[1].secured)
        XCTAssertTrue(WifiNetwork.parse(Data()).isEmpty)
    }

    func testLampStateEncodesLikeFirmware() {
        let lamp = HubLight(id: "abc", name: "x", isOn: true, level: 40, hue: 300, saturation: 0.7, supportsColour: true)
        XCTAssertEqual(KnobCommands.lampState(lamp), Data([1, 40, 0x2C, 0x01, 70] + Array("abc".utf8)))
    }

    func testSetTimeCommand() {
        let d = KnobCommands.setTime(unix: 0x68E6_0000, utcOffsetMinutes: 120)
        XCTAssertEqual(d, Data([2, 0x00, 0x00, 0xE6, 0x68, 0x78, 0x00]))
        XCTAssertEqual(KnobCommands.scanWifi, Data([1]))
    }

    func testSettingsCarrySpinCover() {
        var s = KnobSettings()
        s.spinCover = true
        XCTAssertEqual(s.encoded.count, 28)
        XCTAssertEqual(KnobSettings(data: s.encoded)?.spinCover, true)
        XCTAssertEqual(KnobSettings(data: Data([1, 12, 2, 1, 2, 1, 2, 1, 3, 1]))?.spinCover, false)
    }

    func testHomeConfigLampCycle() {
        var c = HomeConfig()
        c.lampCycle = ["a", "b"]
        XCTAssertEqual(c.encoded, Data([7, 3, 0] + Array("a,b".utf8)))
    }
}
