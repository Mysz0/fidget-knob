import SwiftUI

/// The device itself, after Vessel: a device card, the mode, and its look.
struct KnobTab: View {
    @EnvironmentObject private var link: KnobLink
    @EnvironmentObject private var cover: CoverModel

    private var connected: Bool { link.status == .connected }
    private var inMusic: Bool { link.mode == .music }

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(spacing: 16) {
                    deviceCard
                    if connected {
                        CardSection(title: "Mode") {
                            ForEach(KnobLink.Mode.allCases) { mode in
                                if mode != .lights { Divider().padding(.leading, 66) }
                                modeRow(mode, title: mode.title, subtitle: mode.subtitle, icon: mode.icon)
                            }
                        }
                        LookSection()
                        if inMusic {
                            CardSection(title: "Cover on the knob") {
                                coverRow(.mosaic, title: "Mosaic", subtitle: "The cover painted in sprites", icon: "square.grid.3x3")
                                Divider().padding(.leading, 66)
                                coverRow(.photo, title: "Photo", subtitle: "The artwork itself", icon: "photo")
                            }
                        }
                    } else {
                        notConnectedCard
                    }
                }
                .padding(.horizontal, 20)
                .padding(.top, 4)
                .padding(.bottom, 48)
                .animation(.spring(response: 0.35, dampingFraction: 0.85), value: link.mode)
                .animation(.spring(response: 0.35, dampingFraction: 0.85), value: connected)
            }
            .background(Theme.grouped.ignoresSafeArea())
            .navigationTitle("Fidget Knob")
            .navigationBarTitleDisplayMode(.large)
        }
    }

    private var deviceCard: some View {
        VStack(spacing: 0) {
            HStack(alignment: .center) {
                VStack(alignment: .leading, spacing: 5) {
                    Text("Knob").font(.system(size: 22, weight: .semibold))
                    ConnectionBadge()
                }
                Spacer()
                SpinningFace(content: smallFace, spinning: connected && inMusic && (link.nowPlaying?.playing ?? false), bezel: 2)
                    .frame(width: 52, height: 52)
                    .opacity(connected ? 1 : 0.35)
            }
            .padding(.horizontal, 20)
            .padding(.top, 20)
            .padding(.bottom, 16)

            Divider().padding(.horizontal, 20)

            HStack(spacing: 0) {
                cell(label: "Mode", value: connected ? (link.mode?.title ?? "-") : "-",
                     icon: link.mode?.icon ?? "dial.medium")
                Divider().frame(height: 36)
                cell(label: "Screen", value: link.settings.map { "\($0.screenOffSeconds) s" } ?? "-",
                     icon: "sun.max")
                Divider().frame(height: 36)
                cell(label: "Sleep", value: link.settings.map { "\($0.sleepMinutes) min" } ?? "-",
                     icon: "moon")
            }
            .padding(.horizontal, 8)
            .padding(.vertical, 12)
        }
        .background(Theme.card)
        .clipShape(RoundedRectangle(cornerRadius: Theme.cardRadius, style: .continuous))
    }

    private var smallFace: KnobFaceView.Content {
        if inMusic, let image = cover.image {
            return link.coverStyle == .photo ? .photo(image) : .mosaic(image)
        }
        return .field(value: connected ? 0.5 : 0.08)
    }

    private func cell(label: String, value: String, icon: String) -> some View {
        VStack(spacing: 4) {
            Image(systemName: icon)
                .font(.system(size: 14, weight: .light))
                .foregroundStyle(Theme.secondary)
            Text(value)
                .font(.system(size: 13, weight: .medium))
                .lineLimit(1)
                .minimumScaleFactor(0.7)
                .contentTransition(.numericText())
            Text(label).font(.caption2).foregroundStyle(Theme.secondary)
        }
        .frame(maxWidth: .infinity)
    }

    private func modeRow(_ mode: KnobLink.Mode, title: String, subtitle: String, icon: String) -> some View {
        Button { link.setMode(mode) } label: {
            IconRow(icon: icon, title: title, subtitle: subtitle) {
                if link.switchingMode && link.mode == mode {
                    ProgressView().controlSize(.small)
                } else if link.mode == mode {
                    Image(systemName: "checkmark").font(.system(size: 14, weight: .semibold))
                }
            }
        }
        .buttonStyle(HighlightButtonStyle())
        .disabled(link.switchingMode)
        .sensoryFeedback(.selection, trigger: link.mode)
    }

    private func coverRow(_ style: KnobLink.CoverStyle, title: String, subtitle: String, icon: String) -> some View {
        Button { link.setCoverStyle(style) } label: {
            IconRow(icon: icon, title: title, subtitle: subtitle) {
                if link.coverStyle == style {
                    Image(systemName: "checkmark").font(.system(size: 14, weight: .semibold))
                }
            }
        }
        .buttonStyle(HighlightButtonStyle())
        .sensoryFeedback(.selection, trigger: link.coverStyle)
    }

    private var notConnectedCard: some View {
        VStack(spacing: 8) {
            Text("Not Connected").font(.system(size: 16, weight: .semibold))
            Text(link.status == .bluetoothOff ? "Turn on Bluetooth to reach your knob."
                 : link.status == .unauthorized ? "Allow Bluetooth for Fidget Knob in Settings."
                 : "Pick the knob up so it wakes. It connects as soon as it is nearby.")
                .font(.subheadline)
                .foregroundStyle(Theme.secondary)
                .multilineTextAlignment(.center)
                .fixedSize(horizontal: false, vertical: true)
        }
        .padding(24)
        .frame(maxWidth: .infinity)
        .background(Theme.card)
        .clipShape(RoundedRectangle(cornerRadius: Theme.cardRadius, style: .continuous))
    }
}
