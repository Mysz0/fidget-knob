#include <Arduino.h>

#include <string>
#include <vector>

#include "ancs_codec.h"
#include "app/desk_modes.h"
#include "ascii_fold.h"
#include "ble/ams_client.h"
#include "haptics/haptics.h"
#include "pixel_font.h"
#include "render/display.h"
#include "render/field.h"

namespace notify_mode {
namespace {

struct Item {
  uint32_t uid;
  uint8_t category;
  bool filled;  // Its text has arrived.
  std::string appId, title, message;
};

constexpr size_t kMaxItems = 10;
std::vector<Item> g_items;  // Newest first.
int g_current = 0;

// App display names, asked for once per app.
struct AppName {
  std::string id, name;
};
std::vector<AppName> g_appNames;
std::string g_askedApp;

// One attribute request at a time; the next goes when it is answered.
ancs::Assembler g_assembler;
bool g_waiting = false;
uint32_t g_waitSinceMs = 0;
constexpr uint32_t kReplyTimeoutMs = 2000;

bool g_fresh = false;  // A new one arrived: buzz and light up (this mode only).
int g_twist = 0;
constexpr int kStepsPerItem = 24;
uint32_t g_lastFrameMs = 0;
bool g_active = false;

Item* find(uint32_t uid) {
  for (auto& it : g_items)
    if (it.uid == uid) return &it;
  return nullptr;
}

const std::string* appName(const std::string& id) {
  for (const auto& a : g_appNames)
    if (a.id == id) return &a.name;
  return nullptr;
}

void requestNext() {
  if (g_waiting || !ams_client::ancsReady()) return;
  for (const auto& it : g_items) {
    if (it.filled) continue;
    if (ams_client::ancsCommand(ancs::getAttributes(it.uid, 40, 120))) {
      g_waiting = true;
      g_waitSinceMs = millis();
    }
    return;
  }
  // All filled: any app whose name is unknown.
  for (const auto& it : g_items) {
    if (it.appId.empty() || appName(it.appId) || it.appId == g_askedApp) continue;
    g_askedApp = it.appId;
    if (ams_client::ancsCommand(ancs::getAppName(it.appId))) {
      g_waiting = true;
      g_waitSinceMs = millis();
    }
    return;
  }
}

void onSource(const ancs::SourceEvent& e) {
  if (e.event == ancs::Event::Removed) {
    for (size_t i = 0; i < g_items.size(); ++i) {
      if (g_items[i].uid == e.uid) {
        g_items.erase(g_items.begin() + i);
        if (g_current >= (int)g_items.size()) g_current = max(0, (int)g_items.size() - 1);
        break;
      }
    }
    return;
  }
  Item* it = find(e.uid);
  if (!it) {
    g_items.insert(g_items.begin(), Item{e.uid, e.category, false, "", "", ""});
    if (g_items.size() > kMaxItems) g_items.pop_back();
    g_current = 0;  // The newest is the one to see.
    if (!(e.flags & (ancs::kFlagPreExisting | ancs::kFlagSilent))) g_fresh = true;
  } else {
    it->filled = false;  // Modified: fetch the text again.
  }
}

void onData(const std::vector<uint8_t>& bytes) {
  if (!g_assembler.feed(bytes.data(), bytes.size())) return;
  g_waiting = false;
  if (g_assembler.isAppReply()) {
    g_appNames.push_back({g_assembler.appId(), g_assembler.appName()});
    if (g_appNames.size() > 16) g_appNames.erase(g_appNames.begin());
    return;
  }
  const ancs::Notification& n = g_assembler.notification();
  if (Item* it = find(n.uid)) {
    it->appId = n.appId;
    it->title = n.title;
    it->message = n.message;
    it->filled = true;
  }
}

std::string upper(std::string s) {
  for (char& c : s) c = (char)toupper((unsigned char)c);
  return s;
}

// The block of small text: app (and position), who, and the message.
std::string compose() {
  if (g_items.empty()) return "ALL CLEAR";
  const Item& it = g_items[g_current];
  constexpr size_t kChars = 15;
  std::string app = it.appId;
  if (const std::string* name = appName(it.appId)) app = *name;
  else {
    const size_t dot = app.rfind('.');  // com.apple.MobileSMS -> MobileSMS
    if (dot != std::string::npos) app = app.substr(dot + 1);
  }
  char pos[12] = "";
  if (g_items.size() > 1) snprintf(pos, sizeof pos, " %d/%d", g_current + 1, (int)g_items.size());
  std::string head = knob::foldToAscii(app, kChars - strlen(pos)) + pos;
  if (!it.filled) return upper(head) + "\n\n...";
  std::string out = upper(head) + "\n" + upper(knob::foldToAscii(it.title, kChars));
  for (const auto& l : pixel_font::wrap(knob::foldToAscii(it.message, 200), kChars, 3)) {
    out += "\n" + upper(l);
  }
  return out;
}

void dismiss(int index) {
  if (index < 0 || index >= (int)g_items.size()) return;
  ams_client::ancsCommand(ancs::performAction(g_items[index].uid, /*positive=*/false));
  g_items.erase(g_items.begin() + index);
  if (g_current >= (int)g_items.size()) g_current = max(0, (int)g_items.size() - 1);
}

}  // namespace

void poll() {
  uint8_t kind;
  std::vector<uint8_t> bytes;
  for (int n = 0; n < 6 && ams_client::nextAncs(&kind, &bytes); ++n) {
    if (kind == 0) {
      ancs::SourceEvent e;
      if (ancs::parseSource(bytes.data(), bytes.size(), &e)) onSource(e);
    } else {
      onData(bytes);
    }
  }
  if (g_waiting && millis() - g_waitSinceMs > kReplyTimeoutMs) {
    g_waiting = false;  // No answer: drop it and move on.
    g_assembler.reset();
  }
  if (!ams_client::ancsReady()) g_waiting = false;
  requestNext();
}

void begin() {
  g_active = true;
  g_current = 0;
  g_fresh = false;
}

void onGestures(const gestures::Events& g, const button::Events& b) {
  if ((g.tap || b.shortPress) && !g_items.empty()) {
    dismiss(g_current);
    haptics::play(haptics::Pattern::Confirm);
  }
  if (g.shake && !g_items.empty()) {
    while (!g_items.empty()) dismiss(0);
    haptics::play(haptics::Pattern::ModeChange);
  }
  if (g.twistSteps != 0 && g_items.size() > 1) {
    g_twist += g.twistSteps;
    while (abs(g_twist) >= kStepsPerItem) {
      const int dir = g_twist > 0 ? 1 : -1;
      g_twist -= dir * kStepsPerItem;
      const int n = (int)g_items.size();
      g_current = ((g_current + dir) % n + n) % n;
      haptics::play(haptics::Pattern::Detent);
    }
  }
}

bool tick() {
  bool poke = false;
  if (g_fresh) {
    g_fresh = false;
    haptics::play(haptics::Pattern::TargetSelect);
    poke = true;
  }
  const uint32_t ms = millis();
  if (ms - g_lastFrameMs < 66) return poke;
  g_lastFrameMs = ms;
  if (display::backlightLevel() == 0) return poke;
  const std::string text = compose();
  field::Face f{};
  f.field.on = true;
  f.field.saturation = 0.0f;
  f.field.value = g_items.empty() ? 0.08f : 0.45f;
  f.dim = 0.22f;
  f.big = "";
  f.small = "";
  f.ring = -1.0f;
  f.lines = text.c_str();
  field::drawFace(f);
  return poke;
}

}  // namespace notify_mode
