import SwiftUI

/// How the knob draws its field: which sprite pack, which palette, and
/// whether the marks move on their own. Follows the knob's settings through
/// the environment, so every face in the app looks like the real one.
struct KnobLook: Equatable {
    enum Effect: UInt8, CaseIterable, Identifiable {
        case still = 0, twinkle = 1, drift = 2
        var id: UInt8 { rawValue }
        var title: String {
            switch self {
            case .still: "Still"
            case .twinkle: "Twinkle"
            case .drift: "Drift"
            }
        }
    }

    var pack = 0
    var palette = 0
    var effect = Effect.still
    var customWhite: [UInt8] = [255, 214, 160]
    var customAccent: [UInt8] = [46, 168, 84]

    var spritePack: SpriteArt.Pack { SpriteArt.packs[SpriteArt.packs.indices.contains(pack) ? pack : 0] }
    var colours: SpriteArt.Palette {
        if palette == Int(KnobSettings.customPalette) {
            return SpriteArt.Palette(name: "Custom", white: Color(rgb: customWhite), accent: Color(rgb: customAccent))
        }
        return SpriteArt.palettes[SpriteArt.palettes.indices.contains(palette) ? palette : 0]
    }
}

extension Color {
    init(rgb: [UInt8]) {
        self.init(red: Double(rgb[0]) / 255, green: Double(rgb[1]) / 255, blue: Double(rgb[2]) / 255)
    }

    /// 8-bit RGB, as the knob takes it.
    var rgbBytes: [UInt8] {
        let c = UIColor(self)
        var r: CGFloat = 0, g: CGFloat = 0, b: CGFloat = 0, a: CGFloat = 0
        c.getRed(&r, green: &g, blue: &b, alpha: &a)
        return [r, g, b].map { UInt8(max(0, min(255, ($0 * 255).rounded()))) }
    }
}

private struct KnobLookKey: EnvironmentKey {
    static let defaultValue = KnobLook()
}

extension EnvironmentValues {
    var knobLook: KnobLook {
        get { self[KnobLookKey.self] }
        set { self[KnobLookKey.self] = newValue }
    }
}

/// The knob's round screen, drawn the way the firmware draws it: a diamond of
/// 9x9 sprites, heavy at the centre and fraying at the edge, in warm white
/// with a green accent - or, with a cover, each sprite in the colour of its
/// patch of the artwork (mosaic), or the artwork itself (photo).
struct KnobFaceView: View {
    enum Content {
        case field(value: Double)
        case mosaic(UIImage)
        case photo(UIImage)
    }

    var content: Content
    var bezel: CGFloat = 0
    @Environment(\.knobLook) private var look

    var body: some View {
        ZStack {
            Circle().fill(Color.black)
            switch content {
            case .photo(let image):
                Image(uiImage: image).resizable().aspectRatio(contentMode: .fill).clipShape(Circle())
            case .field(let value):
                LiveField(value: value, colours: nil, look: look).clipShape(Circle())
            case .mosaic(let image):
                LiveField(value: 1, colours: MosaicColours(image: image), look: look).clipShape(Circle())
            }
            if bezel > 0 {
                Circle().strokeBorder(Color(white: 0.2), lineWidth: bezel)
            }
        }
        .compositingGroup()
    }
}

/// Colours of a 23x23 downsample of a cover, one per sprite cell.
struct MosaicColours {
    let cells: [[Color]]

    init(image: UIImage) {
        let data = CoverRender.mosaic(from: image)
        var rows: [[Color]] = []
        for j in 0..<CoverRender.cells {
            var row: [Color] = []
            for i in 0..<CoverRender.cells {
                let k = (j * CoverRender.cells + i) * 2
                let v = UInt16(data[k]) | UInt16(data[k + 1]) << 8
                row.append(Color(red: Double((v >> 11) << 3) / 255,
                                 green: Double(((v >> 5) & 0x3F) << 2) / 255,
                                 blue: Double((v & 0x1F) << 3) / 255))
            }
            rows.append(row)
        }
        cells = rows
    }
}

/// A still field, or one redrawn ~12 times a second when the look has an
/// effect - the rate the knob animates at.
private struct LiveField: View {
    let value: Double
    let colours: MosaicColours?
    let look: KnobLook

    var body: some View {
        if look.effect == .still {
            SpriteField(value: value, colours: colours, look: look, now: 0)
        } else {
            TimelineView(.animation(minimumInterval: 1 / 12)) { context in
                SpriteField(value: value, colours: colours, look: look,
                            now: UInt32(truncatingIfNeeded: Int64(context.date.timeIntervalSince1970 * 1000)))
            }
        }
    }
}

/// The firmware's sprite-field layout (render/field.cpp), at any size.
private struct SpriteField: View {
    let value: Double
    let colours: MosaicColours?
    let look: KnobLook
    let now: UInt32  // Milliseconds, for the effects.

    private static let half = 11

    var body: some View {
        let byWeight = Dictionary(grouping: look.spritePack.sprites, by: { $0.weight }).mapValues { $0.map(\.rows) }
        let warm = look.colours.white
        let accent = look.colours.accent
        let effect = look.effect
        return Canvas { ctx, size in
            let side = min(size.width, size.height)
            let pitch = side / 22
            let px = pitch * 9 / 11 / 9
            let clip = side / 2 * 114 / 120
            let radius = (14 + (220 - 14) * sqrt(max(0, min(1, value)))) / 120 * (side / 2)
            let centre = CGPoint(x: size.width / 2, y: size.height / 2)

            for j in -Self.half...Self.half {
                for i in -Self.half...Self.half {
                    let dx = Double(i) * pitch, dy = Double(j) * pitch
                    guard dx * dx + dy * dy <= clip * clip else { continue }

                    let h = Self.hash(UInt32(truncatingIfNeeded: i + 64) &* 0x9E37_79B1
                                      ^ UInt32(truncatingIfNeeded: j + 64) &* 0x85EB_CA77)
                    let fray = Double(h & 0xFF) / 255
                    let tierJitter = Double((h >> 8) & 0xFF) / 255 - 0.5
                    let accentRoll = Double((h >> 16) & 0xFF) / 255
                    let pick = Int(h >> 24)

                    let d = (abs(dx) + abs(dy)) / radius
                    guard d < 1 - 0.28 * fray else { continue }
                    let t = d + tierJitter * 0.3
                    let weight: SpriteArt.Weight = t < 0.45 ? .heavy : t < 0.78 ? .medium : .light
                    let options = byWeight[weight] ?? []
                    guard !options.isEmpty else { continue }
                    // The same effects as the knob (field.cpp buildCells).
                    var step = 0
                    var sparkle = 1.0
                    if effect == .drift {
                        let cycle = 2600 + (h & 0x7FF)
                        step = Int(((now &+ (h >> 12) % cycle) / cycle) & 0xFF)
                    } else if effect == .twinkle && ((h >> 5) & 0x7) < 3 {
                        let period = 1800 + Double((h >> 9) & 0x3FF) * 2.35
                        let phase = Double((h >> 19) & 0xFF) / 255
                        let wave = sin(2 * .pi * (Double(now) / period + phase))
                        sparkle = 0.3 + 0.7 * (0.5 + 0.5 * wave)
                    }
                    let rows = options[((pick + step) & 0xFF) % options.count]

                    let colour: Color
                    if let colours {
                        colour = colours.cells[j + Self.half][i + Self.half].opacity((1 - 0.15 * d) * sparkle)
                    } else if (i | j) != 0 && accentRoll < 0.10 + 0.45 * d {
                        colour = accent.opacity((0.9 - 0.25 * d) * sparkle)
                    } else {
                        colour = warm.opacity((1 - 0.3 * d) * sparkle)
                    }

                    var path = Path()
                    let origin = CGPoint(x: centre.x + dx - 4.5 * px, y: centre.y + dy - 4.5 * px)
                    for (ry, row) in rows.enumerated() {
                        for (rx, ch) in row.enumerated() where ch == "#" {
                            path.addRect(CGRect(x: origin.x + Double(rx) * px,
                                                y: origin.y + Double(ry) * px,
                                                width: px, height: px))
                        }
                    }
                    ctx.fill(path, with: .color(colour))
                }
            }
        }
    }

    /// The firmware's cell hash, so the app shows the knob's exact pattern.
    private static func hash(_ v: UInt32) -> UInt32 {
        var x = v
        x ^= x >> 16
        x = x &* 0x7FEB_352D
        x ^= x >> 15
        x = x &* 0x846C_A68B
        x ^= x >> 16
        return x
    }
}
