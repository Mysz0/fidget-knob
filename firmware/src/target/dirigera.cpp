#include "dirigera.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <math.h>
#include <string.h>

#include "app/home_store.h"
#include "config.h"

namespace dirigera {
namespace {

constexpr uint32_t kSocketTimeoutMs = 4000;
// The hub closes a connection idle for 5 s (its "Keep-Alive: timeout=5").
constexpr uint32_t kKeepAliveMs = 3500;

// How long the bulb takes to fade to each new level or colour. A twist can
// only reach the hub a few times a second, and a bulb that jumps straight to
// each value shows every one of those updates as a visible step. Fading over
// a little longer than the gap between updates makes each fade run into the
// next, so a continuous twist reads as one continuous change of light.
constexpr int kTransitionMs = 400;

}  // namespace

// ------------------------------------------------------------------ Hub ----

bool Hub::begin() {
#ifdef DIRIGERA_INSECURE
  // Bring-up escape hatch: skips certificate validation entirely. Anything on
  // the LAN can impersonate the hub in this mode. Use it to confirm wiring and
  // the token work, then pin the real certificate and remove the define.
  client_.setInsecure();
#else
  // The hub's certificate is self-signed, so it is its own issuer and can be
  // handed to mbedTLS as a trusted root. Pinning this exact certificate is
  // stronger than trusting a public CA would be.
  client_.setCACert(home_store::hubCertPem());
#endif
  client_.setTimeout(kSocketTimeoutMs / 1000);
  return ensureConnected();
}

bool Hub::connected() { return healthy_ && client_.connected(); }

bool Hub::ensureConnected() {
  if (connected()) return true;

  client_.stop();
  healthy_ = client_.connect(home_store::hubHost(), home_store::hubPort());
  if (!healthy_) {
    const uint32_t largest = ESP.getMaxAllocHeap();
    Serial.printf("hub: reconnect failed (heap %u, largest %u)\n",
                  (unsigned)ESP.getFreeHeap(), (unsigned)largest);
    if (WiFi.status() == WL_CONNECTED && largest < 40000) wantsRestart_ = true;
  }
  return healthy_;
}

namespace {

// An HTTP body as a Stream: undoes chunked encoding (or counts down a
// Content-Length) so ArduinoJson can parse straight off the socket.
class BodyStream : public Stream {
 public:
  BodyStream(WiFiClientSecure& c, bool chunked, long length, uint32_t deadline)
      : c_(c), chunked_(chunked), left_(chunked ? 0 : max(0L, length)),
        done_(!chunked && length <= 0), deadline_(deadline) {}

  int available() override { return (left_ > 0 || (!done_ && chunked_)) ? 1 : 0; }
  int peek() override { return -1; }  // ArduinoJson never peeks a Stream.
  size_t write(uint8_t) override { return 0; }

  int read() override {
    if (!fill()) return -1;
    const int b = next();
    if (b >= 0) --left_;
    return b;
  }

  // Reads to the end of the body, so the kept-alive connection is clean for
  // the next request.
  void drain() {
    while (!failed_ && read() >= 0) {
    }
  }
  bool failed() const { return failed_; }

 private:
  int next() {
    while (!c_.available()) {
      if (millis() > deadline_ || !c_.connected()) {
        failed_ = true;
        done_ = true;
        left_ = 0;
        return -1;
      }
      delay(1);
    }
    return c_.read();
  }

  bool fill() {
    if (left_ > 0) return true;
    if (done_ || !chunked_) {
      done_ = true;
      return false;
    }
    if (!first_) c_.readStringUntil('\n');  // CRLF after the last chunk.
    first_ = false;
    const String sizeLine = c_.readStringUntil('\n');
    const long size = strtol(sizeLine.c_str(), nullptr, 16);
    if (size <= 0) {
      c_.readStringUntil('\n');  // Trailing CRLF.
      done_ = true;
      return false;
    }
    left_ = size;
    return true;
  }

  WiFiClientSecure& c_;
  bool chunked_;
  long left_;
  bool done_;
  bool first_ = true;
  bool failed_ = false;
  uint32_t deadline_;
};

}  // namespace

bool Hub::sendRequest(const char* method, const char* path, const char* body,
                      String* responseBody, JsonDocument* json,
                      const JsonDocument* filter) {
  if (sendRequestOnce(method, path, body, responseBody, json, filter)) return true;
  // No reply at all usually means the kept-alive connection went stale (the
  // hub drops idle ones, e.g. while the knob was in another mode): reconnect
  // and try once more. A request the hub answered is never repeated.
  if (lastStatus_ != 0) return false;
  client_.stop();
  return sendRequestOnce(method, path, body, responseBody, json, filter);
}

bool Hub::sendRequestOnce(const char* method, const char* path, const char* body,
                          String* responseBody, JsonDocument* json,
                          const JsonDocument* filter) {
  lastStatus_ = 0;
  lastRequestMs_ = millis();
  if (!ensureConnected()) return false;

  const size_t bodyLength = body ? strlen(body) : 0;

  // The whole request goes out as one write. Sent header by header, as it
  // used to be, each print became its own TLS record and TCP segment, and
  // Nagle's algorithm held every segment until the hub ACKed the one before -
  // which the hub delays on purpose. A PATCH that the hub answers in ~20 ms
  // took 200-650 ms from the knob, and the light could only follow a twist a
  // couple of times a second.
  static char request[1024];
  int length = snprintf(request, sizeof(request),
                        "%s %s HTTP/1.1\r\n"
                        "Host: %s:%d\r\n"
                        "Authorization: Bearer %s\r\n"
                        "Accept: application/json\r\n"
                        // Keep-alive is the whole point: see dirigera.h.
                        "Connection: keep-alive\r\n",
                        method, path, home_store::hubHost(),
                        home_store::hubPort(), home_store::hubToken());
  if (bodyLength > 0) {
    length += snprintf(request + length, sizeof(request) - length,
                       "Content-Type: application/json\r\n"
                       "Content-Length: %u\r\n\r\n%s",
                       (unsigned)bodyLength, body);
  } else {
    length += snprintf(request + length, sizeof(request) - length, "\r\n");
  }
  if (length >= (int)sizeof(request)) {
    healthy_ = false;  // Would be truncated; never send half a request.
    return false;
  }
  client_.setNoDelay(true);
  if (client_.write((const uint8_t*)request, length) != (size_t)length) {
    healthy_ = false;
    return false;
  }

  // --- status line ---
  const uint32_t deadline = millis() + kSocketTimeoutMs;
  while (!client_.available()) {
    if (millis() > deadline || !client_.connected()) {
      healthy_ = false;
      return false;
    }
    delay(1);
  }

  const String status = client_.readStringUntil('\n');
  const int codeStart = status.indexOf(' ');
  const int code = codeStart > 0 ? status.substring(codeStart + 1).toInt() : 0;
  lastStatus_ = code;

  // --- headers ---
  long contentLength = -1;
  bool chunked = false;
  while (client_.connected() || client_.available()) {
    const String line = client_.readStringUntil('\n');
    if (line.length() <= 1) break;  // Blank line ends the headers.

    String lower = line;
    lower.toLowerCase();
    if (lower.startsWith("content-length:")) {
      contentLength = lower.substring(15).toInt();
    } else if (lower.startsWith("transfer-encoding:") &&
               lower.indexOf("chunked") >= 0) {
      chunked = true;
    }
  }

  // --- body ---
  if (json) {
    BodyStream stream(client_, chunked, contentLength, deadline);
    stream.setTimeout(kSocketTimeoutMs);
    const DeserializationError err =
        filter ? deserializeJson(*json, stream, DeserializationOption::Filter(*filter))
               : deserializeJson(*json, stream);
    stream.drain();
    if (stream.failed()) {
      healthy_ = false;
      return false;
    }
    if (err) {
      Serial.printf("hub: bad JSON from %s (%s)\n", path, err.c_str());
      return false;
    }
    return code >= 200 && code < 300;
  }
  String payload;
  if (chunked) {
    // The hub uses chunked encoding for /v1/devices, which is large enough that
    // it will not fit in one chunk.
    while (true) {
      const String sizeLine = client_.readStringUntil('\n');
      const long chunkSize = strtol(sizeLine.c_str(), nullptr, 16);
      if (chunkSize <= 0) {
        client_.readStringUntil('\n');  // Trailing CRLF.
        break;
      }
      for (long i = 0; i < chunkSize; ++i) {
        while (!client_.available()) {
          if (millis() > deadline) { healthy_ = false; return false; }
          delay(1);
        }
        payload += (char)client_.read();
      }
      client_.readStringUntil('\n');  // CRLF after each chunk.
    }
  } else if (contentLength > 0) {
    payload.reserve(contentLength);
    for (long i = 0; i < contentLength; ++i) {
      while (!client_.available()) {
        if (millis() > deadline) { healthy_ = false; return false; }
        delay(1);
      }
      payload += (char)client_.read();
    }
  }

  if (responseBody) *responseBody = payload;
  return code >= 200 && code < 300;
}

bool Hub::patchDevice(const char* deviceId, const char* body) {
  char path[64];
  snprintf(path, sizeof(path), "/v1/devices/%s", deviceId);
  return sendRequest("PATCH", path, body, nullptr);
}

int Hub::discoverLights(target::Registry* registry) {
  // Filter to only the keys we need. The full device list runs to tens of
  // kilobytes of JSON and would not fit in RAM alongside the TLS buffers if
  // parsed whole.
  JsonDocument filter;
  filter[0]["id"] = true;
  filter[0]["type"] = true;
  filter[0]["attributes"]["customName"] = true;
  filter[0]["attributes"]["isOn"] = true;
  filter[0]["attributes"]["lightLevel"] = true;
  filter[0]["attributes"]["colorHue"] = true;
  filter[0]["attributes"]["colorSaturation"] = true;
  filter[0]["attributes"]["canReceive"] = true;

  JsonDocument doc;
  if (!sendRequest("GET", "/v1/devices", nullptr, nullptr, &doc, &filter)) {
    Serial.printf("hub: device list failed (heap %u, largest %u)\n",
                  (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
    return 0;
  }

  int added = 0;
  for (JsonObject device : doc.as<JsonArray>()) {
    if (strcmp(device["type"] | "", "light") != 0) continue;

    const char* id = device["id"] | "";
    if (!id[0]) continue;

    JsonObject attributes = device["attributes"];
    const char* customName = attributes["customName"] | "Light";

    // A light advertises what it can be sent. Only offer colour mode on lights
    // that actually have colour, rather than letting the user twist a hue that
    // a white bulb will ignore.
    // Colour support is decided by whether the light actually reports a hue,
    // not by the "canReceive" capability list. Real hubs turn out not to send
    // canReceive on the /v1/devices response at all, so keying off it marked
    // every light - including full-colour ones - as white-only.
    const bool colourCapable = attributes["colorHue"].is<float>();

    // Allocated once at startup and deliberately never freed: these live for
    // the lifetime of the device, and a registry of raw pointers is cheaper
    // here than dragging in smart-pointer machinery for objects that outlive
    // everything anyway.
    Light* light = new Light(this, id, customName, colourCapable);
    if (lightCount_ < kMaxLights) lights_[lightCount_++] = light;
    light->applyRemoteState(attributes["isOn"] | false,
                            (attributes["lightLevel"] | 0) / 100.0f,
                            attributes["colorHue"] | 0.0f,
                            attributes["colorSaturation"] | 0.0f);
    registry->add(light);
    ++added;

    if (added >= target::Registry::kMaxTargets) break;
  }
  return added;
}

void Hub::startWorker() {
  // 8 KB: a TLS reconnect runs mbedTLS on this stack. Same priority as the
  // Arduino loop task, so the two share the CPU while a handshake grinds and
  // the main loop keeps drawing and reading gestures throughout.
  xTaskCreate(&Hub::workerMain, "hub", 8192, this, 1, nullptr);
}

void Hub::workerMain(void* self) { static_cast<Hub*>(self)->workerLoop(); }

void Hub::pause() {
  paused_ = true;
  for (int i = 0; i < 200 && !parked_; ++i) delay(10);
}

void Hub::workerLoop() {
  while (true) {
    if (paused_) {
      if (!parked_) {
        client_.stop();  // Frees the TLS buffers.
        healthy_ = false;
        parked_ = true;
      }
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }
    parked_ = false;
    bool sent = false;
    for (int i = 0; i < lightCount_; ++i) {
      Light* light = lights_[i];
      if (light->takeRefreshRequest()) {
        refreshLight(light);
        sent = true;
      }
      const Light::Pending p = light->takePending(transitions_);
      if (p.field == Light::Field::None) continue;

      const uint32_t started = millis();
      const bool ok = patchDevice(light->id(), p.body);
      light->finishWrite(p, ok);
      if (!ok && p.withTransition && lastStatus_ == 400) {
        // A hub that does not understand transitionTime refuses the whole
        // write. Drop it from then on; the retry goes out plain, and the light
        // at worst steps the way it always did rather than stopping.
        transitions_ = false;
        Serial.println("hub: transitionTime rejected; sending plain writes");
      }
      Serial.printf("hub: write %s in %lu ms\n", ok ? "ok" : "FAILED",
                    (unsigned long)(millis() - started));
      if (!ok) {
        // Signal strength is the first thing to rule out: the Xiao C3 has no
        // antenna of its own, only the U.FL socket, and a missing or loose
        // external antenna looks exactly like a hub that will not answer.
        Serial.printf("hub: write failed (wifi %s, %d dBm)\n",
                      WiFi.status() == WL_CONNECTED ? "up" : "DOWN",
                      (int)WiFi.RSSI());
      }
      sent = true;
    }
    // Keep-alive: the hub drops a connection idle for 5 s, and opening a
    // new one needs memory that may not be there. Re-reading a lamp now and
    // then keeps it open - and picks up changes made in the IKEA app or at a
    // wall switch, one lamp at a time.
    if (!sent && lightCount_ > 0 && millis() - lastRequestMs_ > kKeepAliveMs) {
      refreshLight(lights_[keepAliveNext_++ % lightCount_]);
      sent = true;
    }
    // Idle politely; a detent arriving now waits at most this long to go out.
    if (!sent) vTaskDelay(pdMS_TO_TICKS(10));
  }
}

bool Hub::refreshLight(Light* light) {
  char path[64];
  snprintf(path, sizeof(path), "/v1/devices/%s", light->id());
  String payload;
  if (!sendRequest("GET", path, nullptr, &payload)) return false;

  JsonDocument filter;
  filter["attributes"]["isOn"] = true;
  filter["attributes"]["lightLevel"] = true;
  filter["attributes"]["colorHue"] = true;
  filter["attributes"]["colorSaturation"] = true;
  JsonDocument doc;
  if (deserializeJson(doc, payload, DeserializationOption::Filter(filter))) {
    return false;
  }
  JsonObject a = doc["attributes"];
  // applyRemoteState already skips any field with a local write in flight.
  light->applyRemoteState(a["isOn"] | light->isOn(),
                          (a["lightLevel"] | (int)lroundf(light->level() * 100)) / 100.0f,
                          a["colorHue"] | light->hue(),
                          a["colorSaturation"] | light->saturation());
  return true;
}

// ---------------------------------------------------------------- Light ----

Light::Light(Hub* hub, const char* id, const char* name, bool colourCapable)
    : hub_(hub), colourCapable_(colourCapable) {
  strncpy(id_, id, kIdLength - 1);
  id_[kIdLength - 1] = '\0';
  strncpy(name_, name, kNameLength - 1);
  name_[kNameLength - 1] = '\0';
}

void Light::setLevel(float value) {
  portENTER_CRITICAL(&lock_);
  level_ = constrain(value, 0.0f, 1.0f);
  levelDirty_ = true;
  ++levelGen_;
  // Raising the level of an off light is understood as turning it on; making
  // the user press the button first would be a pointless extra step.
  if (level_ > 0.0f && !on_) {
    on_ = true;
    powerDirty_ = true;
    ++powerGen_;
  }
  portEXIT_CRITICAL(&lock_);
}

void Light::setColour(float hue, float saturation) {
  portENTER_CRITICAL(&lock_);
  hue_ = fmodf(hue, 360.0f);
  if (hue_ < 0) hue_ += 360.0f;
  saturation_ = constrain(saturation, 0.0f, 1.0f);
  colourDirty_ = true;
  ++colourGen_;
  portEXIT_CRITICAL(&lock_);
}

void Light::toggle() {
  portENTER_CRITICAL(&lock_);
  on_ = !on_;
  powerDirty_ = true;
  ++powerGen_;
  portEXIT_CRITICAL(&lock_);
}

void Light::applyRemoteState(bool on, float level, float hue,
                             float saturation) {
  portENTER_CRITICAL(&lock_);
  // Remote state is only accepted for fields with no local write in flight.
  // Otherwise an echo of an older value from the hub would yank the display
  // backwards while the user is still turning the knob.
  if (!powerDirty_) on_ = on;
  if (!levelDirty_) level_ = constrain(level, 0.0f, 1.0f);
  if (!colourDirty_) {
    hue_ = hue;
    saturation_ = constrain(saturation, 0.0f, 1.0f);
  }
  portEXIT_CRITICAL(&lock_);
}

Light::Pending Light::takePending(bool withTransition) {
  Pending p;
  const uint32_t now = millis();

  portENTER_CRITICAL(&lock_);
  const bool due = now - lastWriteMs_ >= kMinWriteIntervalMs &&
                   (int32_t)(now - retryAtMs_) >= 0;
  if (!due) {
    // Nothing: p.field stays None.
  } else if (powerDirty_) {
    // Power first and on its own: the hub rejects a lightLevel written in the
    // same PATCH as isOn.
    p.field = Field::Power;
    p.generation = powerGen_;
    snprintf(p.body, sizeof(p.body), "[{\"attributes\":{\"isOn\":%s}}]",
             on_ ? "true" : "false");
  } else if (levelDirty_) {
    // lightLevel is 1..100 on the hub; 0 is not accepted, so a request for zero
    // becomes the dimmest the bulb will go.
    p.field = Field::Level;
    p.generation = levelGen_;
    p.withTransition = withTransition;
    const int percent = max(1, (int)lroundf(level_ * 100.0f));
    if (withTransition) {
      snprintf(p.body, sizeof(p.body),
               "[{\"attributes\":{\"lightLevel\":%d},\"transitionTime\":%d}]",
               percent, kTransitionMs);
    } else {
      snprintf(p.body, sizeof(p.body),
               "[{\"attributes\":{\"lightLevel\":%d}}]", percent);
    }
  } else if (colourDirty_) {
    p.field = Field::Colour;
    p.generation = colourGen_;
    p.withTransition = withTransition;
    if (withTransition) {
      snprintf(p.body, sizeof(p.body),
               "[{\"attributes\":{\"colorHue\":%.1f,\"colorSaturation\":%.2f},"
               "\"transitionTime\":%d}]",
               hue_, saturation_, kTransitionMs);
    } else {
      snprintf(p.body, sizeof(p.body),
               "[{\"attributes\":{\"colorHue\":%.1f,\"colorSaturation\":%.2f}}]",
               hue_, saturation_);
    }
  }
  portEXIT_CRITICAL(&lock_);
  return p;
}

void Light::finishWrite(const Pending& p, bool ok) {
  const uint32_t now = millis();

  portENTER_CRITICAL(&lock_);
  lastWriteMs_ = now;
  if (!ok) {
    retryAtMs_ = now + kRetryAfterFailureMs;
  } else if (p.field == Field::Power && p.generation == powerGen_) {
    powerDirty_ = false;
  } else if (p.field == Field::Level && p.generation == levelGen_) {
    levelDirty_ = false;
  } else if (p.field == Field::Colour && p.generation == colourGen_) {
    colourDirty_ = false;
  }
  portEXIT_CRITICAL(&lock_);
}

}  // namespace dirigera
