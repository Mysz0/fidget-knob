import XCTest
@testable import FidgetKnob

final class DeskModeTests: XCTestCase {
    func testTimerStateParses() throws {
        let s = try XCTUnwrap(TimerState(data: Data([2, 1, 0x2C, 0x01, 0x58, 0x02])))
        XCTAssertEqual(s.state, .running)
        XCTAssertEqual(s.left, 300)
        XCTAssertEqual(s.length, 600)
        XCTAssertEqual(s.fractionLeft, 0.5, accuracy: 0.001)
        XCTAssertNil(TimerState(data: Data([2, 1, 0])))
        XCTAssertNil(TimerState(data: Data([2, 9, 0, 0, 0, 0])))
    }

    func testTimerFormat() {
        XCTAssertEqual(TimerFormat.clock(245), "4:05")
        XCTAssertEqual(TimerFormat.clock(3720), "1:02:00")
        XCTAssertEqual(TimerFormat.long(5400), "1 h 30 min")
        XCTAssertEqual(TimerFormat.long(45), "45 s")
    }

    func testDiceState() throws {
        let s = try XCTUnwrap(DiceState(data: Data([4, 1, 0x11, 0, 0])))
        XCTAssertEqual(s.kind, .d20)
        XCTAssertEqual(s.result, 17)
        XCTAssertFalse(s.rolling)
        XCTAssertEqual(DiceKind.coin.text(1), "Heads")
        XCTAssertEqual(DiceKind.yesNo.text(2), "No")
    }

    func testCommands() {
        XCTAssertEqual(KnobCommands.timerLength(seconds: 300), Data([3, 1, 0x2C, 0x01]))
        XCTAssertEqual(KnobCommands.timerLength(seconds: 1), Data([3, 1, 15, 0]))
        XCTAssertEqual(KnobCommands.diceKind(.coin), Data([4, 1, 2]))
        XCTAssertEqual(KnobCommands.diceRoll, Data([4, 2]))
        XCTAssertEqual(KnobLink.Mode(rawValue: 3), .clock)
    }
}

final class PhotoModeTests: XCTestCase {
    func testPhotoStateParses() throws {
        let s = try XCTUnwrap(PhotoState(data: Data([6, 0b10101, 2, 1])))
        XCTAssertTrue(s.has(0))
        XCTAssertFalse(s.has(1))
        XCTAssertTrue(s.has(4))
        XCTAssertEqual(s.current, 2)
        XCTAssertTrue(s.mosaic)
        XCTAssertNil(PhotoState(data: Data([4, 0, 0, 0])))
    }

    func testPhotoCommands() {
        XCTAssertEqual(KnobCommands.photoDelete(3), Data([5, 1, 3]))
        XCTAssertEqual(KnobCommands.photoShow(0), Data([5, 2, 0]))
        XCTAssertEqual(KnobLink.Mode(rawValue: 6), .photo)
    }

    func testSquareCropAndJpegFit() throws {
        let wide = UIGraphicsImageRenderer(size: CGSize(width: 1200, height: 600)).image { ctx in
            UIColor.systemTeal.setFill()
            ctx.fill(CGRect(x: 0, y: 0, width: 1200, height: 600))
        }
        let square = CoverRender.square(wide)
        XCTAssertEqual(square.size.width, square.size.height)
        let jpeg = try XCTUnwrap(CoverRender.jpeg(from: square, limit: KnobProtocol.maxArtworkBytes - 1))
        XCTAssertLessThan(jpeg.count + 1, KnobProtocol.maxArtworkBytes + 1)
    }
}
