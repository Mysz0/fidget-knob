import SwiftUI

/// Everything stored on the knob: screen, sleep, gestures, title overlay.
/// Each change is written to the knob immediately.
struct SettingsTab: View {
    @EnvironmentObject private var link: KnobLink

    var body: some View {
        NavigationStack {
            Group {
                if let settings = link.settings, link.status == .connected {
                    form(settings)
                } else {
                    ContentUnavailableView("Knob not connected", systemImage: "dial.medium",
                                           description: Text("Settings live on the knob. Pick it up so it wakes and connects."))
                }
            }
            .navigationTitle("Settings")
        }
    }

    @State private var confirmOff = false

    private func binding<T>(_ path: WritableKeyPath<KnobSettings, T>, _ settings: KnobSettings) -> Binding<T> {
        Binding(get: { settings[keyPath: path] },
                set: { var s = settings; s[keyPath: path] = $0; link.updateSettings(s) })
    }

    private func form(_ s: KnobSettings) -> some View {
        Form {
            HomeSetupSection()

            Section {
                percentSlider("Brightness", value: binding(\.brightness, s), range: 10...100)
                Picker("Dim after", selection: binding(\.dimAfterSeconds, s)) {
                    Text("Never").tag(UInt8(0))
                    ForEach([UInt8(3), 5, 10, 15, 30, 60, 120], id: \.self) { Text(seconds($0)).tag($0) }
                }
                if s.dimAfterSeconds != 0 {
                    percentSlider("Dim to", value: binding(\.dimPercent, s), range: 2...60)
                }
                Picker("Turn off after", selection: binding(\.screenOffSeconds, s)) {
                    Text("Never").tag(UInt8(0))
                    ForEach([UInt8(5), 10, 15, 30, 60, 120, 240], id: \.self) { Text(seconds($0)).tag($0) }
                }
                Picker("Sleep after", selection: binding(\.sleepMinutes, s)) {
                    ForEach([UInt8(1), 2, 5, 10, 30, 60], id: \.self) { Text("\($0) min").tag($0) }
                }
            } header: {
                Text("Screen")
            } footer: {
                Text("Times count from when you last moved the knob. Asleep, it wakes when you pick it up.")
            }

            Section("Music gestures") {
                Picker("Tap", selection: binding(\.musicTap, s)) {
                    ForEach(KnobSettings.MusicAction.allCases) { Text($0.title).tag($0) }
                }
                Picker("Shake", selection: binding(\.musicShake, s)) {
                    ForEach(KnobSettings.MusicAction.allCases) { Text($0.title).tag($0) }
                }
                Picker("Twist", selection: binding(\.musicTwist, s)) {
                    ForEach(KnobSettings.MusicTwist.allCases) { Text($0.title).tag($0) }
                }
            }

            Section {
                Picker("Sensitivity", selection: binding(\.twistSensitivity, s)) {
                    ForEach(KnobSettings.Sensitivity.allCases) { Text($0.title).tag($0) }
                }
                Toggle("Reverse direction", isOn: binding(\.twistReverse, s))
                Toggle("Snap back to re-grip", isOn: binding(\.ratchet, s))
            } header: {
                Text("Twisting")
            } footer: {
                Text("Snap back: turn the knob back quickly to reset your hand - the quick turn back doesn't change anything. A slow turn back still does.")
            }

            Section("Lights gestures") {
                Picker("Tap", selection: binding(\.lightsTap, s)) {
                    ForEach(KnobSettings.LightsAction.allCases) { Text($0.title).tag($0) }
                }
                Picker("Shake", selection: binding(\.lightsShake, s)) {
                    ForEach(KnobSettings.LightsAction.allCases) { Text($0.title).tag($0) }
                }
                Picker("Twist", selection: binding(\.lightsTwist, s)) {
                    ForEach(KnobSettings.LightsTwist.allCases) { Text($0.title).tag($0) }
                }
            }

            Section {
                Toggle("Show the title", isOn: binding(\.overlayOn, s))
                if s.overlayOn {
                    Stepper(value: binding(\.overlaySeconds, s), in: 2...10) {
                        LabeledContent("For", value: "\(s.overlaySeconds) s")
                    }
                }
            } header: {
                Text("Song title on the knob")
            } footer: {
                Text("Shown over the sprites when the track changes, in the iPhone's own fonts.")
            }

            Section {
                Toggle("Work with Android", isOn: binding(\.mediaRemote, s))
            } header: {
                Text("Other phones")
            } footer: {
                Text("The knob also becomes a standard Bluetooth media remote, so an Android phone can pair with it in its own Bluetooth settings and take play/pause, next and volume - no app. Song titles, covers and notifications stay iPhone-only. The knob restarts when you change this.")
            }

            Section {
                Button("Turn off the knob", role: .destructive) { confirmOff = true }
                    .disabled(link.status != .connected)
                    .confirmationDialog("Turn off the knob?", isPresented: $confirmOff, titleVisibility: .visible) {
                        Button("Turn off", role: .destructive) { link.powerOff() }
                    } message: {
                        Text("Shake it firmly to turn it back on. Bumps and carrying it around won't wake it.")
                    }
            }

            Section {
                Toggle("Spin the cover while playing", isOn: binding(\.spinCover, s))
            } footer: {
                Text("The mosaic cover turns slowly on the knob, like a record.")
            }
        }
    }

    /// A percent setting as a slider; sent when the finger lifts, not on
    /// every pixel of the drag.
    private func percentSlider(_ title: String, value: Binding<UInt8>, range: ClosedRange<Double>) -> some View {
        PercentSlider(title: title, value: value, range: range)
    }

    private func seconds(_ v: UInt8) -> String {
        v >= 60 ? "\(v / 60) min" : "\(v) s"
    }
}


private struct PercentSlider: View {
    let title: String
    @Binding var value: UInt8
    let range: ClosedRange<Double>
    @State private var draft: Double?

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            LabeledContent(title, value: "\(Int(draft ?? Double(value)))%")
            Slider(value: Binding(get: { draft ?? Double(value) }, set: { draft = $0 }),
                   in: range, step: 1) { editing in
                if !editing, let d = draft {
                    value = UInt8(d)
                    draft = nil
                }
            }
        }
    }
}
