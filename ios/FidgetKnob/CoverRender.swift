import UIKit

/// Turns an album cover into the two forms the knob draws.
enum CoverRender {
    static let cells = 23

    /// 23x23 cells, RGB565 little-endian, row-major from the top-left: the
    /// average colour of each patch of the cover, one per sprite on the knob.
    static func mosaic(from image: UIImage) -> Data {
        let n = cells
        var pixels = [UInt8](repeating: 0, count: n * n * 4)
        let space = CGColorSpaceCreateDeviceRGB()
        pixels.withUnsafeMutableBytes { buf in
            guard let ctx = CGContext(data: buf.baseAddress, width: n, height: n,
                                      bitsPerComponent: 8, bytesPerRow: n * 4, space: space,
                                      bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue),
                  let cg = image.cgImage else { return }
            // Downscaling with high interpolation averages each patch.
            ctx.interpolationQuality = .high
            ctx.draw(cg, in: CGRect(x: 0, y: 0, width: n, height: n))
        }
        var out = Data(capacity: n * n * 2)
        for i in 0..<(n * n) {
            out.append(contentsOf: KnobProtocol.rgb565LE(red: pixels[i * 4], green: pixels[i * 4 + 1],
                                                         blue: pixels[i * 4 + 2]))
        }
        return out
    }

    /// The middle square of a photo, as the round screen shows it.
    static func square(_ image: UIImage) -> UIImage {
        let w = image.size.width, h = image.size.height
        let side = min(w, h)
        let format = UIGraphicsImageRendererFormat()
        format.scale = 1
        let out = min(side, 720)
        return UIGraphicsImageRenderer(size: CGSize(width: out, height: out), format: format).image { _ in
            let scale = out / side
            image.draw(in: CGRect(x: -(w - side) / 2 * scale, y: -(h - side) / 2 * scale,
                                  width: w * scale, height: h * scale))
        }
    }

    /// 240x240 JPEG, as small as it can be while still under the knob's limit.
    static func jpeg(from image: UIImage, limit: Int = KnobProtocol.maxArtworkBytes) -> Data? {
        let format = UIGraphicsImageRendererFormat()
        format.scale = 1
        let side = CGSize(width: 240, height: 240)
        let scaled = UIGraphicsImageRenderer(size: side, format: format).image { _ in
            image.draw(in: CGRect(origin: .zero, size: side))
        }
        for quality in stride(from: 0.8, through: 0.3, by: -0.1) {
            if let data = scaled.jpegData(compressionQuality: quality),
               data.count <= limit {
                return data
            }
        }
        return nil
    }
}
