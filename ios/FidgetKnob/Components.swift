import SwiftUI

/// A titled group of rows in one card (Vessel's section style).
struct CardSection<Content: View>: View {
    let title: String
    @ViewBuilder let content: Content

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            Text(title)
                .font(.footnote.weight(.semibold))
                .foregroundStyle(Theme.secondary)
                .textCase(.uppercase)
                .padding(.leading, 2)
            VStack(spacing: 1) { content }
                .background(Theme.card)
                .clipShape(RoundedRectangle(cornerRadius: Theme.cardRadius, style: .continuous))
        }
    }
}

/// Icon tile, title and subtitle, and a trailing accessory.
struct IconRow<Accessory: View>: View {
    let icon: String
    let title: String
    var subtitle: String? = nil
    @ViewBuilder let accessory: Accessory

    var body: some View {
        HStack(spacing: 14) {
            ZStack {
                RoundedRectangle(cornerRadius: Theme.iconRadius, style: .continuous)
                    .fill(Theme.iconBg)
                    .frame(width: 36, height: 36)
                Image(systemName: icon)
                    .font(.system(size: 15, weight: .light))
                    .foregroundStyle(Theme.iconFg)
            }
            VStack(alignment: .leading, spacing: 2) {
                Text(title)
                    .font(.system(size: 15))
                    .foregroundStyle(Theme.primary)
                if let subtitle {
                    Text(subtitle)
                        .font(.caption)
                        .foregroundStyle(Theme.secondary)
                }
            }
            Spacer(minLength: 8)
            accessory
        }
        .padding(.horizontal, 16)
        .padding(.vertical, 12)
        .contentShape(Rectangle())
    }
}

/// The connection dot and word, used in headers.
struct ConnectionBadge: View {
    @EnvironmentObject private var link: KnobLink

    var body: some View {
        HStack(spacing: 5) {
            Circle().fill(colour).frame(width: 6, height: 6)
            Text(text).font(.system(size: 13)).foregroundStyle(Theme.secondary)
        }
        .animation(.easeInOut(duration: 0.3), value: link.status)
    }

    private var colour: Color {
        if link.switchingMode { return Color(.systemOrange) }
        switch link.status {
        case .connected: return Color(.systemGreen)
        case .connecting, .searching: return Color(.systemOrange)
        default: return Theme.secondary
        }
    }

    private var text: String {
        if link.switchingMode { return "Switching..." }
        switch link.status {
        case .connected: return "Connected"
        case .connecting: return "Connecting..."
        case .searching: return "Searching..."
        case .bluetoothOff: return "Bluetooth off"
        case .unauthorized: return "No Bluetooth access"
        }
    }
}

/// The knob face, turning like a record while music plays and easing to a
/// stop on pause - it keeps its angle, so resuming does not jump.
struct SpinningFace: View {
    let content: KnobFaceView.Content
    let spinning: Bool
    var bezel: CGFloat = 8

    @State private var restingAngle: Double = 0
    @State private var spinStart: Date?

    private let degreesPerSecond = 24.0

    var body: some View {
        TimelineView(.animation(paused: !spinning)) { timeline in
            KnobFaceView(content: content, bezel: bezel)
                .rotationEffect(.degrees(angle(at: timeline.date)))
        }
        .onAppear { if spinning { spinStart = .now } }
        .onChange(of: spinning) { _, now in
            if now {
                spinStart = .now
            } else if let start = spinStart {
                restingAngle += Date.now.timeIntervalSince(start) * degreesPerSecond
                spinStart = nil
            }
        }
    }

    private func angle(at date: Date) -> Double {
        guard let start = spinStart else { return restingAngle }
        return restingAngle + date.timeIntervalSince(start) * degreesPerSecond
    }
}
