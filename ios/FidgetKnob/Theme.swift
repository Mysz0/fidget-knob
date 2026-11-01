import SwiftUI

/// Design tokens, after Vessel: one monochrome palette built from system
/// colours, so the app follows light and dark mode on its own. The only hue
/// is the green of "connected".
enum Theme {
    static let primary = Color.primary
    static let secondary = Color.secondary
    static let fill2 = Color(.secondarySystemFill)
    static let grouped = Color(.systemGroupedBackground)
    static let card = Color(.secondarySystemGroupedBackground)
    static let separator = Color(.separator)

    static let iconFg = Color.primary.opacity(0.75)
    static let iconBg = Color(.tertiarySystemFill)

    static let cardRadius: CGFloat = 20
    static let iconRadius: CGFloat = 9
    static let buttonRadius: CGFloat = 14
}

struct ScaleButtonStyle: ButtonStyle {
    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .scaleEffect(configuration.isPressed ? 0.96 : 1.0)
            .animation(.spring(response: 0.25, dampingFraction: 0.7), value: configuration.isPressed)
    }
}

struct HighlightButtonStyle: ButtonStyle {
    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .background(configuration.isPressed ? Color(.systemFill).opacity(0.6) : Color.clear)
            .animation(.easeOut(duration: 0.12), value: configuration.isPressed)
    }
}
