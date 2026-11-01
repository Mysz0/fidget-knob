import Foundation

/// Finds album art in Apple's public catalogue (iTunes Search API - no login,
/// and it carries almost everything that is on Spotify and YouTube Music too).
enum CoverSearch {
    static func searchURL(for np: NowPlaying, byAlbum: Bool = false) -> URL? {
        var parts = URLComponents(string: "https://itunes.apple.com/search")
        // Searched by song, not album: Apple's album search misses real
        // albums outright ("Polyphia Remember That You Will Die" finds
        // nothing), while artist + track title lands on the track, which
        // carries its album's name and artwork. Only the main artist and the
        // bare title go in: "(feat. Sophia Black)" in the term made Apple
        // return everything except the song.
        let what = byAlbum ? cleanTitle(np.album) : cleanTitle(np.title)
        parts?.queryItems = [
            URLQueryItem(name: "term", value: "\(mainArtist(np.artist)) \(what)"),
            URLQueryItem(name: "entity", value: "song"),
            URLQueryItem(name: "limit", value: "10"),
        ]
        return parts?.url
    }

    /// The first named artist: "Polyphia & Sophia Black" -> "Polyphia".
    static func mainArtist(_ s: String) -> String {
        var t = s
        for cut in [" & ", ", ", " feat", " ft.", " x ", " X ", " with ", " and "] {
            if let r = t.range(of: cut, options: .caseInsensitive) { t = String(t[..<r.lowerBound]) }
        }
        return t.trimmingCharacters(in: .whitespacesAndNewlines)
    }

    /// The title without "(feat. ...)", "[with ...]" or " - Remastered".
    static func cleanTitle(_ s: String) -> String {
        var t = s
        for cut in [" (feat", " [feat", " feat.", " (with ", " [with ", " (ft", " - remaster", " - single", " - ep"] {
            if let r = t.range(of: cut, options: .caseInsensitive) { t = String(t[..<r.lowerBound]) }
        }
        return t.trimmingCharacters(in: .whitespacesAndNewlines)
    }

    /// The 600 px artwork of the best result by this artist: the same song
    /// first, then the same album. Nil when nothing is by this artist - a
    /// wrong cover is worse than none.
    static func bestArtworkURL(in json: Data, for np: NowPlaying) throws -> URL? {
        try bestArtwork(in: json, for: np)?.url
    }

    /// The best match and how good it is: 3+ is the same song or album, 1-2
    /// only the same artist.
    static func bestArtwork(in json: Data, for np: NowPlaying) throws -> (url: URL, score: Int)? {
        struct Response: Decodable { let results: [Track] }
        struct Track: Decodable {
            let artistName: String?
            let collectionName: String?
            let trackName: String?
            let artworkUrl100: String?
        }

        let tracks = try JSONDecoder().decode(Response.self, from: json).results
        let artist = normalise(mainArtist(np.artist))
        let album = normalise(np.album)
        let title = normalise(cleanTitle(np.title))

        let scored = tracks.compactMap { t -> (Int, String)? in
            guard let art = t.artworkUrl100, let name = t.artistName else { return nil }
            // The result's artist may be "Polyphia" while the phone says
            // "Polyphia & Sophia Black", or the other way round.
            let theirs = normalise(mainArtist(name))
            guard !artist.isEmpty, theirs == artist || theirs.contains(artist) || artist.contains(theirs) else {
                return nil
            }
            let collection = normalise(t.collectionName ?? "")
            var score = collection == album ? 3 : (!album.isEmpty && (collection.hasPrefix(album) || album.hasPrefix(collection))) ? 2 : 1
            if normalise(cleanTitle(t.trackName ?? "")) == title { score += 3 }
            return (score, art)
        }
        guard let best = scored.max(by: { $0.0 < $1.0 }),
              let url = URL(string: best.1.replacingOccurrences(of: "100x100bb", with: "600x600bb")) else { return nil }
        return (url, best.0)
    }

    /// Lower-case, without the decorations streaming services add to album
    /// names (" - Single", "(Deluxe)", "feat. ..."), so the same album matches.
    static func normalise(_ s: String) -> String {
        var t = s.lowercased()
        for cut in [" - single", " - ep", " (deluxe", " [deluxe", " (feat.", " feat.", " (remaster"] {
            if let r = t.range(of: cut) { t = String(t[..<r.lowerBound]) }
        }
        return t.trimmingCharacters(in: .whitespacesAndNewlines)
    }

    static func fetchCover(for np: NowPlaying) async throws -> Data? {
        // By song first; unless that found the song or its album, by album
        // too - Apple often answers "artist + title" with the artist's
        // other records, and a wrong cover is worse than a late one.
        var best: (url: URL, score: Int)?
        for byAlbum in np.album.isEmpty ? [false] : [false, true] {
            if let b = best, b.score >= 3 { break }
            guard let url = searchURL(for: np, byAlbum: byAlbum) else { continue }
            let (json, _) = try await URLSession.shared.data(from: url)
            if let found = try bestArtwork(in: json, for: np), found.score > (best?.score ?? 0) {
                best = found
            }
        }
        guard let art = best?.url else { return nil }
        let (image, _) = try await URLSession.shared.data(from: art)
        return image
    }
}
