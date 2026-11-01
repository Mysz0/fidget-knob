import UIKit

/// Renders the track title for the knob in real iOS fonts, as an alpha mask
/// the knob composites over its sprite field. Any script works - accents,
/// CJK, emoji - which the knob's own ASCII font cannot draw.
enum TitleRender {
    static let maxWidth = 216
    static let height = 76

    /// [width][height][alpha...] or nil if nothing could be drawn.
    static func mask(title: String, artist: String) -> Data? {
        let width = maxWidth
        var pixels = [UInt8](repeating: 0, count: width * height)
        let drawn: Bool = pixels.withUnsafeMutableBytes { buf in
            guard let ctx = CGContext(data: buf.baseAddress, width: width, height: height,
                                      bitsPerComponent: 8, bytesPerRow: width,
                                      space: CGColorSpaceCreateDeviceGray(),
                                      bitmapInfo: CGImageAlphaInfo.none.rawValue) else { return false }
            // UIKit draws top-down; flip the bitmap context to match.
            ctx.translateBy(x: 0, y: CGFloat(height))
            ctx.scaleBy(x: 1, y: -1)
            UIGraphicsPushContext(ctx)
            defer { UIGraphicsPopContext() }

            let centred = NSMutableParagraphStyle()
            centred.alignment = .center
            centred.lineBreakMode = .byTruncatingTail

            let titleAttrs: [NSAttributedString.Key: Any] = [
                .font: UIFont.systemFont(ofSize: 25, weight: .semibold),
                .foregroundColor: UIColor.white,
                .paragraphStyle: centred,
            ]
            let artistAttrs: [NSAttributedString.Key: Any] = [
                .font: UIFont.systemFont(ofSize: 17, weight: .regular),
                .foregroundColor: UIColor(white: 1, alpha: 0.62),
                .paragraphStyle: centred,
            ]
            (title as NSString).draw(in: CGRect(x: 0, y: 8, width: width, height: 34), withAttributes: titleAttrs)
            (artist as NSString).draw(in: CGRect(x: 0, y: 44, width: width, height: 26), withAttributes: artistAttrs)
            return true
        }
        guard drawn else { return nil }
        var out = Data([UInt8(width), UInt8(height)])
        out.append(contentsOf: pixels)
        return out
    }
}
