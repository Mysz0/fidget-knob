import XCTest
@testable import FidgetKnob

final class CoverSearchTests: XCTestCase {
    private let json = """
    {"resultCount":3,"results":[
      {"artistName":"Various Artists","collectionName":"Remember That You Will Die (Karaoke)","artworkUrl100":"https://x/a/100x100bb.jpg"},
      {"artistName":"Polyphia","collectionName":"Remember That You Will Die","artworkUrl100":"https://x/b/100x100bb.jpg"},
      {"artistName":"Polyphia","collectionName":"New Levels New Devils","artworkUrl100":"https://x/c/100x100bb.jpg"}
    ]}
    """.data(using: .utf8)!

    func testPicksExactArtistAndAlbum() throws {
        let np = NowPlaying(artist: "Polyphia", album: "Remember That You Will Die", title: "Playing God", playing: true)
        let url = try CoverSearch.bestArtworkURL(in: json, for: np)
        XCTAssertEqual(url?.absoluteString, "https://x/b/600x600bb.jpg")
    }

    func testIgnoresCaseAndFeaturing() throws {
        let np = NowPlaying(artist: "polyphia", album: "Remember that you will die - Single", title: "x", playing: true)
        XCTAssertEqual(try CoverSearch.bestArtworkURL(in: json, for: np)?.absoluteString, "https://x/b/600x600bb.jpg")
    }

    func testNoMatchingArtistGivesNil() throws {
        let np = NowPlaying(artist: "Someone Else", album: "Unknown", title: "x", playing: true)
        XCTAssertNil(try CoverSearch.bestArtworkURL(in: json, for: np))
    }

    func testSearchUsesArtistAndTitle() {
        let np = NowPlaying(artist: "Polyphia", album: "Remember That You Will Die", title: "Playing God", playing: true)
        let term = URLComponents(url: CoverSearch.searchURL(for: np)!, resolvingAgainstBaseURL: false)?
            .queryItems?.first { $0.name == "term" }?.value
        XCTAssertEqual(term, "Polyphia Playing God")
    }

    func testFeaturedArtistsAndTitlesMatch() throws {
        // What the phone reports for a featured track.
        let np = NowPlaying(artist: "Polyphia & Sophia Black", album: "Remember That You Will Die",
                            title: "ABC (feat. Sophia Black)", playing: true)
        XCTAssertEqual(try CoverSearch.bestArtworkURL(in: json, for: np)?.absoluteString, "https://x/b/600x600bb.jpg")
        let term = URLComponents(url: CoverSearch.searchURL(for: np)!, resolvingAgainstBaseURL: false)?
            .queryItems?.first { $0.name == "term" }?.value
        XCTAssertEqual(term, "Polyphia ABC")
    }

    func testSameSongBeatsSameAlbum() throws {
        let tracks = """
        {"results":[
          {"artistName":"Polyphia","collectionName":"Other","trackName":"Genesis (feat. Brasstracks)","artworkUrl100":"https://x/song/100x100bb.jpg"},
          {"artistName":"Polyphia","collectionName":"Remember That You Will Die","trackName":"Playing God","artworkUrl100":"https://x/album/100x100bb.jpg"}
        ]}
        """.data(using: .utf8)!
        let np = NowPlaying(artist: "Polyphia", album: "", title: "Genesis (feat. Brasstracks)", playing: true)
        XCTAssertEqual(try CoverSearch.bestArtworkURL(in: tracks, for: np)?.absoluteString, "https://x/song/600x600bb.jpg")
    }

    func testSearchURLIsEncoded() {
        let np = NowPlaying(artist: "Żółć & Co", album: "A/B", title: "x", playing: true)
        let url = CoverSearch.searchURL(for: np)!
        XCTAssertEqual(url.host, "itunes.apple.com")
        // Album search misses real albums ("Polyphia Remember That You Will
        // Die" returns nothing); a song search by artist + title finds them.
        XCTAssertTrue(url.absoluteString.contains("entity=song"))
        XCTAssertFalse(url.absoluteString.contains(" "))
    }
}
