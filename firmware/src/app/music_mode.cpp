#include "music_mode.h"

#include <Arduino.h>

#include <string>
#include <vector>

#include "app/mode_store.h"
#include "app/settings_store.h"
#include "ble/ams_client.h"
#include "ble/hid_remote.h"
#include "ble/knob_service.h"
#include "haptics/haptics.h"
#include "music_controller.h"
#include "render/display.h"
#include "render/field.h"

namespace music_mode {
namespace {

music::Controller g_controller;
constexpr uint32_t kFrameIntervalMs = 33;
uint32_t g_lastFrameMs = 0;

// The raw AMS strings, for the app's cover lookup. AMS reports artist, album
// and title as three separate notifications, so the app is told only once
// they have settled - otherwise it would look up "new artist, old album".
std::string g_artist, g_album, g_title;
bool g_nowPlayingDirty = false;
uint32_t g_nowPlayingChangedMs = 0;
constexpr uint32_t kNowPlayingSettleMs = 300;

knob::CoverStyle g_style = knob::CoverStyle::Mosaic;
std::vector<uint8_t> g_jpeg;  // Photo cover for the current track, if any.
bool g_photoShown = false;    // The panel currently holds the photo.
bool g_lastPlaying = false;   // Last playing state told to the app.
std::string g_publishedTrack;  // artist/album/title last told to the app.
float g_spinDegrees = 0.0f;   // Spinning cover, while playing.
// The photo cover, spinning: decoded once into a small sprite (a full
// 240x240 one is 115 KB, more than the knob has in one piece) and turned and
// scaled up onto the panel each frame.
LGFX_Sprite* g_spinPhoto = nullptr;
uint32_t g_lastSpinDrawMs = 0;
constexpr uint32_t kSpinPhotoFrameMs = 66;  // ~15 fps is plenty at 24 deg/s.

void freeSpinPhoto() {
  if (!g_spinPhoto) return;
  g_spinPhoto->deleteSprite();
  delete g_spinPhoto;
  g_spinPhoto = nullptr;
}

// The biggest sprite memory allows; the bigger, the sharper the spin.
bool makeSpinPhoto() {
  if (g_spinPhoto) return true;
  if (g_jpeg.empty()) return false;
  g_spinPhoto = new LGFX_Sprite(&display::lcd);
  g_spinPhoto->setColorDepth(16);
  for (int side : {168, 144, 120, 96}) {
    if (!g_spinPhoto->createSprite(side, side)) continue;
    g_spinPhoto->drawJpg(g_jpeg.data(), g_jpeg.size(), 0, 0, side, side, 0, 0,
                         (float)side / display::kWidth);
    Serial.printf("music: spinning photo at %d px\n", side);
    return true;
  }
  delete g_spinPhoto;
  g_spinPhoto = nullptr;
  return false;
}
constexpr float kSpinDegreesPerSecond = 24.0f;  // Matches the app.

int commandFor(knob::Action a) {
  switch (a) {
    case knob::Action::PlayPause: return (int)ams::Command::TogglePlayPause;
    case knob::Action::Next: return (int)ams::Command::NextTrack;
    case knob::Action::Previous: return (int)ams::Command::PreviousTrack;
    default: return music::kTapNone;
  }
}

void forgetCover() {
  freeSpinPhoto();
  field::setMosaic(nullptr);
  field::setTitleMask(0, 0, nullptr);  // The app renders one per track.
  std::vector<uint8_t>().swap(g_jpeg);
  g_photoShown = false;
  field::invalidate();
}

void noteTrackField(std::string* field, const std::string& value,
                    uint32_t now) {
  if (*field == value) return;
  *field = value;
  g_nowPlayingDirty = true;
  g_nowPlayingChangedMs = now;
}

void collectFromPhone() {
  knob::CoverStyle style;
  if (knob_service::takeCoverStyle(&style) && style != g_style) {
    g_style = style;
    mode_store::saveCoverStyle(style);
    g_photoShown = false;
    field::invalidate();
    Serial.printf("music: cover style %s\n",
                  style == knob::CoverStyle::Photo ? "photo" : "mosaic");
  }

  uint8_t kind;
  std::vector<uint8_t> bytes;
  if (!knob_service::takeArtwork(&kind, &bytes)) return;

  if (kind == knob::kArtworkMosaic &&
      bytes.size() == (size_t)knob::kMosaicCells * knob::kMosaicCells * 2) {
    std::vector<uint16_t> grid(knob::kMosaicCells * knob::kMosaicCells);
    for (size_t i = 0; i < grid.size(); ++i) {
      grid[i] = (uint16_t)(bytes[i * 2] | (bytes[i * 2 + 1] << 8));
    }
    field::setMosaic(grid.data());
    Serial.println("music: mosaic cover received");
  } else if (kind == knob::kArtworkTitle && bytes.size() >= 2 &&
             bytes.size() == 2 + (size_t)bytes[0] * bytes[1]) {
    field::setTitleMask(bytes[0], bytes[1], bytes.data() + 2);
    Serial.printf("music: title overlay received (%ux%u)\n", bytes[0], bytes[1]);
  } else if (kind == knob::kArtworkJpeg) {
    freeSpinPhoto();
    g_jpeg.swap(bytes);
    g_photoShown = false;
    Serial.printf("music: photo cover received (%u bytes)\n",
                  (unsigned)g_jpeg.size());
  }
}

}  // namespace

void begin() {
  g_style = mode_store::loadCoverStyle();
  applySettings();
  g_photoShown = false;
  // Back in Music with a track known: tell the app again, so it sends the
  // cover (leaving Music dropped it).
  if (!g_title.empty()) {
    g_nowPlayingDirty = true;
    g_nowPlayingChangedMs = millis();
  }
  // The same track as before is not a new one: a cover the app sends as
  // Music comes back must survive this re-announcement.
  g_publishedTrack = g_artist + "\x1F" + g_album + "\x1F" + g_title;
}

void end() {
  forgetCover();  // Other modes draw the plain field, not the cover.
  g_photoShown = false;
}

void onGestures(const gestures::Events& g, const button::Events& b) {
  (void)b;  // The button has no Music-mode role yet.
  const music::Feedback fb = g_controller.onGestures(
      music::Gestures{g.twistSteps, g.tap, g.shake}, millis());

  if (fb.limit) haptics::play(haptics::Pattern::Limit);
  else if (fb.volumeStep) haptics::play(haptics::Pattern::Detent);
  if (fb.toggled || fb.skipped) haptics::play(haptics::Pattern::Confirm);

  if (fb.toggled) Serial.println("music: play/pause");
  if (fb.skipped) Serial.println("music: next track");
}

void tick() {
  const uint32_t now = millis();

  ams_client::poll();
  // Connected means a phone takes commands: an iPhone through AMS, or any
  // phone through the media remote.
  g_controller.setConnected(ams_client::ready() || hid_remote::ready(), now);
  // Phone (re)connected: announce the track again so the app re-sends a
  // cover that may never have made it across.
  static bool wasReady = false;
  const bool appListening = knob_service::takeNowPlayingSubscribed();
  if (((ams_client::ready() && !wasReady) || appListening) && !g_title.empty()) {
    g_nowPlayingDirty = true;
    g_nowPlayingChangedMs = now;
  }
  wasReady = ams_client::ready();

  ams::Update u;
  while (ams_client::nextUpdate(&u)) {
    g_controller.onUpdate(u, now);
    if (u.entity != ams::Entity::Track) continue;
    if (u.attribute == ams::kTrackTitle) {
      noteTrackField(&g_title, u.value, now);
    } else if (u.attribute == ams::kTrackArtist) {
      noteTrackField(&g_artist, u.value, now);
    } else if (u.attribute == ams::kTrackAlbum) {
      noteTrackField(&g_album, u.value, now);
    }
  }

  // Playing/paused changes are news for the app too (its cover spins).
  const bool playing = g_controller.view(now).playing;
  if (playing != g_lastPlaying) {
    g_lastPlaying = playing;
    g_nowPlayingDirty = true;
  }
  if (g_nowPlayingDirty && now - g_nowPlayingChangedMs >= kNowPlayingSettleMs) {
    g_nowPlayingDirty = false;
    // The cover is dropped only when the track really changed, and only now,
    // as the app is told: iOS re-sends the same title now and then, and
    // dropping on every title update threw away a cover that had just
    // arrived - the app, seeing the same track, never sent it again.
    const std::string track = g_artist + "\x1F" + g_album + "\x1F" + g_title;
    if (track != g_publishedTrack) {
      g_publishedTrack = track;
      forgetCover();
      Serial.printf("music: now playing \"%s\"\n", g_title.c_str());
    }
    knob_service::setNowPlaying(g_artist, g_album, g_title, playing);
  }

  collectFromPhone();

  ams::Command cmd;
  if (g_controller.nextCommand(now, &cmd) && !ams_client::send(cmd) &&
      !hid_remote::send(cmd)) {
    Serial.printf("music: command %u not delivered\n", (unsigned)cmd);
  }

  if (now - g_lastFrameMs < kFrameIntervalMs) return;
  g_lastFrameMs = now;
  if (display::backlightLevel() == 0) return;

  const music::View v = g_controller.view(now);
  if (settings_store::current().spinCover && v.playing) {
    g_spinDegrees = fmodf(g_spinDegrees + kSpinDegreesPerSecond * kFrameIntervalMs / 1000.0f, 360.0f);
  }

  // Photo cover: drawn once straight to the panel by LovyanGFX's JPEG decoder,
  // and left alone until an overlay (title, volume) needs the sprite field.
  const bool photo = g_style == knob::CoverStyle::Photo && !g_jpeg.empty() &&
                     v.connected && !v.showTitle && !v.showVolume;
  const bool spinning = settings_store::current().spinCover && v.playing;
  if (photo && spinning && makeSpinPhoto()) {
    // The sprite's square covers the round screen at any angle, so turning
    // it leaves no corners showing and nothing needs clearing.
    if (!g_photoShown || now - g_lastSpinDrawMs >= kSpinPhotoFrameMs) {
      g_lastSpinDrawMs = now;
      const float zoom = (float)display::kWidth / g_spinPhoto->width();
      g_spinPhoto->pushRotateZoomWithAA(&display::lcd, display::kWidth / 2.0f,
                                        display::kHeight / 2.0f, g_spinDegrees, zoom, zoom);
      g_photoShown = true;
    }
    return;
  }
  if (photo) {
    if (g_spinPhoto && !spinning) freeSpinPhoto();  // Paused: the sharp photo.
    if (g_photoShown && g_spinDegrees != 0.0f && !spinning && g_lastSpinDrawMs) {
      g_photoShown = false;  // Redraw it upright and full resolution.
      g_lastSpinDrawMs = 0;
    }
    if (!g_photoShown) {
      display::lcd.drawJpg(g_jpeg.data(), g_jpeg.size(), 0, 0,
                           display::kWidth, display::kHeight);
      g_photoShown = true;
    }
    return;
  }
  if (g_photoShown) {
    g_photoShown = false;
    field::invalidate();  // The panel holds the photo, not the last field.
  }

  field::MusicScreen screen{v.connected, v.playing, v.showVolume, v.volume,
                            v.showTitle, v.title.c_str(), v.artist.c_str(),
                            g_spinDegrees};
  field::drawMusic(screen);
}

void beforeSleep() { ams_client::shutdown(); }

void applySettings() {
  const knob::KnobSettings& s = settings_store::current();
  g_controller.setGestureCommands(commandFor(s.musicTap),
                                  commandFor(s.musicShake));
  g_controller.setTitleShowMs(s.overlayOn ? s.overlaySeconds * 1000UL : 0);
  static const int kStepsPerNotch[3] = {32, 24, 16};  // Low, normal, high.
  const music::Controller::Twist twist =
      s.musicTwist == knob::MusicTwist::None ? music::Controller::Twist::None
      : s.musicTwist == knob::MusicTwist::Tracks ? music::Controller::Twist::Tracks
                                                 : music::Controller::Twist::Volume;
  g_controller.setTwist(twist, kStepsPerNotch[min<uint8_t>(s.twistSensitivity, 2)]);
}

}  // namespace music_mode
