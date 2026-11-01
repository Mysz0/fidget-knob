#pragma once

#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#include <freertos/FreeRTOS.h>

#include "target.h"

// IKEA DIRIGERA hub client.
//
// The hub exposes a local API on HTTPS port 8443 with bearer-token auth. No
// traffic leaves the LAN.
//
//   Enumerate  GET   /v1/devices
//   Command    PATCH /v1/devices/{id}   [{"attributes":{"lightLevel":70}}]
//   Live state wss://{hub}:8443/v1
//
// The certificate is self-signed, so it is pinned directly (see config.h)
// rather than validated against a CA.

namespace dirigera {

class Light;

// Opens and holds one TLS connection to the hub.
//
// The connection is kept alive deliberately. A TLS handshake on the C3 costs
// several hundred milliseconds, which would be paid on the first twist of every
// interaction and would make the knob feel laggy exactly when it matters. With
// the socket warm, a command is one already-encrypted write.
class Hub {
 public:
  bool begin();

  // True if the socket is open and usable.
  bool connected();

  // Last known health of the link, safe to read from any task (connected()
  // touches the socket, which belongs to the worker).
  bool healthy() const { return healthy_; }
  // A reconnect failed for want of one big enough block of memory - the TLS
  // handshake needs it, and with Bluetooth up the heap can be too broken up.
  // Only a restart (TLS first, Bluetooth after) gets it back.
  bool wantsRestart() const { return wantsRestart_; }
  // Closes the connection and parks the worker, freeing the TLS memory while
  // the knob is in a mode that needs it (Music, Photos). Blocks until the
  // worker has let go, at most ~2 s. resume() lets it reconnect on demand.
  void pause();
  void resume() { paused_ = false; }

  // Reopens the connection if it has dropped. Cheap when already connected.
  bool ensureConnected();

  // Sends a PATCH. `body` is the raw JSON array. Returns false on transport
  // failure, which marks the connection dead so the next call reconnects.
  bool patchDevice(const char* deviceId, const char* body);

  // Enumerates light devices and registers one Light per device found.
  // Returns the number added.
  int discoverLights(target::Registry* registry);

  // Starts the background task that sends every light's pending writes. Call
  // once, after discoverLights().
  //
  // All hub traffic after boot happens on that task, never on the main loop. A
  // request to a hub that has gone quiet blocks for up to the 4 s socket
  // timeout, plus a TLS handshake to reconnect; on the main loop that froze the
  // screen, the haptics and the gestures along with it, and the twist that
  // piled up meanwhile landed as one jump of a dozen detents.
  void startWorker();

  // Whether writes ask the hub to fade to the new value (see kTransitionMs in
  // dirigera.cpp). Starts on, and switches itself off for good if the hub
  // rejects a request carrying it.
  bool transitions() const { return transitions_; }

 private:
  static constexpr int kMaxLights = 12;

  WiFiClientSecure client_;
  bool healthy_ = false;
  volatile bool wantsRestart_ = false;
  volatile bool paused_ = false;
  volatile bool parked_ = false;
  uint32_t lastRequestMs_ = 0;
  int keepAliveNext_ = 0;
  bool transitions_ = true;
  int lastStatus_ = 0;  // HTTP status of the last request; 0 = no reply.

  Light* lights_[kMaxLights] = {nullptr};
  int lightCount_ = 0;

  static void workerMain(void* self);
  void workerLoop();
  bool refreshLight(Light* light);

  // With `json`, the reply is parsed (through `filter`) as it streams in,
  // never held whole: the device list runs to tens of kilobytes, and with
  // Bluetooth up there is no single free block that large.
  bool sendRequest(const char* method, const char* path, const char* body,
                   String* responseBody, JsonDocument* json = nullptr,
                   const JsonDocument* filter = nullptr);
  bool sendRequestOnce(const char* method, const char* path, const char* body,
                       String* responseBody, JsonDocument* json,
                       const JsonDocument* filter);
};

// One controllable light on the hub.
class Light : public target::Target {
 public:
  Light(Hub* hub, const char* id, const char* name, bool colourCapable);

  const char* name() const override { return name_; }
  bool supportsColour() const override { return colourCapable_; }

  bool isOn() const override { return on_; }
  float level() const override { return level_; }
  float hue() const override { return hue_; }
  float saturation() const override { return saturation_; }

  void setLevel(float value) override;
  void setColour(float hue, float saturation) override;
  void toggle() override;

  // Writes go out on the hub's worker task; nothing to do on the main loop.
  void tick() override {}

  // Applies state pushed from the hub's websocket, so the knob reflects changes
  // made from the IKEA app or a wall switch.
  void applyRemoteState(bool on, float level, float hue, float saturation);

  // --- Worker side. Called only from the hub's worker task. ---

  enum class Field : uint8_t { None, Power, Level, Colour };

  // A snapshot of one pending write, taken atomically.
  struct Pending {
    Field field = Field::None;
    uint32_t generation = 0;
    bool withTransition = false;
    char body[112];
  };

  // Picks the next field to send, or Field::None. Honours the write interval
  // and the retry backoff. `withTransition` asks the bulb to fade to level
  // and colour changes rather than jump.
  Pending takePending(bool withTransition);

  // Records the outcome. On success the field is only marked clean if nothing
  // newer was set while the request was in flight - otherwise the newer value
  // would be silently dropped.
  void finishWrite(const Pending& p, bool ok);

  const char* id() const { return id_; }

  // Ask the worker to re-read this light from the hub - its state may have
  // been changed by the app, the IKEA app or a wall switch.
  void requestRefresh() { refreshWanted_ = true; }
  bool takeRefreshRequest() {
    const bool r = refreshWanted_;
    refreshWanted_ = false;
    return r;
  }

 private:
  // Retry gap after a failed write, so a hub that is down is not hammered.
  static constexpr uint32_t kRetryAfterFailureMs = 500;

  static constexpr size_t kIdLength = 40;
  static constexpr size_t kNameLength = 24;

  // Minimum gap between writes for this light. A fast spin generates detents
  // far quicker than the hub can service them; without coalescing the queue
  // would back up and the light would keep moving after the hand stopped.
  static constexpr uint32_t kMinWriteIntervalMs = 100;

  Hub* hub_;
  char id_[kIdLength];
  char name_[kNameLength];
  bool colourCapable_;

  bool on_ = false;
  float level_ = 0.5f;
  float hue_ = 30.0f;
  float saturation_ = 0.0f;

  // Newest-value-wins: a pending write holds the latest value, not a queue.
  // Each field's generation counts its sets, so the worker can tell whether
  // the value it sent is still the latest one.
  bool levelDirty_ = false;
  bool colourDirty_ = false;
  bool powerDirty_ = false;
  uint32_t levelGen_ = 0;
  uint32_t colourGen_ = 0;
  uint32_t powerGen_ = 0;
  uint32_t lastWriteMs_ = 0;
  uint32_t retryAtMs_ = 0;
  volatile bool refreshWanted_ = false;

  // Guards the fields above between the main loop and the worker task.
  portMUX_TYPE lock_ = portMUX_INITIALIZER_UNLOCKED;
};

}  // namespace dirigera
