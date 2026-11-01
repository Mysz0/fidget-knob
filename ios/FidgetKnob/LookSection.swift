import SwiftUI

/// Knob tab > Look: the sprite pack, the palette and the effect. Each choice
/// is previewed as a little knob face drawn exactly as the knob will draw it.
struct LookSection: View {
    @EnvironmentObject private var link: KnobLink

    private var settings: KnobSettings { link.settings ?? KnobSettings() }

    var body: some View {
        CardSection(title: "Look") {
            VStack(alignment: .leading, spacing: 18) {
                ScrollView(.horizontal, showsIndicators: false) {
                    HStack(spacing: 14) {
                        ForEach(SpriteArt.packs.indices, id: \.self) { i in
                            packTile(i)
                        }
                    }
                    .padding(.horizontal, 16)
                }

                HStack(spacing: 0) {
                    ForEach(SpriteArt.palettes.indices, id: \.self) { i in
                        paletteSwatch(i).frame(maxWidth: .infinity)
                    }
                    customSwatch.frame(maxWidth: .infinity)
                }
                .padding(.horizontal, 10)

                if settings.palette == KnobSettings.customPalette {
                    VStack(spacing: 4) {
                        ColorPicker("Main colour", selection: colourBinding(\.customWhite), supportsOpacity: false)
                        ColorPicker("Accent", selection: colourBinding(\.customAccent), supportsOpacity: false)
                    }
                    .padding(.horizontal, 16)
                    .transition(.opacity)
                }

                Picker("Effect", selection: Binding(
                    get: { settings.effect },
                    set: { e in change { $0.effect = e } }
                )) {
                    ForEach(KnobLook.Effect.allCases) { Text($0.title).tag($0) }
                }
                .pickerStyle(.segmented)
                .padding(.horizontal, 16)
            }
            .padding(.vertical, 16)
        }
        .disabled(link.settings == nil)
        .sensoryFeedback(.selection, trigger: settings.look)
    }

    private func packTile(_ i: Int) -> some View {
        let on = Int(settings.spritePack) == i
        return Button {
            change { $0.spritePack = UInt8(i) }
        } label: {
            VStack(spacing: 7) {
                KnobFaceView(content: .field(value: 0.8), bezel: 2)
                    .environment(\.knobLook, KnobLook(pack: i, palette: Int(settings.palette), effect: settings.effect))
                    .frame(width: 72, height: 72)
                    .overlay(Circle().strokeBorder(on ? Color.primary : .clear, lineWidth: 2).padding(-5))
                Text(SpriteArt.packs[i].name)
                    .font(.caption.weight(on ? .semibold : .regular))
                    .foregroundStyle(on ? .primary : .secondary)
            }
            .padding(.top, 5)
        }
        .buttonStyle(.plain)
    }

    private func paletteSwatch(_ i: Int) -> some View {
        let on = Int(settings.palette) == i
        let p = SpriteArt.palettes[i]
        return Button {
            change { $0.palette = UInt8(i) }
        } label: {
            VStack(spacing: 6) {
                ZStack {
                    Circle().fill(p.white)
                    Circle().trim(from: 0, to: 0.5).fill(p.accent).rotationEffect(.degrees(45))
                }
                .frame(width: 32, height: 32)
                .overlay(Circle().strokeBorder(on ? Color.primary : .clear, lineWidth: 2).padding(-4))
                Text(p.name)
                    .font(.caption2.weight(on ? .semibold : .regular))
                    .foregroundStyle(on ? .primary : .secondary)
                    .lineLimit(1)
                    .minimumScaleFactor(0.8)
            }
        }
        .buttonStyle(.plain)
    }

    private var customSwatch: some View {
        let on = settings.palette == KnobSettings.customPalette
        return Button {
            withAnimation(.easeOut(duration: 0.2)) { change { $0.palette = KnobSettings.customPalette } }
        } label: {
            VStack(spacing: 6) {
                ZStack {
                    if on {
                        Circle().fill(Color(rgb: settings.customWhite))
                        Circle().trim(from: 0, to: 0.5).fill(Color(rgb: settings.customAccent)).rotationEffect(.degrees(45))
                    } else {
                        Circle().fill(AngularGradient(colors: [.red, .yellow, .green, .cyan, .blue, .purple, .red],
                                                      center: .center))
                    }
                }
                .frame(width: 32, height: 32)
                .overlay(Circle().strokeBorder(on ? Color.primary : .clear, lineWidth: 2).padding(-4))
                Text("Custom")
                    .font(.caption2.weight(on ? .semibold : .regular))
                    .foregroundStyle(on ? .primary : .secondary)
                    .lineLimit(1)
                    .minimumScaleFactor(0.8)
            }
        }
        .buttonStyle(.plain)
    }

    /// A picked colour goes to the knob a moment after the picker stops
    /// moving, not on every step of a drag across the wheel.
    private func colourBinding(_ path: WritableKeyPath<KnobSettings, [UInt8]>) -> Binding<Color> {
        Binding(
            get: { Color(rgb: settings[keyPath: path]) },
            set: { colour in
                let bytes = colour.rgbBytes
                guard bytes != settings[keyPath: path] else { return }
                change { $0[keyPath: path] = bytes }
            }
        )
    }

    private func change(_ edit: (inout KnobSettings) -> Void) {
        var s = settings
        edit(&s)
        link.updateSettings(s)
    }
}
