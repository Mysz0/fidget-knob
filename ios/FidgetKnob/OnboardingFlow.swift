import SwiftUI

/// First launch, after Vessel: black pages, the knob's face glowing above a
/// page indicator, one white button at the bottom.
struct OnboardingFlow: View {
    @EnvironmentObject private var link: KnobLink
    let finish: () -> Void

    @State private var step = 0
    @State private var glow = false

    var body: some View {
        ZStack {
            Color.black.ignoresSafeArea()

            VStack(spacing: 0) {
                Spacer()

                KnobFaceView(content: .field(value: faceValue), bezel: 4)
                    .frame(width: 150, height: 150)
                    .shadow(color: .white.opacity(glow ? 0.16 : 0.06), radius: 40)
                    .animation(.easeInOut(duration: 2.4).repeatForever(autoreverses: true), value: glow)
                    .animation(.spring(response: 0.6, dampingFraction: 0.8), value: step)

                indicator
                    .padding(.top, 36)

                page
                    .padding(.top, 28)
                    .padding(.horizontal, 36)
                    .id(step)
                    .transition(.asymmetric(insertion: .move(edge: .trailing).combined(with: .opacity),
                                            removal: .move(edge: .leading).combined(with: .opacity)))

                Spacer()
                Spacer()

                button
                    .padding(.horizontal, 24)
                    .padding(.bottom, 24)
            }
        }
        .preferredColorScheme(.dark)
        .onAppear { glow = true }
        .onChange(of: link.status) { _, status in
            if step == 2 && status == .connected { finish() }
        }
    }

    /// The face fills in as setup goes on, like the knob brightening.
    private var faceValue: Double { [0.08, 0.3, link.status == .connected ? 1 : 0.6][step] }

    private var indicator: some View {
        HStack(spacing: 6) {
            ForEach(0..<3) { i in
                Capsule()
                    .fill(i == step ? Color.white : Color.white.opacity(0.25))
                    .frame(width: i == step ? 20 : 6, height: 6)
            }
        }
        .animation(.spring(response: 0.35, dampingFraction: 0.8), value: step)
    }

    @ViewBuilder
    private var page: some View {
        switch step {
        case 0:
            StepText(symbol: "hand.wave", title: "Fidget Knob",
                     message: "Lights at home, music anywhere. Twist, tap or shake the knob - this app picks what it controls.")
        case 1:
            StepText(symbol: "checkmark.shield", title: "Bluetooth",
                     message: "The app talks to your knob over Bluetooth to switch its mode and send album covers.")
        default:
            StepText(symbol: "dot.radiowaves.left.and.right", title: "Find your knob",
                     message: "Pick the knob up so it wakes. If it isn't paired yet, pair it in Settings, Bluetooth, under Other Devices.")
        }
    }

    private var button: some View {
        Button {
            switch step {
            case 0: withAnimation { step = 1 }
            case 1:
                link.start()
                withAnimation { step = 2 }
            default: finish()
            }
        } label: {
            Text(buttonTitle)
                .font(.system(size: 17, weight: .semibold))
                .foregroundStyle(.black)
                .frame(maxWidth: .infinity)
                .frame(height: 54)
                .background(Color.white, in: RoundedRectangle(cornerRadius: Theme.buttonRadius, style: .continuous))
        }
        .buttonStyle(ScaleButtonStyle())
    }

    private var buttonTitle: String {
        switch step {
        case 0: return "Get Started"
        case 1: return "Allow Bluetooth"
        default: return link.status == .connected ? "Done" : "Searching..."
        }
    }
}

private struct StepText: View {
    let symbol: String
    let title: String
    let message: String

    var body: some View {
        VStack(spacing: 14) {
            Image(systemName: symbol)
                .font(.system(size: 34, weight: .regular))
                .foregroundStyle(.white)
            Text(title)
                .font(.system(size: 28, weight: .bold))
                .foregroundStyle(.white)
            Text(message)
                .font(.system(size: 17))
                .foregroundStyle(Color.white.opacity(0.55))
                .multilineTextAlignment(.center)
                .fixedSize(horizontal: false, vertical: true)
        }
    }
}
