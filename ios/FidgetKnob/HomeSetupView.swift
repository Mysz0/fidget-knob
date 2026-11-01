import SwiftUI

/// Settings > Home: the knob's WiFi, the IKEA hub, and its lamps.
struct HomeSetupSection: View {
    @EnvironmentObject private var link: KnobLink
    @EnvironmentObject private var home: HomeModel
    @State private var showWifi = false
    @State private var pairing = false

    var body: some View {
        Section("Home") {
            Button { showWifi = true } label: {
                LabeledContent {
                    WifiStatusLabel(status: link.knobStatus)
                } label: {
                    Label("WiFi", systemImage: "wifi")
                }
            }
            .foregroundStyle(.primary)
            .disabled(link.status != .connected)

            Button { pairing = true } label: {
                LabeledContent {
                    if home.isPaired {
                        HStack(spacing: 6) {
                            Circle().fill(link.knobStatus?.hubConnected == true ? Color.green : Color.secondary)
                                .frame(width: 7, height: 7)
                            Text(home.hubHost ?? "")
                        }
                    } else {
                        Text("Pair")
                    }
                } label: {
                    Label("IKEA hub", systemImage: "wifi.router")
                }
            }
            .foregroundStyle(.primary)

            if home.isPaired {
                if let error = home.lightsError {
                    Text(error).font(.footnote).foregroundStyle(.secondary)
                }
                Picker(selection: Binding(
                    get: { home.selectedLampId ?? "" },
                    set: { id in if let lamp = home.lights.first(where: { $0.id == id }) { home.choose(lamp, link: link) } }
                )) {
                    ForEach(home.lights) { lamp in
                        Label(lamp.name, systemImage: lamp.supportsColour ? "lightbulb.led" : "lightbulb")
                            .tag(lamp.id)
                    }
                } label: {
                    Label("Lamp", systemImage: "lightbulb")
                }
                .pickerStyle(.navigationLink)

                NavigationLink {
                    LampCycleView()
                } label: {
                    LabeledContent {
                        Text(home.lampCycle.isEmpty ? "All lamps" : "\(home.lampCycle.count) lamps")
                    } label: {
                        Label("Next-lamp gesture", systemImage: "arrow.triangle.2.circlepath")
                    }
                }
            }
        }
        .sheet(isPresented: $showWifi) { WifiSheet() }
        .sheet(isPresented: $pairing) { HubPairingView() }
        .task { if home.isPaired { await home.refreshLights() } }
    }
}

/// "Network ▂▄▆" or what is wrong.
struct WifiStatusLabel: View {
    let status: KnobStatus?

    var body: some View {
        if let status {
            switch status.wifi {
            case .connected:
                HStack(spacing: 6) {
                    Text(status.ssid)
                    Image(systemName: "wifi", variableValue: Double(status.bars) / 4)
                }
            case .connecting: Text("Connecting to \(status.ssid)")
            case .failed: Text("Can't join \(status.ssid)").foregroundStyle(.red)
            case .off: Text(status.ssid.isEmpty ? "Not set" : status.ssid)
            }
        } else {
            Text("-")
        }
    }
}

/// The WiFi popup: what the knob is on now, and the networks it can see.
struct WifiSheet: View {
    @EnvironmentObject private var link: KnobLink
    @Environment(\.dismiss) private var dismiss
    @State private var chosen: WifiNetwork?
    @State private var password = ""

    var body: some View {
        NavigationStack {
            List {
                Section {
                    HStack(spacing: 14) {
                        Image(systemName: "wifi", variableValue: Double(link.knobStatus?.bars ?? 0) / 4)
                            .font(.system(size: 28))
                            .foregroundStyle(link.knobStatus?.wifi == .connected ? Color.accentColor : .secondary)
                            .frame(width: 40)
                        VStack(alignment: .leading, spacing: 3) {
                            Text(link.knobStatus?.ssid.isEmpty == false ? link.knobStatus!.ssid : "No network")
                                .font(.headline)
                            Text(subtitle).font(.subheadline).foregroundStyle(.secondary)
                        }
                    }
                    .padding(.vertical, 6)
                }

                Section {
                    if let networks = link.networks {
                        ForEach(networks) { net in
                            Button {
                                // Already on it: nothing to join (and no way to
                                // wipe the saved password by accident).
                                guard !isCurrent(net) else { return }
                                password = ""
                                chosen = net
                            } label: {
                                HStack {
                                    Text(net.ssid).foregroundStyle(.primary)
                                    Spacer()
                                    if isCurrent(net) {
                                        Image(systemName: "checkmark").fontWeight(.semibold)
                                    }
                                    if net.secured { Image(systemName: "lock.fill").font(.caption).foregroundStyle(.secondary) }
                                    Image(systemName: "wifi", variableValue: bars(net.rssi))
                                        .foregroundStyle(.secondary)
                                }
                            }
                        }
                        if networks.isEmpty {
                            Text("No networks found").foregroundStyle(.secondary)
                        }
                    } else {
                        HStack {
                            ProgressView()
                            Text("Looking...").foregroundStyle(.secondary).padding(.leading, 6)
                        }
                    }
                } header: {
                    HStack {
                        Text("Networks the knob can see")
                        Spacer()
                        Button("Refresh") { link.scanWifi() }.font(.footnote).textCase(nil)
                    }
                }
            }
            .navigationTitle("Knob WiFi")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar { ToolbarItem(placement: .confirmationAction) { Button("Done") { dismiss() } } }
            .onAppear { link.scanWifi() }
            .alert(chosen.map { "Join \"\($0.ssid)\"" } ?? "", isPresented: Binding(
                get: { chosen != nil }, set: { if !$0 { chosen = nil } }
            )) {
                if chosen?.secured == true {
                    SecureField("Password", text: $password)
                }
                Button("Join") { join() }
                    .disabled(chosen?.secured == true && password.isEmpty)
                Button("Cancel", role: .cancel) { chosen = nil }
            } message: {
                Text("The knob restarts to join it. If it can't, it goes back to the network it was on.")
            }
        }
        .presentationDetents([.medium, .large])
    }

    private var subtitle: String {
        switch link.knobStatus?.wifi {
        case .connected: return "Connected · used to reach your IKEA hub"
        case .connecting: return "Connecting..."
        case .failed: return "Can't connect - check the password"
        default: return "WiFi is only used in Lights mode"
        }
    }

    private func isCurrent(_ net: WifiNetwork) -> Bool {
        net.ssid == link.knobStatus?.ssid && link.knobStatus?.wifi == .connected
    }

    private func bars(_ rssi: Int) -> Double {
        rssi < -85 ? 0.25 : rssi < -72 ? 0.5 : rssi < -60 ? 0.75 : 1
    }

    private func join() {
        guard let net = chosen else { return }
        var config = HomeConfig()
        config.ssid = net.ssid
        config.password = net.secured ? password : ""
        link.sendHomeConfig(config)
        chosen = nil
        dismiss()
    }
}

/// Which lamps the next-lamp gesture steps through.
struct LampCycleView: View {
    @EnvironmentObject private var home: HomeModel

    var body: some View {
        List {
            Section {
                ForEach(home.lights) { lamp in
                    Toggle(isOn: Binding(
                        get: { home.lampCycle.contains(lamp.id) },
                        set: { home.setInCycle(lamp, $0) }
                    )) {
                        Label(lamp.name, systemImage: lamp.supportsColour ? "lightbulb.led" : "lightbulb")
                    }
                }
            } footer: {
                Text("The next-lamp gesture only steps through the lamps switched on here. With none on, it uses them all.")
            }
        }
        .navigationTitle("Next-lamp gesture")
    }
}

struct HubPairingView: View {
    @EnvironmentObject private var link: KnobLink
    @EnvironmentObject private var home: HomeModel
    @Environment(\.dismiss) private var dismiss
    @StateObject private var finder = HubFinder()

    enum Step: Equatable { case find, waitForButton(String), done, failed(String) }
    @State private var step: Step = .find
    @State private var manualHost = ""
    @State private var pulse = false

    var body: some View {
        NavigationStack {
            VStack(spacing: 24) {
                switch step {
                case .find: findView
                case .waitForButton: buttonView
                case .done: doneView
                case .failed(let message): failedView(message)
                }
            }
            .padding(24)
            .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .top)
            .navigationTitle("IKEA hub")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) { Button("Close") { dismiss() } }
            }
        }
        .onAppear { finder.start() }
        .onDisappear { finder.stop() }
    }

    private var findView: some View {
        VStack(spacing: 20) {
            Image(systemName: "wifi.router")
                .font(.system(size: 54, weight: .light))
                .padding(.top, 20)
            Text("Looking for your hub on this WiFi")
                .font(.headline)
            if finder.hosts.isEmpty {
                ProgressView()
            }
            ForEach(finder.hosts, id: \.self) { host in
                Button { pair(with: host) } label: {
                    Label("DIRIGERA at \(host)", systemImage: "checkmark.circle")
                        .frame(maxWidth: .infinity)
                        .padding(.vertical, 4)
                }
                .buttonStyle(.glassProminent)
            }
            VStack(spacing: 8) {
                TextField("Or type its address, e.g. 192.168.1.50", text: $manualHost)
                    .keyboardType(.numbersAndPunctuation)
                    .textFieldStyle(.roundedBorder)
                Button("Use this address") { pair(with: manualHost.trimmingCharacters(in: .whitespaces)) }
                    .disabled(manualHost.isEmpty)
            }
            .padding(.top, 12)
        }
    }

    private var buttonView: some View {
        VStack(spacing: 20) {
            Image(systemName: "button.programmable")
                .font(.system(size: 60, weight: .light))
                .scaleEffect(pulse ? 1.08 : 0.94)
                .animation(.easeInOut(duration: 0.9).repeatForever(autoreverses: true), value: pulse)
                .onAppear { pulse = true }
                .padding(.top, 20)
            Text("Press the action button on the bottom of your hub")
                .font(.headline)
                .multilineTextAlignment(.center)
            ProgressView()
        }
    }

    private var doneView: some View {
        VStack(spacing: 18) {
            Image(systemName: "checkmark.circle.fill")
                .font(.system(size: 60))
                .foregroundStyle(.green)
                .padding(.top, 20)
            Text("Hub paired").font(.headline)
            Text("The knob restarts to connect to it. Pick its lamp in Settings.")
                .foregroundStyle(.secondary)
                .multilineTextAlignment(.center)
            Button("Done") { dismiss() }.buttonStyle(.glassProminent)
        }
    }

    private func failedView(_ message: String) -> some View {
        VStack(spacing: 18) {
            Image(systemName: "exclamationmark.triangle")
                .font(.system(size: 54, weight: .light))
                .padding(.top, 20)
            Text(message).multilineTextAlignment(.center)
            Button("Try again") { step = .find }.buttonStyle(.glass)
        }
    }

    private func pair(with host: String) {
        guard !host.isEmpty else { return }
        finder.stop()
        let client = HubClient(host: host)
        let verifier = PKCE.makeVerifier()
        Task {
            do {
                let code = try await client.authorize(challenge: PKCE.challenge(for: verifier))
                step = .waitForButton(host)
                // The hub answers 403 until its button is pressed; give it 90 s.
                for _ in 0..<60 {
                    try await Task.sleep(for: .seconds(1.5))
                    if let token = try await client.exchange(code: code, verifier: verifier) {
                        client.token = token
                        finish(client)
                        return
                    }
                }
                step = .failed("The button wasn't pressed in time.")
            } catch {
                step = .failed("Couldn't reach a hub at \(host). Is this iPhone on the same WiFi?")
            }
        }
    }

    private func finish(_ client: HubClient) {
        home.adopt(client)
        var config = HomeConfig()
        config.hubHost = client.host
        config.hubToken = client.token
        config.hubCertPEM = client.issuerChainPEM
        link.sendHomeConfig(config)
        step = .done
    }
}
