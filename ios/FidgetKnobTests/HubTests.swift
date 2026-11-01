import XCTest
@testable import FidgetKnob

final class HubTests: XCTestCase {
    func testPKCEChallengeMatchesRFC7636() {
        // RFC 7636 appendix B.
        XCTAssertEqual(PKCE.challenge(for: "dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk"),
                       "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM")
    }

    func testVerifierIsLongAndURLSafe() {
        let v = PKCE.makeVerifier()
        XCTAssertEqual(v.count, 128)
        XCTAssertTrue(v.allSatisfy { $0.isLetter || $0.isNumber || "-._~".contains($0) })
    }

    func testHomeConfigEncodesLikeFirmware() {
        var c = HomeConfig()
        c.lampId = "xyz"
        // key 6, length 3 little-endian, bytes.
        XCTAssertEqual(c.encoded, Data([6, 3, 0, 0x78, 0x79, 0x7A]))
    }

    func testHomeConfigOrdersFieldsByKey() {
        var c = HomeConfig()
        c.lampId = "L"
        c.ssid = "S"
        XCTAssertEqual(c.encoded, Data([1, 1, 0, 0x53, 6, 1, 0, 0x4C]))
    }

    func testPEMWrapsBase64At64() {
        let der = Data(repeating: 0xAB, count: 100)
        let pem = PEM.certificate(der)
        XCTAssertTrue(pem.hasPrefix("-----BEGIN CERTIFICATE-----\n"))
        XCTAssertTrue(pem.hasSuffix("-----END CERTIFICATE-----\n"))
        let body = pem.split(separator: "\n").dropFirst().dropLast()
        XCTAssertTrue(body.allSatisfy { $0.count <= 64 })
        XCTAssertEqual(Data(base64Encoded: body.joined()), der)
    }

    func testParsesLightsOnly() throws {
        let json = """
        [{"id":"a","type":"light","attributes":{"customName":"Desk lamp","isOn":true,"lightLevel":40,"colorHue":300,"colorSaturation":0.7}},
         {"id":"b","type":"sensor","attributes":{"customName":"Motion"}},
         {"id":"c","type":"light","attributes":{"customName":"Hall","isOn":false,"lightLevel":100}}]
        """.data(using: .utf8)!
        let lights = try HubLight.parse(json)
        XCTAssertEqual(lights.map(\.id), ["a", "c"])
        XCTAssertEqual(lights[0].name, "Desk lamp")
        XCTAssertTrue(lights[0].supportsColour)
        XCTAssertFalse(lights[1].supportsColour)
        XCTAssertEqual(lights[0].level, 40)
    }

    func testSettingsCarryRatchet() {
        var s = KnobSettings()
        XCTAssertEqual(s.encoded.count, 28)
        XCTAssertEqual(s.encoded[9], 1)
        s.ratchet = false
        XCTAssertEqual(KnobSettings(data: s.encoded)?.ratchet, false)
        // Nine bytes from older firmware: ratchet stays on.
        XCTAssertEqual(KnobSettings(data: Data([1, 12, 2, 1, 2, 1, 2, 1, 3]))?.ratchet, true)
    }
}
