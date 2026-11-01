#include "ams_client.h"

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <string.h>

#include "app/settings_store.h"
#include "ancs_codec.h"
#include "hid_remote.h"
#include "knob_service.h"

// NimBLE host: indicate Service Changed to the connected peer.
extern "C" void ble_svc_gatt_changed(uint16_t start_handle, uint16_t end_handle);

namespace ams_client {
namespace {

// One AMS notification: up to an ATT payload. Titles longer than this arrive
// flagged as truncated, which the codec reports.
struct RawNotification {
  uint8_t len;
  uint8_t data[180];
};

QueueHandle_t g_queue = nullptr;
// ANCS packets: kind 0 = Notification Source, 1 = Data Source.
struct AncsPacket {
  uint8_t kind;
  uint8_t len;
  uint8_t data[180];
};
QueueHandle_t g_ancsQueue = nullptr;
NimBLERemoteCharacteristic* g_ancsControl = nullptr;
NimBLEServer* g_server = nullptr;
NimBLEClient* g_client = nullptr;
NimBLERemoteCharacteristic* g_remoteCommand = nullptr;

// Every connected phone, tracked on its own: the iPhone that carries Apple's
// music service (AMS) and notifications, and any others (an Android on the
// media remote). It used to be one "current phone", so whichever connected
// or paired last took the slot - the phones swapped and broke each other.
struct Link {
  uint16_t handle = BLE_HS_CONN_HANDLE_NONE;
  uint32_t since = 0;
  bool encrypted = false;
  bool drop = false;   // Pairing failed: disconnect from poll().
  uint8_t tries = 0;   // AMS set-up attempts.
  bool noAms = false;  // Not an iPhone (no AMS): it only gets the remote.
};
constexpr int kMaxLinks = 3;
Link g_links[kMaxLinks];
portMUX_TYPE g_linkLock = portMUX_INITIALIZER_UNLOCKED;
volatile uint16_t g_amsHandle = BLE_HS_CONN_HANDLE_NONE;    // The AMS link.
volatile uint16_t g_setupHandle = BLE_HS_CONN_HANDLE_NONE;  // AMS set-up due.

Link* findLink(uint16_t h) {
  for (Link& l : g_links)
    if (l.handle == h) return &l;
  return nullptr;
}
volatile bool g_ready = false;
// Link health. A link that never gets encrypted (a missed pairing prompt, a
// phone that "forgot" the knob mid-way) used to sit there until a reboot,
// holding the knob hostage; now it is dropped so a clean retry can start.
uint32_t g_lastAdvCheckMs = 0;
constexpr uint32_t kEncryptTimeoutMs = 15000;

void onEntityUpdate(NimBLERemoteCharacteristic*, uint8_t* data, size_t len,
                    bool /*isNotify*/) {
  RawNotification n;
  n.len = (uint8_t)(len > sizeof(n.data) ? sizeof(n.data) : len);
  memcpy(n.data, data, n.len);
  // Never block the BLE task: if the main loop is behind, drop the oldest.
  if (xQueueSend(g_queue, &n, 0) != pdTRUE) {
    RawNotification dropped;
    xQueueReceive(g_queue, &dropped, 0);
    xQueueSend(g_queue, &n, 0);
  }
}

void resetAms() {
  g_ready = false;
  g_client = nullptr;
  g_remoteCommand = nullptr;
  g_ancsControl = nullptr;
  g_amsHandle = BLE_HS_CONN_HANDLE_NONE;
}

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer*, NimBLEConnInfo& info) override {
    const uint16_t h = info.getConnHandle();
    portENTER_CRITICAL(&g_linkLock);
    if (Link* free = findLink(BLE_HS_CONN_HANDLE_NONE)) {
      *free = Link{};
      free->handle = h;
      free->since = millis();
    }
    portEXIT_CRITICAL(&g_linkLock);
    Serial.printf("ble: phone connected (link %u)\n", h);
    // AMS is only readable on an encrypted link; ask the phone to pair (or to
    // re-encrypt with the stored bond on later connections).
    NimBLEDevice::startSecurity(h);
    // Keep advertising while connected, so another phone can always find and
    // pair with the knob (and the iPhone app can scan for it).
    NimBLEDevice::getAdvertising()->start();
  }

  void onDisconnect(NimBLEServer*, NimBLEConnInfo& info, int reason) override {
    const uint16_t h = info.getConnHandle();
    Serial.printf("ble: phone disconnected (link %u, reason %d)\n", h, reason);
    portENTER_CRITICAL(&g_linkLock);
    if (Link* l = findLink(h)) *l = Link{};
    portEXIT_CRITICAL(&g_linkLock);
    if (h == g_amsHandle) resetAms();
    if (h == g_setupHandle) g_setupHandle = BLE_HS_CONN_HANDLE_NONE;
    // Explicitly, rather than trusting advertise-on-disconnect: a lost link
    // must always leave the knob findable.
    NimBLEDevice::getAdvertising()->start();
  }

  void onAuthenticationComplete(NimBLEConnInfo& info) override {
    const uint16_t h = info.getConnHandle();
    Link* l = findLink(h);
    if (!info.isEncrypted()) {
      Serial.println("ble: pairing failed or was refused, dropping the link");
      if (l) l->drop = true;  // From poll(): never disconnect inside a callback.
      return;
    }
    if (l) l->encrypted = true;
    Serial.printf("ble: encrypted (link %u, bonded=%d)\n", h, info.isBonded());
    // iOS caches a bonded accessory's services and never looks again on its
    // own; announcing a change over the whole handle range on every
    // encrypted connection makes it re-read them. One short indication.
    ble_svc_gatt_changed(0x0001, 0xFFFF);
    // Music set-up on this link, unless an iPhone already has it.
    if (g_amsHandle == BLE_HS_CONN_HANDLE_NONE && g_setupHandle == BLE_HS_CONN_HANDLE_NONE) {
      g_setupHandle = h;
    }
  }
};

// 128-bit UUID in the little-endian byte order advertising data uses.
void appendSolicitation(NimBLEAdvertisementData* adv) {
  const NimBLEUUID uuid(ams::kServiceUuid);
  const uint8_t* le = uuid.getValue();  // NimBLE stores UUIDs little-endian.
  uint8_t field[2 + 16];
  field[0] = 17;    // Length: type + 16 bytes.
  field[1] = 0x15;  // List of 128-bit Service Solicitation UUIDs.
  memcpy(field + 2, le, 16);
  adv->addData(field, sizeof(field));
}

void queueAncs(uint8_t kind, const uint8_t* data, size_t len) {
  if (!g_ancsQueue) return;
  AncsPacket p;
  p.kind = kind;
  p.len = (uint8_t)(len > sizeof(p.data) ? sizeof(p.data) : len);
  memcpy(p.data, data, p.len);
  xQueueSend(g_ancsQueue, &p, 0);  // Full: dropped; the next event re-syncs.
}

void onAncsSource(NimBLERemoteCharacteristic*, uint8_t* d, size_t len, bool) { queueAncs(0, d, len); }
void onAncsData(NimBLERemoteCharacteristic*, uint8_t* d, size_t len, bool) { queueAncs(1, d, len); }

// The iPhone's notifications, on the same link as the music. The first time,
// iOS asks whether to share notifications with the knob.
void setupAncs() {
  g_ancsControl = nullptr;
  if (!g_client) return;
  NimBLERemoteService* service = g_client->getService(ancs::kServiceUuid);
  if (!service) {
    Serial.println("ble: no notification service (ANCS)");
    return;
  }
  NimBLERemoteCharacteristic* source = service->getCharacteristic(ancs::kNotificationSourceUuid);
  NimBLERemoteCharacteristic* data = service->getCharacteristic(ancs::kDataSourceUuid);
  NimBLERemoteCharacteristic* control = service->getCharacteristic(ancs::kControlPointUuid);
  if (!source || !data || !control) {
    Serial.println("ble: ANCS characteristics missing");
    return;
  }
  // Data Source first: the existing notifications come in as soon as the
  // source is subscribed, and their replies must have somewhere to land.
  if (!data->subscribe(true, onAncsData, true) || !source->subscribe(true, onAncsSource, true)) {
    Serial.println("ble: ANCS subscribe failed (notifications not allowed?)");
    return;
  }
  g_ancsControl = control;
  Serial.println("ble: notifications ready");
}

// 0 = AMS is up on this link; 1 = this phone has no AMS (not an iPhone);
// 2 = it failed in a way worth retrying.
int setupAms(uint16_t handle) {
  NimBLEConnInfo info = g_server->getPeerInfoByHandle(handle);
  g_client = g_server->getClient(info);
  if (!g_client) {
    Serial.println("ble: no client for connection");
    return 2;
  }

  // Always discover afresh: handles cached from an earlier connection are
  // wrong after the phone re-pairs, and subscribing to them fails silently.
  g_client->deleteServices();
  NimBLERemoteService* service = g_client->getService(ams::kServiceUuid);
  if (!service) {
    Serial.println("ble: this phone has no AMS (not an iPhone): media remote only");
    g_client = nullptr;
    return 1;
  }
  NimBLERemoteCharacteristic* entityUpdate =
      service->getCharacteristic(ams::kEntityUpdateUuid);
  g_remoteCommand = service->getCharacteristic(ams::kRemoteCommandUuid);
  if (!entityUpdate || !g_remoteCommand) {
    Serial.println("ble: AMS characteristics missing");
    return 2;
  }

  if (!entityUpdate->subscribe(true, onEntityUpdate, true)) {
    Serial.println("ble: subscribe to entity updates failed");
    return 2;
  }
  const auto player = ams::playerSubscription();
  const auto track = ams::trackSubscription();
  // Writes with response: AMS answers with an error if it rejects one, which
  // NimBLE turns into false here.
  if (!entityUpdate->writeValue(player.data(), player.size(), true) ||
      !entityUpdate->writeValue(track.data(), track.size(), true)) {
    Serial.println("ble: AMS refused the subscription");
    return 2;
  }
  g_amsHandle = handle;
  g_ready = true;
  Serial.printf("ble: AMS ready (link %u)\n", handle);
  setupAncs();
  return 0;
}

}  // namespace

void begin(const char* deviceName, uint8_t mode, knob::CoverStyle style) {
  g_queue = xQueueCreate(8, sizeof(RawNotification));
  g_ancsQueue = xQueueCreate(12, sizeof(AncsPacket));

  NimBLEDevice::init(deviceName);
  // Just Works bonding: the knob has no screen keyboard, so iOS shows a plain
  // "Pair" prompt. Secure Connections on; bonds persist in NVS.
  NimBLEDevice::setSecurityAuth(/*bonding=*/true, /*mitm=*/false, /*sc=*/true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);

  g_server = NimBLEDevice::createServer();
  g_server->setCallbacks(new ServerCallbacks());
  g_server->advertiseOnDisconnect(true);
  knob_service::attach(g_server, mode, style, settings_store::current());
  const bool remote = settings_store::current().mediaRemote;
  if (remote) hid_remote::begin(g_server);

  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  NimBLEAdvertisementData adv;
  adv.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
  appendSolicitation(&adv);
  if (remote) {
    // A media remote: the HID service and the "HID" appearance are what get
    // Android to list it as an input device it can pair from its settings.
    // They take the room the short name had; the full name is in the scan
    // response either way.
    adv.setAppearance(0x03C0);  // Generic HID.
    adv.addServiceUUID(NimBLEUUID((uint16_t)0x1812));
  } else {
    adv.setName("Knob");  // Short: flags + solicitation leave 10 bytes.
  }
  // The scan response carries the full name and the Knob service, which is
  // how the app recognises a knob when it has to scan for one.
  NimBLEAdvertisementData scan;
  scan.setName(deviceName);
  scan.addServiceUUID(NimBLEUUID(knob::kKnobServiceUuid));
  advertising->setAdvertisementData(adv);
  advertising->setScanResponseData(scan);
  advertising->start();
  Serial.println("ble: advertising");
}

void poll() {
  // Music set-up, one link at a time, in the main loop (it blocks on GATT).
  const uint16_t h = g_setupHandle;
  if (h != BLE_HS_CONN_HANDLE_NONE) {
    g_setupHandle = BLE_HS_CONN_HANDLE_NONE;
    Link* l = findLink(h);
    if (l && g_amsHandle == BLE_HS_CONN_HANDLE_NONE) {
      const int r = setupAms(h);
      if (r == 1) {
        l->noAms = true;
      } else if (r == 2 && ++l->tries < 3) {
        g_setupHandle = h;  // iOS is sometimes still settling the bond.
      }
    }
  }
  // No AMS link (the iPhone left, or set-up ran on an Android first): try
  // any other paired link that might be an iPhone.
  if (g_amsHandle == BLE_HS_CONN_HANDLE_NONE && g_setupHandle == BLE_HS_CONN_HANDLE_NONE) {
    for (Link& l : g_links) {
      if (l.handle != BLE_HS_CONN_HANDLE_NONE && l.encrypted && !l.noAms && l.tries < 3) {
        g_setupHandle = l.handle;
        break;
      }
    }
  }

  // Links that failed to pair, or never encrypted, are dropped so a clean
  // retry can start.
  if (g_server) {
    for (Link& l : g_links) {
      if (l.handle == BLE_HS_CONN_HANDLE_NONE) continue;
      const bool stale = !l.encrypted && millis() - l.since > kEncryptTimeoutMs;
      if (l.drop || stale) {
        Serial.printf("ble: dropping link %u (%s)\n", l.handle, stale ? "never encrypted" : "pairing failed");
        l.drop = false;
        l.since = millis();  // One attempt per timeout, not a flood.
        g_server->disconnect(l.handle);
      }
    }
  }

  // Watchdog: whatever happened, the knob stays findable.
  if (g_server && millis() - g_lastAdvCheckMs > 5000) {
    g_lastAdvCheckMs = millis();
    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    if (!adv->isAdvertising()) {
      Serial.println("ble: advertising had stopped, restarting");
      adv->start();
    }
  }
}

bool ready() { return g_ready; }

bool send(ams::Command cmd) {
  if (!g_ready || !g_remoteCommand) return false;
  const uint8_t byte = (uint8_t)cmd;
  return g_remoteCommand->writeValue(&byte, 1, true);
}

bool nextAncs(uint8_t* kind, std::vector<uint8_t>* bytes) {
  if (!g_ancsQueue) return false;
  AncsPacket p;
  if (xQueueReceive(g_ancsQueue, &p, 0) != pdTRUE) return false;
  *kind = p.kind;
  bytes->assign(p.data, p.data + p.len);
  return true;
}

bool ancsCommand(const std::vector<uint8_t>& cmd) {
  if (!g_ancsControl || g_amsHandle == BLE_HS_CONN_HANDLE_NONE) return false;
  return g_ancsControl->writeValue(cmd.data(), cmd.size(), true);
}

bool ancsReady() { return g_ancsControl != nullptr; }

bool nextUpdate(ams::Update* out) {
  if (!g_queue) return false;
  RawNotification n;
  while (xQueueReceive(g_queue, &n, 0) == pdTRUE) {
    if (ams::parseUpdate(n.data, n.len, out)) return true;
  }
  return false;
}

void shutdown() {
  NimBLEDevice::getAdvertising()->stop();
  if (g_server) {
    for (Link& l : g_links)
      if (l.handle != BLE_HS_CONN_HANDLE_NONE) g_server->disconnect(l.handle);
  }
  g_ready = false;
}

}  // namespace ams_client
