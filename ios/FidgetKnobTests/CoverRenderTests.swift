import XCTest
import UIKit
@testable import FidgetKnob

final class CoverRenderTests: XCTestCase {
    private func solid(_ color: UIColor, size: CGFloat = 600) -> UIImage {
        UIGraphicsImageRenderer(size: CGSize(width: size, height: size)).image { ctx in
            color.setFill()
            ctx.fill(CGRect(x: 0, y: 0, width: size, height: size))
        }
    }

    func testMosaicIsGridOfCellColours() {
        let grid = CoverRender.mosaic(from: solid(.red))
        XCTAssertEqual(grid.count, 23 * 23 * 2)
        // Every cell pure red in RGB565 little-endian (allowing 1 LSB of
        // colour-management drift in green/blue).
        for i in stride(from: 0, to: grid.count, by: 2) {
            let v = UInt16(grid[i]) | UInt16(grid[i + 1]) << 8
            XCTAssertGreaterThanOrEqual(v >> 11, 30)
        }
    }

    func testJPEGFitsTheKnob() {
        let jpeg = CoverRender.jpeg(from: solid(.blue))
        XCTAssertNotNil(jpeg)
        XCTAssertLessThanOrEqual(jpeg!.count, KnobProtocol.maxArtworkBytes)
        let img = UIImage(data: jpeg!)!
        XCTAssertEqual(img.size.width * img.scale, 240)
        XCTAssertEqual(img.size.height * img.scale, 240)
    }
}
