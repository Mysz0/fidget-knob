import CoreBluetooth
import Foundation
import os

private let log = Logger(subsystem: "com.urbanradar.fidgetknob", category: "link")

/// The Bluetooth link to the knob.
///
/// The iPhone is usually already connected to the knob (it is paired for the
/// music controls), so the link first asks the system for that connection and
/// only scans when there is none. It remembers the knob, keeps a pending
/// connection open so it reconnects by itself, and restores across launches.
@MainActor
final class KnobLink: NSObject, ObservableObject {
    enum Status: Equatable {
        case bluetoothOff
        case unauthorized
        case searching
        case connecting
        case connected
    }

    enum Mode: UInt8, CaseIterable, Identifiable {
        case lights = 0
        case music = 1
        case timer = 2
        case clock = 3
        case dice = 4
        case ambient = 5
        case photo = 6
        case notify = 7
        case games = 8

        var id: UInt8 { rawValue }
        var title: String {
            switch self {
            case .lights: "Lights"
            case .music: "Music"
            case .timer: "Timer"
            case .clock: "Clock"
            case .dice: "Dice"
            case .ambient: "Ambient"
            case .photo: "Photos"
            case .notify: "Notifications"
            case .games: "Games"
            }
        }
        var subtitle: String {
            switch self {
            case .lights: "Your lamp, through the IKEA hub"
            case .music: "Whatever your iPhone is playing"
            case .timer: "A countdown, set by turning the knob"
            case .clock: "The time, in sprites"
            case .dice: "Roll it, flip it, let it decide"
            case .ambient: "Just the sprites, for the look"
            case .photo: "Your pictures on the knob"
            case .notify: "Your iPhone's notifications"
            case .games: "Tilt and Memory, steered by tilting"
            }
        }
        var icon: String {
            switch self {
            case .lights: "lightbulb"
            case .music: "music.note"
            case .timer: "timer"
            case .clock: "clock"
            case .dice: "dice"
            case .ambient: "sparkles"
            case .photo: "photo.on.rectangle"
            case .notify: "bell"
            case .games: "gamecontroller"
            }
        }
    }

    enum CoverStyle: UInt8 {
        case mosaic = 0
        case photo = 1
    }

    @Published private(set) var status: Status = .searching
    @Published private(set) var mode: Mode?
    @Published private(set) var coverStyle: CoverStyle = .mosaic
    @Published private(set) var nowPlaying: NowPlaying?
    /// Bumped on every now-playing message, even one equal to the last: the
    /// knob announcing a track is the cue to send its cover.
    @Published private(set) var nowPlayingSerial = 0
    /// The knob's stored settings; nil until read after connecting.
    @Published private(set) var settings: KnobSettings?
    /// WiFi / hub status the knob reports.
    @Published private(set) var knobStatus: KnobStatus?
    /// Networks from the knob's last WiFi scan; nil while scanning.
    @Published private(set) var networks: [WifiNetwork]? = []
    /// The knob's timer, while it is in Timer mode.
    @Published private(set) var timer: TimerState?
    /// The dice, while the knob is in Dice mode.
    @Published private(set) var dice: DiceState?
    /// The photos, while the knob is in Photos mode.
    @Published private(set) var photos: PhotoState?
    /// Image frames still to go out (a photo upload in progress).
    @Published private(set) var framesLeft = 0
    /// Frames in the transfer under way, for a progress bar.
    @Published private(set) var framesTotal = 0
    /// True while the knob restarts into a new mode.
    @Published private(set) var switchingMode = false

    private var central: CBCentralManager?
    private var knob: CBPeripheral?
    private var characteristics: [CBUUID: CBCharacteristic] = [:]
    private var pendingFrames: [Data] = []
    private var identityWatch: Task<Void, Never>?

    private static let savedKnobKey = "knobIdentifier"

    /// Creates the Bluetooth manager - which is what makes iOS ask for
    /// permission - so onboarding decides when that happens.
    func start() {
        guard central == nil else { return }
        central = CBCentralManager(delegate: self, queue: .main, options: [
            CBCentralManagerOptionRestoreIdentifierKey: "fidget-knob-central",
        ])
    }


    // MARK: - Commands

    func setMode(_ mode: Mode) {
        guard mode != self.mode, write(Data([mode.rawValue]), to: KnobProtocol.mode, withResponse: true) else { return }
        // The knob switches in place and confirms with a mode notification.
        // Older firmware restarts instead; the reconnect clears it then.
        switchingMode = true
        self.mode = mode
        Task { [weak self] in
            try? await Task.sleep(for: .seconds(20))
            self?.switchingMode = false
        }
    }

    func setCoverStyle(_ style: CoverStyle) {
        guard write(Data([style.rawValue]), to: KnobProtocol.coverStyle, withResponse: true) else { return }
        coverStyle = style
    }

    /// Asks the knob to look for WiFi networks; results arrive in `networks`.
    func scanWifi() {
        guard write(KnobCommands.scanWifi, to: KnobProtocol.command, withResponse: true) else { return }
        networks = nil
    }

    /// Tells the knob a lamp changed through the hub, so it does not show a
    /// stale level next time it is twisted.
    func pushLampState(_ lamp: HubLight) {
        write(KnobCommands.lampState(lamp), to: KnobProtocol.lampState, withResponse: false)
    }

    /// Timer, dice and photo commands; ignored by the knob in other modes.
    func send(_ command: Data) {
        write(command, to: KnobProtocol.command, withResponse: true)
    }

    func sendTime() {
        let now = Date()
        let offset = Int16(TimeZone.current.secondsFromGMT(for: now) / 60)
        write(KnobCommands.setTime(unix: UInt32(now.timeIntervalSince1970), utcOffsetMinutes: offset),
              to: KnobProtocol.command, withResponse: true)
    }

    func updateSettings(_ new: KnobSettings) {
        guard write(new.encoded, to: KnobProtocol.settings, withResponse: true) else { return }
        settings = new
    }

    /// Sends WiFi / hub / lamp setup. Anything but a lamp-only change makes
    /// the knob restart onto the new network.
    func sendHomeConfig(_ config: HomeConfig) {
        if config.ssid != nil || config.hubHost != nil || config.hubToken != nil {
            switchingMode = true  // Shown as "Restarting..." until it is back.
        }
        sendArtwork(kind: .homeConfig, payload: config.encoded, replacingQueued: false)
    }

    /// Queues an image for the knob. A newer image replaces one still sending.
    func sendArtwork(kind: KnobProtocol.ArtworkKind, payload: Data, replacingQueued: Bool) {
        guard let knob, status == .connected else { return }
        let maxFrame = min(knob.maximumWriteValueLength(for: .withoutResponse), 244)
        let frames = KnobProtocol.artworkFrames(kind: kind, payload: payload, maxFrame: maxFrame)
        if replacingQueued { pendingFrames.removeAll() }
        framesTotal = pendingFrames.isEmpty ? frames.count : framesTotal + frames.count
        pendingFrames.append(contentsOf: frames)
        framesLeft = pendingFrames.count
        pumpFrames()
    }

    // MARK: - Internals

    @discardableResult
    private func write(_ data: Data, to uuid: CBUUID, withResponse: Bool) -> Bool {
        guard let knob, let c = characteristics[uuid] else { return false }
        knob.writeValue(data, for: c, type: withResponse ? .withResponse : .withoutResponse)
        return true
    }

    private func pumpFrames() {
        guard let knob, let c = characteristics[KnobProtocol.artwork] else { return }
        while !pendingFrames.isEmpty && knob.canSendWriteWithoutResponse {
            knob.writeValue(pendingFrames.removeFirst(), for: c, type: .withoutResponse)
        }
        if framesLeft != pendingFrames.count { framesLeft = pendingFrames.count }
    }

    private func find() {
        guard let central, central.state == .poweredOn else { return }
        if let connected = central.retrieveConnectedPeripherals(withServices: [KnobProtocol.service]).first {
            attach(connected)
        } else if let saved = UserDefaults.standard.string(forKey: Self.savedKnobKey),
                  let id = UUID(uuidString: saved),
                  let known = central.retrievePeripherals(withIdentifiers: [id]).first {
            attach(known)
        } else {
            status = .searching
            central.scanForPeripherals(withServices: [KnobProtocol.service])
        }
    }

    private func attach(_ peripheral: CBPeripheral) {
        guard let central else { return }
        central.stopScan()
        if let old = knob, old.identifier != peripheral.identifier {
            // A knob the phone forgot and paired again can come back under a
            // new identity; the pending connect to the old one would wait forever.
            central.cancelPeripheralConnection(old)
        }
        knob = peripheral
        peripheral.delegate = self
        UserDefaults.standard.set(peripheral.identifier.uuidString, forKey: Self.savedKnobKey)
        status = .connecting
        // A pending connect never times out: iOS completes it whenever the
        // knob is next in range and awake, even with the app in the background.
        central.connect(peripheral)
        watchForNewIdentity()
    }

    /// Still not connected after a while: also scan, and take whichever knob
    /// answers - the same one under a new identity, or after a re-pair.
    private func watchForNewIdentity() {
        identityWatch?.cancel()
        identityWatch = Task { [weak self] in
            try? await Task.sleep(for: .seconds(12))
            guard let self, !Task.isCancelled, self.status != .connected,
                  let central = self.central, central.state == .poweredOn else { return }
            central.scanForPeripherals(withServices: [KnobProtocol.service])
        }
    }

    /// Turns the knob off; a firm shake brings it back.
    func powerOff() {
        send(KnobCommands.powerOff)
    }
}

extension KnobLink: CBCentralManagerDelegate {
    nonisolated func centralManagerDidUpdateState(_ central: CBCentralManager) {
        MainActor.assumeIsolated {
            switch central.state {
            case .poweredOn: find()
            case .unauthorized: status = .unauthorized
            default: status = .bluetoothOff
            }
        }
    }

    nonisolated func centralManager(_ central: CBCentralManager, willRestoreState dict: [String: Any]) {
        MainActor.assumeIsolated {
            if let restored = (dict[CBCentralManagerRestoredStatePeripheralsKey] as? [CBPeripheral])?.first {
                knob = restored
                restored.delegate = self
            }
        }
    }

    nonisolated func centralManager(_ central: CBCentralManager, didDiscover peripheral: CBPeripheral,
                                    advertisementData: [String: Any], rssi RSSI: NSNumber) {
        MainActor.assumeIsolated {
            // The knob we are already waiting on: just stop scanning.
            if peripheral.identifier == knob?.identifier {
                central.stopScan()
                return
            }
            attach(peripheral)
        }
    }

    nonisolated func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        MainActor.assumeIsolated {
            peripheral.discoverServices([KnobProtocol.service])
        }
    }

    nonisolated func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral, error: Error?) {
        MainActor.assumeIsolated {
            status = .connecting
            central.connect(peripheral)
        }
    }

    nonisolated func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral, error: Error?) {
        MainActor.assumeIsolated {
            characteristics.removeAll()
            pendingFrames.removeAll()
            status = .connecting
            central.connect(peripheral)  // Back as soon as it wakes or returns.
        }
    }
}

extension KnobLink: CBPeripheralDelegate {
    nonisolated func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        MainActor.assumeIsolated {
            guard let service = peripheral.services?.first(where: { $0.uuid == KnobProtocol.service }) else { return }
            peripheral.discoverCharacteristics(nil, for: service)
        }
    }

    nonisolated func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService, error: Error?) {
        MainActor.assumeIsolated {
            for c in service.characteristics ?? [] { characteristics[c.uuid] = c }
            if let np = characteristics[KnobProtocol.nowPlaying] {
                peripheral.setNotifyValue(true, for: np)
                peripheral.readValue(for: np)
            }
            if let m = characteristics[KnobProtocol.mode] {
                // The knob switches modes without restarting and says so here.
                peripheral.setNotifyValue(true, for: m)
                peripheral.readValue(for: m)
            }
            if let s = characteristics[KnobProtocol.coverStyle] { peripheral.readValue(for: s) }
            if let s = characteristics[KnobProtocol.settings] {
                // The knob changes its look itself in Ambient mode.
                peripheral.setNotifyValue(true, for: s)
                peripheral.readValue(for: s)
            }
            for uuid in [KnobProtocol.status, KnobProtocol.scan, KnobProtocol.modeState] {
                if let c = characteristics[uuid] {
                    peripheral.setNotifyValue(true, for: c)
                    peripheral.readValue(for: c)
                }
            }
            status = .connected
            switchingMode = false
            sendTime()  // The knob's clock follows the phone.
        }
    }

    nonisolated func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic, error: Error?) {
        MainActor.assumeIsolated {
            guard let value = characteristic.value else { return }
            switch characteristic.uuid {
            case KnobProtocol.mode:
                if let byte = value.first { mode = Mode(rawValue: byte) }
                switchingMode = false
                if mode != .timer { timer = nil }
                if mode != .dice { dice = nil }
                if mode != .photo { photos = nil }
            case KnobProtocol.coverStyle:
                if let byte = value.first, let style = CoverStyle(rawValue: byte) { coverStyle = style }
            case KnobProtocol.nowPlaying:
                nowPlaying = KnobProtocol.parseNowPlaying(value)
                nowPlayingSerial += 1
            case KnobProtocol.settings:
                settings = KnobSettings(data: value)
            case KnobProtocol.status:
                knobStatus = KnobStatus(data: value)
            case KnobProtocol.scan:
                networks = WifiNetwork.parse(value)
            case KnobProtocol.modeState:
                switch value.first {
                case 2: timer = TimerState(data: value)
                case 4: dice = DiceState(data: value)
                case 6: photos = PhotoState(data: value)
                default: break
                }
            default:
                break
            }
        }
    }

    nonisolated func peripheral(_ peripheral: CBPeripheral, didWriteValueFor characteristic: CBCharacteristic, error: Error?) {
        let uuid = characteristic.uuid.uuidString
        if let error {
            log.error("write \(uuid, privacy: .public) failed: \(error.localizedDescription, privacy: .public)")
        } else {
        }
    }

    nonisolated func peripheral(_ peripheral: CBPeripheral, didUpdateNotificationStateFor characteristic: CBCharacteristic, error: Error?) {
    }

    nonisolated func peripheralIsReady(toSendWriteWithoutResponse peripheral: CBPeripheral) {
        MainActor.assumeIsolated { pumpFrames() }
    }
}
