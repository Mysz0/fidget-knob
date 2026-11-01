import Foundation

/// Settings stored on the knob. Byte layout must match KnobSettings in
/// `firmware/lib/knob_music/knob_protocol.h` (version 1).
struct KnobSettings: Equatable {
    enum MusicAction: UInt8, CaseIterable, Identifiable {
        case none = 0, playPause = 1, next = 2, previous = 3
        var id: UInt8 { rawValue }
        var title: String {
            switch self {
            case .none: return "Nothing"
            case .playPause: return "Play / Pause"
            case .next: return "Next track"
            case .previous: return "Previous track"
            }
        }
    }

    enum LightsAction: UInt8, CaseIterable, Identifiable {
        case none = 0, toggle = 1, colourMode = 2, nextLight = 3
        var id: UInt8 { rawValue }
        var title: String {
            switch self {
            case .none: return "Nothing"
            case .toggle: return "Lamp on / off"
            case .colourMode: return "Brightness / colour"
            case .nextLight: return "Next lamp"
            }
        }
    }

    enum MusicTwist: UInt8, CaseIterable, Identifiable {
        case none = 0, volume = 1, tracks = 2
        var id: UInt8 { rawValue }
        var title: String {
            switch self {
            case .none: "Nothing"
            case .volume: "Volume"
            case .tracks: "Skip tracks"
            }
        }
    }

    enum LightsTwist: UInt8, CaseIterable, Identifiable {
        // 2 was "always colour"; it left shake nothing to do and now reads
        // as brightness.
        case none = 0, brightness = 1, lamps = 3
        var id: UInt8 { rawValue }
        var title: String {
            switch self {
            case .none: "Nothing"
            case .brightness: "Brightness / colour"
            case .lamps: "Switch lamps"
            }
        }
    }

    enum Sensitivity: UInt8, CaseIterable, Identifiable {
        case low = 0, normal = 1, high = 2
        var id: UInt8 { rawValue }
        var title: String {
            switch self {
            case .low: "Low"
            case .normal: "Normal"
            case .high: "High"
            }
        }
    }

    var screenOffSeconds: UInt8 = 12
    var sleepMinutes: UInt8 = 2
    var musicTap: MusicAction = .playPause
    var musicShake: MusicAction = .next
    var lightsTap: LightsAction = .toggle
    var lightsShake: LightsAction = .colourMode
    var overlayOn = true
    var overlaySeconds: UInt8 = 3
    /// A fast twist back (re-gripping the knob) does not count.
    var ratchet = true
    /// Music: the mosaic cover turns like a record on the knob.
    var spinCover = false
    /// The look: sprite pack and palette (indices into SpriteArt), effect.
    var spritePack: UInt8 = 0
    var palette: UInt8 = 0
    var effect: KnobLook.Effect = .still
    var musicTwist: MusicTwist = .volume
    var lightsTwist: LightsTwist = .brightness
    var twistSensitivity: Sensitivity = .normal
    /// Clockwise and anticlockwise swap.
    var twistReverse = false

    /// The custom palette (palette == customPalette), as RGB.
    var customWhite: [UInt8] = [255, 214, 160]
    var customAccent: [UInt8] = [46, 168, 84]
    static let customPalette: UInt8 = 0xFF
    /// Seconds still before the screen dims; 0 = never.
    var dimAfterSeconds: UInt8 = 6
    /// How bright when dimmed, percent.
    var dimPercent: UInt8 = 16
    /// How bright normally, percent.
    var brightness: UInt8 = 100
    /// Also a standard Bluetooth media remote, for Android and other phones.
    var mediaRemote = false

    var look: KnobLook {
        KnobLook(pack: Int(spritePack), palette: Int(palette), effect: effect,
                 customWhite: customWhite, customAccent: customAccent)
    }

    init() {}

    init?(data: Data) {
        let b = [UInt8](data)
        guard b.count >= 9, b[0] == 1 else { return nil }
        screenOffSeconds = b[1]
        sleepMinutes = b[2]
        musicTap = MusicAction(rawValue: b[3]) ?? .none
        musicShake = MusicAction(rawValue: b[4]) ?? .none
        lightsTap = LightsAction(rawValue: b[5]) ?? .none
        lightsShake = LightsAction(rawValue: b[6]) ?? .none
        overlayOn = b[7] != 0
        overlaySeconds = b[8]
        ratchet = b.count >= 10 ? b[9] != 0 : true
        spinCover = b.count >= 11 ? b[10] != 0 : false
        spritePack = b.count >= 12 ? b[11] : 0
        palette = b.count >= 13 ? b[12] : 0
        effect = b.count >= 14 ? KnobLook.Effect(rawValue: b[13]) ?? .still : .still
        musicTwist = b.count >= 15 ? MusicTwist(rawValue: b[14]) ?? .volume : .volume
        lightsTwist = b.count >= 16 ? LightsTwist(rawValue: b[15]) ?? .brightness : .brightness
        twistSensitivity = b.count >= 17 ? Sensitivity(rawValue: b[16]) ?? .normal : .normal
        twistReverse = b.count >= 18 ? b[17] != 0 : false
        if b.count >= 24 {
            customWhite = Array(b[18..<21])
            customAccent = Array(b[21..<24])
        }
        dimAfterSeconds = b.count >= 25 ? b[24] : screenOffSeconds / 2
        dimPercent = b.count >= 26 ? b[25] : 16
        brightness = b.count >= 27 ? b[26] : 100
        mediaRemote = b.count >= 28 ? b[27] != 0 : false
    }

    var encoded: Data {
        Data([1, screenOffSeconds, sleepMinutes, musicTap.rawValue, musicShake.rawValue,
              lightsTap.rawValue, lightsShake.rawValue, overlayOn ? 1 : 0, overlaySeconds,
              ratchet ? 1 : 0, spinCover ? 1 : 0, spritePack, palette, effect.rawValue,
              musicTwist.rawValue, lightsTwist.rawValue, twistSensitivity.rawValue, twistReverse ? 1 : 0]
             + customWhite + customAccent + [dimAfterSeconds, dimPercent, brightness, mediaRemote ? 1 : 0])
    }
}
