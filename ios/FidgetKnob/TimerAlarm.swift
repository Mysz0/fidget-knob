import Foundation
import UserNotifications

/// A notification for when the knob's timer runs out, scheduled for that
/// moment so it arrives with the app closed or the phone in a pocket.
@MainActor
final class TimerAlarm: ObservableObject {
    private var scheduledEnd: Date?
    private static let id = "timer.done"

    func requestPermission() {
        UNUserNotificationCenter.current().requestAuthorization(options: [.alert, .sound]) { _, _ in }
    }

    func update(_ state: TimerState?) {
        let center = UNUserNotificationCenter.current()
        guard let state, state.state == .running, state.left > 0 else {
            if scheduledEnd != nil {
                center.removePendingNotificationRequests(withIdentifiers: [Self.id])
                scheduledEnd = nil
            }
            return
        }
        let end = Date().addingTimeInterval(TimeInterval(state.left))
        // The knob reports every second; only reschedule when the end moved.
        if let s = scheduledEnd, abs(s.timeIntervalSince(end)) < 3 { return }
        scheduledEnd = end

        let content = UNMutableNotificationContent()
        content.title = "Time's up"
        content.body = TimerFormat.long(state.length) + " timer"
        content.sound = .default
        content.interruptionLevel = .timeSensitive
        let trigger = UNTimeIntervalNotificationTrigger(timeInterval: TimeInterval(state.left), repeats: false)
        center.add(UNNotificationRequest(identifier: Self.id, content: content, trigger: trigger))
    }
}

enum TimerFormat {
    /// 4:05, 1:02:00 - as the knob shows it.
    static func clock(_ seconds: Int) -> String {
        let s = max(0, seconds)
        return s >= 3600
            ? String(format: "%d:%02d:%02d", s / 3600, s / 60 % 60, s % 60)
            : String(format: "%d:%02d", s / 60, s % 60)
    }

    /// "25 min", "1 h 30 min", "45 s".
    static func long(_ seconds: Int) -> String {
        if seconds < 60 { return "\(seconds) s" }
        let h = seconds / 3600, m = seconds / 60 % 60, s = seconds % 60
        var parts: [String] = []
        if h > 0 { parts.append("\(h) h") }
        if m > 0 { parts.append("\(m) min") }
        if s > 0 && h == 0 { parts.append("\(s) s") }
        return parts.joined(separator: " ")
    }
}
