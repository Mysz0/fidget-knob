import Charts
import SwiftUI


// MARK: - Timer

/// A ring that runs down, the time, presets, and start / pause / reset.
struct TimerLiveView: View {
    @EnvironmentObject private var link: KnobLink
    @StateObject private var alarm = TimerAlarm()
    @State private var customMinutes = 10

    private var state: TimerState? { link.timer }
    private let presets = [60, 3 * 60, 5 * 60, 10 * 60, 15 * 60, 25 * 60, 45 * 60, 60 * 60]

    var body: some View {
        ScrollView {
            VStack(spacing: 24) {
                HStack {
                    ConnectionBadge()
                    Spacer()
                }
                ring
                controls
                CardSection(title: "Set") {
                    VStack(spacing: 14) {
                        LazyVGrid(columns: Array(repeating: GridItem(.flexible(), spacing: 10), count: 4), spacing: 10) {
                            ForEach(presets, id: \.self) { secs in
                                Button { set(secs) } label: {
                                    Text(TimerFormat.long(secs))
                                        .font(.subheadline.weight(.medium))
                                        .frame(maxWidth: .infinity, minHeight: 40)
                                        .background(state?.length == secs ? Color.accentColor.opacity(0.2) : Theme.fill2,
                                                    in: RoundedRectangle(cornerRadius: 12, style: .continuous))
                                }
                                .buttonStyle(.plain)
                            }
                        }
                        Stepper(value: $customMinutes, in: 1...180) {
                            LabeledContent("Custom", value: "\(customMinutes) min")
                        }
                        Button("Set \(customMinutes) min") { set(customMinutes * 60) }
                            .frame(maxWidth: .infinity, alignment: .trailing)
                    }
                    .padding(16)
                }
                .disabled(state == nil || state?.state == .running || state?.state == .paused)
            }
            .padding(.horizontal, 20)
            .padding(.top, 8)
            .padding(.bottom, 48)
        }
        .background(Theme.grouped.ignoresSafeArea())
        .onAppear { alarm.requestPermission() }
    }

    private var ring: some View {
        let left = state?.left ?? 0
        let done = state?.state == .done
        return ZStack {
            Circle().stroke(Color.accentColor.opacity(0.15), lineWidth: 14)
            Circle()
                .trim(from: 0, to: state?.state == .idle ? 1 : (state?.fractionLeft ?? 0))
                .stroke(Color.accentColor, style: StrokeStyle(lineWidth: 14, lineCap: .round))
                .rotationEffect(.degrees(-90))
                .animation(.linear(duration: 1), value: left)
            VStack(spacing: 6) {
                Text(TimerFormat.clock(left))
                    .font(.system(size: 64, weight: .semibold, design: .rounded))
                    .monospacedDigit()
                    .contentTransition(.numericText(countsDown: true))
                    .animation(.default, value: left)
                Text(done ? "Done" : state?.state == .paused ? "Paused" : state == nil ? "Not connected" : " ")
                    .font(.headline)
                    .foregroundStyle(.secondary)
            }
        }
        .frame(width: 270, height: 270)
        .padding(.top, 12)
        .opacity(state == nil ? 0.4 : 1)
    }

    private var controls: some View {
        HStack(spacing: 28) {
            Button { link.send(KnobCommands.timerReset) } label: {
                Image(systemName: "arrow.counterclockwise").font(.title3).frame(width: 52, height: 52)
            }
            .buttonStyle(.glass)
            .buttonBorderShape(.circle)
            .disabled(state?.state == .idle)

            Button { link.send(KnobCommands.timerToggle) } label: {
                Image(systemName: state?.state == .running ? "pause.fill" : "play.fill")
                    .font(.title)
                    .frame(width: 76, height: 76)
                    .contentTransition(.symbolEffect(.replace))
            }
            .buttonStyle(.glassProminent)
            .buttonBorderShape(.circle)
        }
        .disabled(state == nil)
        .sensoryFeedback(.impact, trigger: state?.state)
    }

    private func set(_ seconds: Int) {
        link.send(KnobCommands.timerLength(seconds: seconds))
    }
}

// MARK: - Clock

/// The knob's clock face, mirrored: the time over a field that fills each
/// minute, just as the knob draws it.
struct ClockLiveView: View {
    @EnvironmentObject private var link: KnobLink
    @State private var synced = false

    var body: some View {
        VStack(spacing: 0) {
            HStack {
                ConnectionBadge()
                Spacer()
            }
            .padding(.horizontal, 24)
            .padding(.top, 8)

            Spacer()

            TimelineView(.periodic(from: .now, by: 1)) { context in
                let second = Calendar.current.component(.second, from: context.date)
                ZStack {
                    KnobFaceView(content: .field(value: 0.04 + 0.6 * Double(second) / 59), bezel: 10)
                        .opacity(0.45)
                        .animation(.easeOut(duration: 0.4), value: second)
                    VStack(spacing: 4) {
                        Text(context.date, format: .dateTime.hour(.twoDigits(amPM: .omitted)).minute(.twoDigits))
                            .font(.system(size: 64, weight: .bold))
                            .monospacedDigit()
                        Text(context.date, format: .dateTime.weekday(.abbreviated).day().month(.abbreviated))
                            .font(.title3)
                            .foregroundStyle(.secondary)
                    }
                }
                .frame(width: 290, height: 290)
                .shadow(color: .black.opacity(0.45), radius: 30, y: 16)
            }

            Spacer()

            Button {
                link.sendTime()
                synced = true
            } label: {
                Label(synced ? "Synced" : "Sync time", systemImage: synced ? "checkmark" : "arrow.triangle.2.circlepath")
                    .padding(.horizontal, 8)
                    .contentTransition(.symbolEffect(.replace))
            }
            .buttonStyle(.glass)
            .disabled(link.status != .connected)
            .sensoryFeedback(.success, trigger: synced)
            .padding(.bottom, 40)
        }
        .frame(maxWidth: .infinity)
        .background(Theme.grouped.ignoresSafeArea())
    }
}

// MARK: - Dice

/// Roll from the phone or the knob; both show the same result.
struct DiceLiveView: View {
    @EnvironmentObject private var link: KnobLink
    @State private var history: [String] = []
    @State private var tumble = 0

    private var state: DiceState? { link.dice }
    private var kind: DiceKind { state?.kind ?? .d6 }

    var body: some View {
        VStack(spacing: 0) {
            HStack {
                ConnectionBadge()
                Spacer()
            }
            .padding(.horizontal, 24)
            .padding(.top, 8)

            Spacer()

            ZStack {
                KnobFaceView(content: .field(value: state?.rolling == true ? 0.7 : (state?.result ?? 0) > 0 ? 0.5 : 0.08), bezel: 10)
                    .opacity(0.5)
                    .rotationEffect(.degrees(Double(tumble) * 37))
                Text(resultText)
                    .font(.system(size: 72, weight: .bold, design: .rounded))
                    .contentTransition(.numericText())
                    .animation(.spring(response: 0.3, dampingFraction: 0.6), value: resultText)
            }
            .frame(width: 290, height: 290)
            .shadow(color: .black.opacity(0.45), radius: 30, y: 16)
            .animation(.spring(response: 0.25, dampingFraction: 0.5), value: tumble)

            if !history.isEmpty {
                Text(history.suffix(8).reversed().joined(separator: "  "))
                    .font(.subheadline.monospacedDigit())
                    .foregroundStyle(.secondary)
                    .lineLimit(1)
                    .padding(.top, 24)
                    .padding(.horizontal, 24)
            }

            Spacer()

            Picker("Dice", selection: Binding(get: { kind }, set: { link.send(KnobCommands.diceKind($0)) })) {
                ForEach(DiceKind.allCases) { Text($0.title).tag($0) }
            }
            .pickerStyle(.segmented)
            .padding(.horizontal, 24)

            Button { link.send(KnobCommands.diceRoll) } label: {
                Label("Roll", systemImage: "dice.fill").font(.headline).padding(.horizontal, 28).padding(.vertical, 6)
            }
            .buttonStyle(.glassProminent)
            .disabled(state == nil || state?.rolling == true)
            .padding(.top, 18)
            .padding(.bottom, 40)
        }
        .frame(maxWidth: .infinity)
        .background(Theme.grouped.ignoresSafeArea())
        .onChange(of: state) { old, new in
            guard let new else { return }
            if new.rolling { tumble += 1 }
            if old?.rolling == true, !new.rolling, new.result > 0 {
                history.append(new.kind.text(new.result))
            }
            if old?.kind != new.kind { history.removeAll() }
        }
        .sensoryFeedback(.impact(weight: .heavy), trigger: history.count)
    }

    private var resultText: String {
        guard let s = state else { return "?" }
        if s.rolling { return "…" }
        return s.result > 0 ? s.kind.text(s.result) : "?"
    }
}
