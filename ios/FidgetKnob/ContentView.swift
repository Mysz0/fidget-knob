import SwiftUI

struct ContentView: View {
    @AppStorage("hasCompletedOnboarding") private var hasCompletedOnboarding = false
    @StateObject private var link = KnobLink()
    @StateObject private var cover = CoverModel()
    @StateObject private var home = HomeModel()
    @StateObject private var timerAlarm = TimerAlarm()
    @StateObject private var album = PhotoAlbum()

    var body: some View {
        Group {
            if hasCompletedOnboarding {
                MainTabs()
                    .transition(.opacity)
            } else {
                OnboardingFlow { hasCompletedOnboarding = true }
                    .transition(.opacity)
            }
        }
        .environmentObject(link)
        .environmentObject(cover)
        .environmentObject(home)
        .environmentObject(album)
        .environment(\.knobLook, link.settings?.look ?? KnobLook())
        .animation(.easeInOut(duration: 0.35), value: hasCompletedOnboarding)
        .onAppear {
            home.link = link
            if hasCompletedOnboarding { link.start() }
        }
        .onChange(of: link.nowPlayingSerial) { _, _ in
            if link.mode == .music { cover.update(for: link.nowPlaying, link: link) }
        }
        .onChange(of: link.timer) { _, state in timerAlarm.update(state) }
        .onChange(of: link.mode) { _, mode in
            // The knob drops the cover when it leaves Music; give it back.
            // Into Music (or the app just learned it is in Music): the cover
            // for what is playing now.
            if mode == .music {
                cover.forget()
                cover.update(for: link.nowPlaying, link: link)
            }
        }
        .onChange(of: link.status) { _, status in
            // Covers only go to Music: elsewhere the knob has no use for one,
            // and in Lights no memory to spare for it either.
            if status == .connected { cover.forget() }
        }
    }
}

struct MainTabs: View {
    var body: some View {
        TabView {
            Tab("Live", systemImage: "waveform") { LiveTab() }
            Tab("Knob", systemImage: "dial.medium") { KnobTab() }
            Tab("Settings", systemImage: "gearshape") { SettingsTab() }
        }
        .tabBarMinimizeBehavior(.onScrollDown)
    }
}
