import SwiftUI

/// Watches what the knob says is playing, finds the cover, and sends it.
@MainActor
final class CoverModel: ObservableObject {
    enum State: Equatable {
        case idle
        case looking
        case found
        case notFound
    }

    @Published private(set) var image: UIImage?
    @Published private(set) var state: State = .idle

    private var task: Task<Void, Never>?
    private var lastLookup: String?

    func update(for np: NowPlaying?, link: KnobLink) {
        guard let np, np.trackKey != lastLookup else { return }
        lastLookup = np.trackKey
        task?.cancel()
        image = nil
        state = .looking

        // The title first: it is small, and the knob shows it the moment the
        // track changes, while the cover can take a second to find.
        if link.settings?.overlayOn ?? true,
           let mask = TitleRender.mask(title: np.title, artist: np.artist) {
            link.sendArtwork(kind: .title, payload: mask, replacingQueued: true)
        }

        task = Task {
            do {
                guard let data = try await CoverSearch.fetchCover(for: np),
                      let cover = UIImage(data: data) else {
                    state = .notFound
                    return
                }
                try Task.checkCancellation()
                image = cover
                state = .found
                // Mosaic first: 1 KB, on screen almost at once. The photo
                // follows, so switching style later needs no re-fetch.
                link.sendArtwork(kind: .mosaic, payload: CoverRender.mosaic(from: cover), replacingQueued: false)
                if let jpeg = CoverRender.jpeg(from: cover) {
                    link.sendArtwork(kind: .jpeg, payload: jpeg, replacingQueued: false)
                }
            } catch is CancellationError {
            } catch {
                state = .notFound
            }
        }
    }

    /// The knob reconnected or came back to Music, and may have dropped its
    /// cover: the next track it announces gets the cover sent again. Not
    /// sent now - the knob clears its cover when it announces the track, so
    /// a cover sent before that would be thrown away.
    func forget() {
        lastLookup = nil
    }
}
