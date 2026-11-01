import CryptoKit
import Foundation
import Network

/// PKCE (RFC 7636) for the hub's pairing, as tools/pair.py does it.
enum PKCE {
    static func makeVerifier() -> String {
        let alphabet = Array("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-._~")
        return String((0..<128).map { _ in alphabet.randomElement()! })
    }

    static func challenge(for verifier: String) -> String {
        Data(SHA256.hash(data: Data(verifier.utf8))).base64EncodedString()
            .replacingOccurrences(of: "+", with: "-")
            .replacingOccurrences(of: "/", with: "_")
            .replacingOccurrences(of: "=", with: "")
    }
}

enum PEM {
    static func certificate(_ der: Data) -> String {
        let b64 = der.base64EncodedString()
        var lines: [String] = []
        var i = b64.startIndex
        while i < b64.endIndex {
            let j = b64.index(i, offsetBy: 64, limitedBy: b64.endIndex) ?? b64.endIndex
            lines.append(String(b64[i..<j]))
            i = j
        }
        return "-----BEGIN CERTIFICATE-----\n" + lines.joined(separator: "\n") + "\n-----END CERTIFICATE-----\n"
    }
}

/// WiFi, hub and lamp for the knob, in the knob's record format
/// (knob_protocol.h, HomeConfig). Only fields that are set are sent.
struct HomeConfig {
    var ssid: String?
    var password: String?
    var hubHost: String?
    var hubToken: String?
    var hubCertPEM: String?
    var lampId: String?
    /// Lamps the next-lamp gesture steps through.
    var lampCycle: [String]?

    var encoded: Data {
        var out = Data()
        let cycle = lampCycle.map { $0.joined(separator: ",") }
        for (key, value) in [(1, ssid), (2, password), (3, hubHost), (4, hubToken), (5, hubCertPEM),
                             (6, lampId), (7, cycle)] {
            guard let value else { continue }
            let bytes = Data(value.utf8)
            out.append(UInt8(key))
            out.append(UInt8(bytes.count & 0xFF))
            out.append(UInt8(bytes.count >> 8))
            out.append(bytes)
        }
        return out
    }
}

struct HubLight: Identifiable, Equatable {
    let id: String
    var name: String
    var isOn: Bool
    var level: Int        // 1...100
    var hue: Double       // 0...360
    var saturation: Double
    let supportsColour: Bool

    static func parse(_ json: Data) throws -> [HubLight] {
        struct Device: Decodable {
            let id: String
            let type: String
            let attributes: Attributes
        }
        struct Attributes: Decodable {
            let customName: String?
            let isOn: Bool?
            let lightLevel: Int?
            let colorHue: Double?
            let colorSaturation: Double?
        }
        return try JSONDecoder().decode([Device].self, from: json)
            .filter { $0.type == "light" }
            .map { d in
                HubLight(id: d.id, name: d.attributes.customName ?? "Light",
                         isOn: d.attributes.isOn ?? false, level: d.attributes.lightLevel ?? 100,
                         hue: d.attributes.colorHue ?? 0, saturation: d.attributes.colorSaturation ?? 0,
                         supportsColour: d.attributes.colorHue != nil)
            }
    }
}

/// Talks to the IKEA DIRIGERA hub on the local network.
///
/// The hub's certificate is self-signed, so it cannot be checked against a
/// public CA. On first contact (pairing) it is accepted and its issuer chain
/// is kept - that chain is what the knob pins afterwards (see pair.py).
final class HubClient: NSObject, URLSessionDelegate, @unchecked Sendable {
    let host: String
    var token: String?
    private(set) var issuerChainPEM: String?

    private lazy var session = URLSession(configuration: .ephemeral, delegate: self, delegateQueue: nil)
    private var base: String { "https://\(host):8443/v1" }

    init(host: String, token: String? = nil) {
        self.host = host
        self.token = token
    }

    enum HubError: Error { case badResponse(Int), noCode }

    func authorize(challenge: String) async throws -> String {
        var parts = URLComponents(string: "\(base)/oauth/authorize")!
        parts.queryItems = [
            URLQueryItem(name: "audience", value: "homesmart.local"),
            URLQueryItem(name: "response_type", value: "code"),
            URLQueryItem(name: "code_challenge", value: challenge),
            URLQueryItem(name: "code_challenge_method", value: "S256"),
        ]
        let (data, response) = try await session.data(from: parts.url!)
        try check(response)
        guard let code = (try JSONSerialization.jsonObject(with: data) as? [String: Any])?["code"] as? String else {
            throw HubError.noCode
        }
        return code
    }

    /// The token, or nil while the hub's action button has not been pressed.
    func exchange(code: String, verifier: String) async throws -> String? {
        var request = URLRequest(url: URL(string: "\(base)/oauth/token")!)
        request.httpMethod = "POST"
        request.setValue("application/x-www-form-urlencoded", forHTTPHeaderField: "Content-Type")
        let form = ["code": code, "code_verifier": verifier, "name": "Fidget Knob",
                    "grant_type": "authorization_code"]
        request.httpBody = form.map { "\($0.key)=\($0.value.addingPercentEncoding(withAllowedCharacters: .urlQueryAllowed) ?? $0.value)" }
            .joined(separator: "&").data(using: .utf8)
        let (data, response) = try await session.data(for: request)
        if (response as? HTTPURLResponse)?.statusCode == 403 { return nil }
        try check(response)
        return (try JSONSerialization.jsonObject(with: data) as? [String: Any])?["access_token"] as? String
    }

    func lights() async throws -> [HubLight] {
        let (data, response) = try await session.data(for: authed("\(base)/devices"))
        try check(response)
        return try HubLight.parse(data)
    }

    func patch(light id: String, attributes: [String: Any]) async throws {
        var request = authed("\(base)/devices/\(id)")
        request.httpMethod = "PATCH"
        request.setValue("application/json", forHTTPHeaderField: "Content-Type")
        request.httpBody = try JSONSerialization.data(withJSONObject: [["attributes": attributes]])
        let (_, response) = try await session.data(for: request)
        try check(response)
    }

    private func authed(_ url: String) -> URLRequest {
        var r = URLRequest(url: URL(string: url)!)
        r.timeoutInterval = 6
        if let token { r.setValue("Bearer \(token)", forHTTPHeaderField: "Authorization") }
        return r
    }

    private func check(_ response: URLResponse) throws {
        let code = (response as? HTTPURLResponse)?.statusCode ?? 0
        guard (200..<300).contains(code) else { throw HubError.badResponse(code) }
    }

    func urlSession(_ session: URLSession, didReceive challenge: URLAuthenticationChallenge,
                    completionHandler: @escaping (URLSession.AuthChallengeDisposition, URLCredential?) -> Void) {
        guard challenge.protectionSpace.authenticationMethod == NSURLAuthenticationMethodServerTrust,
              challenge.protectionSpace.host == host,
              let trust = challenge.protectionSpace.serverTrust else {
            completionHandler(.performDefaultHandling, nil)
            return
        }
        // Keep everything above the hub's own leaf: the knob pins that chain.
        let chain = (SecTrustCopyCertificateChain(trust) as? [SecCertificate]) ?? []
        if chain.count >= 2 {
            issuerChainPEM = chain.dropFirst().map { PEM.certificate(SecCertificateCopyData($0) as Data) }.joined()
        }
        completionHandler(.useCredential, URLCredential(trust: trust))
    }
}

/// Finds DIRIGERA hubs on the local network (they advertise _ihsp._tcp).
@MainActor
final class HubFinder: ObservableObject {
    @Published private(set) var hosts: [String] = []
    private var browser: NWBrowser?
    private var resolving: [NWConnection] = []

    func start() {
        let browser = NWBrowser(for: .bonjour(type: "_ihsp._tcp", domain: nil), using: .tcp)
        browser.browseResultsChangedHandler = { [weak self] results, _ in
            Task { @MainActor in results.forEach { self?.resolve($0.endpoint) } }
        }
        browser.start(queue: .main)
        self.browser = browser
    }

    func stop() {
        browser?.cancel()
        resolving.forEach { $0.cancel() }
        resolving.removeAll()
    }

    /// The knob connects by IP, so each service is resolved to an address.
    private func resolve(_ endpoint: NWEndpoint) {
        let params = NWParameters.tcp
        if let ip = params.defaultProtocolStack.internetProtocol as? NWProtocolIP.Options {
            ip.version = .v4
        }
        let connection = NWConnection(to: endpoint, using: params)
        connection.stateUpdateHandler = { [weak self, weak connection] state in
            guard case .ready = state, let connection else { return }
            if case .hostPort(let host, _) = connection.currentPath?.remoteEndpoint {
                var address = "\(host)"
                if let percent = address.firstIndex(of: "%") { address = String(address[..<percent]) }
                Task { @MainActor in
                    if let self, !self.hosts.contains(address) { self.hosts.append(address) }
                }
            }
            connection.cancel()
        }
        connection.start(queue: .main)
        resolving.append(connection)
    }
}

/// The hub's login, kept in the Keychain.
enum HubCredentials {
    private static let service = "com.urbanradar.fidgetknob.hub"

    static func save(host: String, token: String) {
        UserDefaults.standard.set(host, forKey: "hubHost")
        let query: [String: Any] = [kSecClass as String: kSecClassGenericPassword,
                                    kSecAttrService as String: service]
        SecItemDelete(query as CFDictionary)
        var add = query
        add[kSecValueData as String] = Data(token.utf8)
        SecItemAdd(add as CFDictionary, nil)
    }

    static func load() -> (host: String, token: String)? {
        guard let host = UserDefaults.standard.string(forKey: "hubHost") else { return nil }
        let query: [String: Any] = [kSecClass as String: kSecClassGenericPassword,
                                    kSecAttrService as String: service,
                                    kSecReturnData as String: true]
        var item: CFTypeRef?
        guard SecItemCopyMatching(query as CFDictionary, &item) == errSecSuccess,
              let data = item as? Data, let token = String(data: data, encoding: .utf8) else { return nil }
        return (host, token)
    }
}
