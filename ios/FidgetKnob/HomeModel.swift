import SwiftUI

/// The IKEA side: the paired hub, its lamps, and which one the knob drives.
/// Talks to the hub directly over the home network, so the Live tab can show
/// and change the lamp too.
@MainActor
final class HomeModel: ObservableObject {
    @Published private(set) var hubHost: String?
    @Published private(set) var lights: [HubLight] = []
    @Published private(set) var lightsError: String?
    @Published var selectedLampId: String? {
        didSet { UserDefaults.standard.set(selectedLampId, forKey: "lampId") }
    }

    private(set) var client: HubClient?
    /// Set by the root view; lamp changes made here are passed on to the knob.
    weak var link: KnobLink?
    /// Lamps the next-lamp gesture steps through (empty = all).
    @Published var lampCycle: Set<String> {
        didSet { UserDefaults.standard.set(Array(lampCycle), forKey: "lampCycle") }
    }

    var isPaired: Bool { client?.token != nil }
    var selectedLamp: HubLight? { lights.first { $0.id == selectedLampId } }

    init() {
        selectedLampId = UserDefaults.standard.string(forKey: "lampId")
        lampCycle = Set(UserDefaults.standard.stringArray(forKey: "lampCycle") ?? [])
        if let saved = HubCredentials.load() {
            hubHost = saved.host
            client = HubClient(host: saved.host, token: saved.token)
        }
    }

    func adopt(_ paired: HubClient) {
        guard let token = paired.token else { return }
        HubCredentials.save(host: paired.host, token: token)
        client = paired
        hubHost = paired.host
        Task { await refreshLights() }
    }

    func refreshLights() async {
        guard let client else { return }
        do {
            lights = try await client.lights()
            lightsError = nil
            if selectedLampId == nil { selectedLampId = lights.first?.id }
        } catch {
            lightsError = "Can't reach the hub. Is this iPhone on your home WiFi?"
        }
    }

    func choose(_ lamp: HubLight, link: KnobLink) {
        selectedLampId = lamp.id
        var config = HomeConfig()
        config.lampId = lamp.id
        link.sendHomeConfig(config)  // Lamp only: applied live, no restart.
    }

    // MARK: - Live control of the chosen lamp

    func setOn(_ on: Bool) {
        apply { $0.isOn = on }
        send(["isOn": on], key: "power")
    }

    func setLevel(_ level: Int) {
        let clamped = max(1, min(100, level))
        let wasOff = selectedLamp?.isOn == false
        apply { $0.level = clamped; $0.isOn = true }
        // Power goes on its own PATCH: the hub rejects a level sent with isOn.
        if wasOff { send(["isOn": true], key: "power") }
        send(["lightLevel": clamped], key: "level")
    }

    func setHue(_ hue: Double) {
        let saturation = max(0.65, selectedLamp?.saturation ?? 0.65)
        apply { $0.hue = hue; $0.saturation = saturation }
        send(["colorHue": hue, "colorSaturation": saturation], key: "colour")
    }

    func setInCycle(_ lamp: HubLight, _ included: Bool) {
        if included { lampCycle.insert(lamp.id) } else { lampCycle.remove(lamp.id) }
        var config = HomeConfig()
        // Kept in the lamp list's order, so the gesture steps predictably.
        config.lampCycle = lights.map(\.id).filter { lampCycle.contains($0) }
        link?.sendHomeConfig(config)
    }

    /// Optimistic: the control moves at once, the hub catches up - and the
    /// knob is told, so its next twist starts from the new value.
    private func apply(_ change: (inout HubLight) -> Void) {
        guard let index = lights.firstIndex(where: { $0.id == selectedLampId }) else { return }
        change(&lights[index])
        link?.pushLampState(lights[index])
    }

    /// Coalesces a dragged slider into at most one request per 120 ms per
    /// attribute - the newest value wins, the hub is never flooded.
    private var pending: [String: [String: Any]] = [:]
    private var flushing: Set<String> = []

    private func send(_ attributes: [String: Any], key: String) {
        guard let client, let id = selectedLampId else { return }
        pending[key] = attributes
        guard !flushing.contains(key) else { return }
        flushing.insert(key)
        Task {
            while let next = pending.removeValue(forKey: key) {
                try? await client.patch(light: id, attributes: next)
                try? await Task.sleep(for: .milliseconds(120))
            }
            flushing.remove(key)
        }
    }
}
