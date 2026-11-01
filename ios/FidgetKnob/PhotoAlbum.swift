import SwiftUI

/// The app's copy of each photo slot on the knob - the knob keeps the real
/// thing in its flash but cannot send it back, so thumbnails live here.
/// Also runs uploads, start to finish, so the screen can show each step.
@MainActor
final class PhotoAlbum: ObservableObject {
    enum Upload: Equatable {
        case preparing(slot: Int)
        case sending(slot: Int, progress: Double)
        case saving(slot: Int)
        case saved(slot: Int)
        case failed(slot: Int, reason: String)

        var slot: Int {
            switch self {
            case .preparing(let s), .sending(let s, _), .saving(let s), .saved(let s), .failed(let s, _): s
            }
        }
        var inFlight: Bool {
            switch self {
            case .preparing, .sending, .saving: true
            default: false
            }
        }
    }

    @Published private(set) var images: [Int: UIImage] = [:]
    @Published private(set) var upload: Upload?

    /// The photo being sent, kept until the knob confirms it (or for a retry).
    private var pending: (slot: Int, image: UIImage)?
    private var savedBefore: UInt8?
    private var timeout: Task<Void, Never>?

    private let folder: URL = {
        let base = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
        let dir = base.appendingPathComponent("photos", isDirectory: true)
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        return dir
    }()

    init() {
        for slot in 0..<KnobProtocol.photoSlots {
            if let data = try? Data(contentsOf: file(slot)), let image = UIImage(data: data) {
                images[slot] = image
            }
        }
    }

    /// Crops to the round screen's square and sends both forms: the mosaic
    /// first, then the JPEG - the knob confirms once the JPEG is stored.
    func upload(_ picked: UIImage, to slot: Int, link: KnobLink) {
        guard link.status == .connected, link.mode == .photo else {
            fail(slot, "The knob isn't connected.")
            return
        }
        let square = CoverRender.square(picked)
        pending = (slot, square)
        // One byte goes to the slot number.
        guard let jpeg = CoverRender.jpeg(from: square, limit: KnobProtocol.maxPhotoBytes - 1) else {
            fail(slot, "That photo couldn't be made small enough.")
            return
        }
        savedBefore = link.photos?.saved
        upload = .sending(slot: slot, progress: 0)
        link.sendArtwork(kind: .photoMosaic, payload: Data([UInt8(slot)]) + CoverRender.mosaic(from: square),
                         replacingQueued: false)
        link.sendArtwork(kind: .photoJpeg, payload: Data([UInt8(slot)]) + jpeg, replacingQueued: false)
    }

    /// A photo was picked and is loading from the library (iCloud ones can
    /// take a moment): shown straight away so the tap is never silent.
    func preparing(_ slot: Int) { upload = .preparing(slot: slot) }

    func couldNotOpen(_ slot: Int) { fail(slot, "Couldn't open that photo.") }

    func retry(link: KnobLink) {
        guard let p = pending else { return }
        upload(p.image, to: p.slot, link: link)
    }

    /// Frames still to go out: the progress bar, then waiting for the knob.
    func framesChanged(left: Int, total: Int) {
        guard case .sending(let slot, _) = upload else { return }
        if left == 0 {
            upload = .saving(slot: slot)
            timeout?.cancel()
            timeout = Task { [weak self] in
                try? await Task.sleep(for: .seconds(6))
                guard let self, !Task.isCancelled, case .saving = self.upload else { return }
                self.fail(slot, "The knob didn't save it. Try again, closer to the knob.")
            }
        } else if total > 0 {
            upload = .sending(slot: slot, progress: 1 - Double(left) / Double(total))
        }
    }

    /// The knob reported its photos: an upload is done when its count moves.
    func knobChanged(_ state: PhotoState?) {
        guard let state, let up = upload, up.inFlight, let p = pending,
              let before = savedBefore, state.saved != before else { return }
        timeout?.cancel()
        images[p.slot] = p.image
        try? p.image.jpegData(compressionQuality: 0.85)?.write(to: file(p.slot))
        pending = nil
        upload = .saved(slot: p.slot)
        Task { [weak self] in
            try? await Task.sleep(for: .seconds(2))
            if case .saved = self?.upload { self?.upload = nil }
        }
    }

    func connectionChanged(connected: Bool) {
        if !connected, let up = upload, up.inFlight {
            fail(up.slot, "Lost the connection to the knob.")
        }
    }

    func dismissError() { if case .failed = upload { upload = nil } }

    func delete(_ slot: Int, link: KnobLink) {
        images[slot] = nil
        try? FileManager.default.removeItem(at: file(slot))
        link.send(KnobCommands.photoDelete(slot))
    }

    private func fail(_ slot: Int, _ reason: String) {
        timeout?.cancel()
        upload = .failed(slot: slot, reason: reason)
    }

    private func file(_ slot: Int) -> URL { folder.appendingPathComponent("slot\(slot).jpg") }
}
