import SwiftUI

/// The live screen. It follows what the knob is doing: the lamp it drives in
/// Lights mode, the spinning cover in Music, the timer, the clock, the dice.
struct LiveTab: View {
    @EnvironmentObject private var link: KnobLink

    var body: some View {
        switch link.status == .connected ? link.mode : .music {
        case .lights: LampLiveView()
        case .timer: TimerLiveView()
        case .clock: ClockLiveView()
        case .dice: DiceLiveView()
        case .ambient: AmbientLiveView()
        case .photo: PhotoLiveView()
        case .notify: KnobOnlyLiveView(
            title: "Notifications",
            detail: "New notifications buzz and show on the knob. Twist through them, tap to dismiss, shake to clear all.",
            note: "If none appear: iPhone Settings > Bluetooth > Knob (i) > Share System Notifications.")
        case .games: KnobOnlyLiveView(
            title: "Games",
            detail: "Twist to pick Tilt or Memory, tap to start, shake to quit.",
            note: nil)
        default: MusicLiveView()
        }
    }
}

/// The show-off screen: the knob's face spinning with the cover, over a glow
/// taken from the cover's own colours.
struct MusicLiveView: View {
    @EnvironmentObject private var link: KnobLink
    @EnvironmentObject private var cover: CoverModel

    private var inMusic: Bool { link.mode == .music && link.status == .connected }
    private var playing: Bool { inMusic && (link.nowPlaying?.playing ?? false) }

    var body: some View {
        ZStack {
            ambient

            VStack(spacing: 0) {
                HStack {
                    ConnectionBadge()
                    Spacer()
                }
                .padding(.horizontal, 24)
                .padding(.top, 8)

                Spacer(minLength: 12)

                SpinningFace(content: face, spinning: playing, bezel: 10)
                    .frame(width: 290, height: 290)
                    .shadow(color: .black.opacity(0.45), radius: 30, y: 16)
                    .scaleEffect(playing ? 1 : 0.94)
                    .animation(.spring(response: 0.6, dampingFraction: 0.8), value: playing)

                VStack(spacing: 6) {
                    Text(titleLine)
                        .font(.system(size: 26, weight: .bold))
                        .lineLimit(1)
                        .contentTransition(.opacity)
                    Text(subtitleLine)
                        .font(.system(size: 17))
                        .foregroundStyle(.secondary)
                        .lineLimit(1)
                }
                .padding(.horizontal, 28)
                .padding(.top, 34)
                .animation(.easeOut(duration: 0.25), value: link.nowPlaying?.trackKey)

                Spacer(minLength: 12)

                bottomControl
                    .padding(.horizontal, 24)
                    .padding(.bottom, 24)
            }
        }
    }

    // MARK: - Pieces

    @ViewBuilder
    private var ambient: some View {
        ZStack {
            Theme.grouped
            if inMusic, let image = cover.image {
                Image(uiImage: image)
                    .resizable()
                    .aspectRatio(contentMode: .fill)
                    .blur(radius: 70)
                    .opacity(0.6)
                    .transition(.opacity)
            }
        }
        .ignoresSafeArea()
        .animation(.easeInOut(duration: 0.6), value: cover.image)
    }

    private var face: KnobFaceView.Content {
        if inMusic, let image = cover.image {
            return link.coverStyle == .photo ? .photo(image) : .mosaic(image)
        }
        return .field(value: inMusic ? (link.nowPlaying == nil ? 0.08 : 0.55) : 0.3)
    }

    private var titleLine: String {
        guard link.status == .connected else { return "Knob is away" }
        guard inMusic else { return "Starting..." }
        return link.nowPlaying?.title ?? "Nothing playing"
    }

    private var subtitleLine: String {
        guard link.status == .connected else { return "Pick it up so it wakes" }
        guard inMusic else { return "" }
        guard let np = link.nowPlaying else { return "Play something on your iPhone" }
        return cover.state == .looking ? "\(np.artist) · finding the cover" : np.artist
    }

    @ViewBuilder
    private var bottomControl: some View {
        if link.status == .connected && inMusic {
            CoverStyleToggle()
        }
    }
}


/// Mosaic or photo, as a compact glass toggle: two icons, the chosen one lit.
struct CoverStyleToggle: View {
    @EnvironmentObject private var link: KnobLink
    @Namespace private var glass

    var body: some View {
        GlassEffectContainer(spacing: 8) {
            HStack(spacing: 4) {
                option(.mosaic, icon: "square.grid.3x3.fill", label: "Mosaic")
                option(.photo, icon: "photo.fill", label: "Photo")
            }
            .padding(4)
            .glassEffect(.regular, in: Capsule())
        }
        .fixedSize()
        .sensoryFeedback(.selection, trigger: link.coverStyle)
    }

    private func option(_ style: KnobLink.CoverStyle, icon: String, label: String) -> some View {
        let on = link.coverStyle == style
        return Button {
            withAnimation(.spring(response: 0.3, dampingFraction: 0.8)) { link.setCoverStyle(style) }
        } label: {
            Label(label, systemImage: icon)
                .font(.system(size: 15, weight: .semibold))
                .foregroundStyle(on ? Color.primary : Color.secondary)
                .padding(.horizontal, 16)
                .padding(.vertical, 10)
                .background {
                    if on {
                        Capsule().fill(Color.primary.opacity(0.12))
                            .matchedGeometryEffect(id: "chosen", in: glass)
                    }
                }
        }
        .buttonStyle(.plain)
        .accessibilityAddTraits(on ? .isSelected : [])
    }
}

/// Lights mode, live: the lamp the knob drives, with the same controls.
struct LampLiveView: View {
    @EnvironmentObject private var home: HomeModel
    @State private var level: Double = 50
    @State private var hue: Double = 30

    var body: some View {
        NavigationStack {
            Group {
                if !home.isPaired {
                    ContentUnavailableView("No IKEA hub yet", systemImage: "wifi.router",
                                           description: Text("Pair your hub in Settings to see and control your lamp here."))
                } else if let lamp = home.selectedLamp {
                    lampView(lamp)
                } else if let error = home.lightsError {
                    ContentUnavailableView("Hub not reachable", systemImage: "wifi.exclamationmark",
                                           description: Text(error))
                } else {
                    ProgressView()
                }
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)
            .background(Theme.grouped.ignoresSafeArea())
            .navigationTitle(home.selectedLamp?.name ?? "Lights")
            .task { await home.refreshLights() }
        }
    }

    private func lampView(_ lamp: HubLight) -> some View {
        VStack(spacing: 28) {
            Spacer(minLength: 0)
            // The glow is the lamp: its colour, and as bright as the lamp is.
            ZStack {
                Circle()
                    .fill(glowColour(lamp))
                    .blur(radius: 50)
                    .opacity(lamp.isOn ? 0.35 + 0.6 * Double(lamp.level) / 100 : 0.05)
                    .frame(width: 260, height: 260)
                KnobFaceView(content: .field(value: lamp.isOn ? Double(lamp.level) / 100 : 0.02), bezel: 8)
                    .frame(width: 230, height: 230)
            }
            .animation(.easeOut(duration: 0.25), value: lamp.level)
            .animation(.easeOut(duration: 0.25), value: lamp.isOn)

            Button {
                home.setOn(!lamp.isOn)
            } label: {
                Label(lamp.isOn ? "On" : "Off", systemImage: "power")
                    .font(.system(size: 17, weight: .semibold))
                    .frame(width: 120)
                    .padding(.vertical, 6)
            }
            .buttonStyle(.glass)
            .sensoryFeedback(.impact, trigger: lamp.isOn)

            VStack(spacing: 18) {
                labelledSlider(icon: "sun.min", trailing: "sun.max", value: $level, range: 1...100) {
                    home.setLevel(Int($0))
                }
                if lamp.supportsColour {
                    labelledSlider(icon: "paintpalette", trailing: nil, value: $hue, range: 0...359) {
                        home.setHue($0)
                    }
                    .tint(Color(hue: hue / 360, saturation: 0.8, brightness: 1))
                }
            }
            .padding(20)
            .glassEffect(.regular, in: RoundedRectangle(cornerRadius: 28, style: .continuous))
            .padding(.horizontal, 20)
            Spacer(minLength: 0)
        }
        .padding(.bottom, 24)
        .onAppear {
            level = Double(lamp.level)
            hue = lamp.hue
        }
        .onChange(of: lamp.id) { _, _ in
            level = Double(lamp.level)
            hue = lamp.hue
        }
    }

    private func labelledSlider(icon: String, trailing: String?, value: Binding<Double>,
                                range: ClosedRange<Double>, commit: @escaping (Double) -> Void) -> some View {
        HStack(spacing: 12) {
            Image(systemName: icon).foregroundStyle(.secondary).frame(width: 22)
            Slider(value: value, in: range)
                .onChange(of: value.wrappedValue) { _, v in commit(v) }
            if let trailing { Image(systemName: trailing).foregroundStyle(.secondary).frame(width: 22) }
        }
    }

    private func glowColour(_ lamp: HubLight) -> Color {
        lamp.supportsColour && lamp.saturation > 0.05
            ? Color(hue: lamp.hue / 360, saturation: lamp.saturation, brightness: 1)
            : Color(red: 1, green: 0.84, blue: 0.63)
    }
}
