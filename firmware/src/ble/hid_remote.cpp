#include "hid_remote.h"

#include <Arduino.h>
#include <NimBLEHIDDevice.h>

#include <vector>

namespace hid_remote {
namespace {

NimBLEHIDDevice* g_hid = nullptr;
NimBLECharacteristic* g_input = nullptr;
NimBLEServer* g_server = nullptr;
// Connections that asked for key presses (enabled notifications).
std::vector<uint16_t> g_listeners;

class InputCallbacks : public NimBLECharacteristicCallbacks {
  void onSubscribe(NimBLECharacteristic*, NimBLEConnInfo& info, uint16_t subValue) override {
    const uint16_t h = info.getConnHandle();
    for (size_t i = 0; i < g_listeners.size(); ++i) {
      if (g_listeners[i] == h) g_listeners.erase(g_listeners.begin() + i);
    }
    if (subValue) g_listeners.push_back(h);
    Serial.printf("ble: media remote %s\n", subValue ? "in use" : "released");
  }
};

// One 8-bit report: a bit per media key.
uint8_t kReportMap[] = {
    0x05, 0x0C,  // Usage Page (Consumer)
    0x09, 0x01,  // Usage (Consumer Control)
    0xA1, 0x01,  // Collection (Application)
    0x85, 0x01,  //   Report ID (1)
    0x15, 0x00,  //   Logical Minimum (0)
    0x25, 0x01,  //   Logical Maximum (1)
    0x75, 0x01,  //   Report Size (1)
    0x95, 0x08,  //   Report Count (8)
    0x09, 0xCD,  //   Usage (Play/Pause)        bit 0
    0x09, 0xB5,  //   Usage (Scan Next Track)   bit 1
    0x09, 0xB6,  //   Usage (Scan Prev Track)   bit 2
    0x09, 0xE9,  //   Usage (Volume Up)         bit 3
    0x09, 0xEA,  //   Usage (Volume Down)       bit 4
    0x09, 0xE2,  //   Usage (Mute)              bit 5
    0x09, 0xB7,  //   Usage (Stop)              bit 6
    0x09, 0x00,  //   Usage (unassigned)        bit 7
    0x81, 0x02,  //   Input (Data, Variable, Absolute)
    0xC0,        // End Collection
};

uint8_t bitFor(ams::Command cmd) {
  switch (cmd) {
    case ams::Command::TogglePlayPause: return 1 << 0;
    case ams::Command::NextTrack: return 1 << 1;
    case ams::Command::PreviousTrack: return 1 << 2;
    case ams::Command::VolumeUp: return 1 << 3;
    case ams::Command::VolumeDown: return 1 << 4;
    default: return 0;
  }
}

}  // namespace

void begin(NimBLEServer* server) {
  if (g_hid) return;
  g_server = server;
  g_hid = new NimBLEHIDDevice(server);
  g_hid->setManufacturer("Fidget Knob");
  g_hid->setPnp(0x02, 0x303A, 0x4B4E, 0x0100);  // USB-IF ids: Espressif, "KN".
  g_hid->setHidInfo(0x00, 0x01);
  g_hid->setReportMap(kReportMap, sizeof(kReportMap));
  g_input = g_hid->getInputReport(1);
  g_input->setCallbacks(new InputCallbacks());
  g_hid->setBatteryLevel(100);
  Serial.println("ble: media remote (HID) on");
}

bool enabled() { return g_hid != nullptr; }

bool ready() {
  if (!g_input || !g_server || g_listeners.empty()) return false;
  // Drop listeners whose link has gone.
  const auto peers = g_server->getPeerDevices();
  for (size_t i = g_listeners.size(); i-- > 0;) {
    bool live = false;
    for (uint16_t p : peers) live |= p == g_listeners[i];
    if (!live) g_listeners.erase(g_listeners.begin() + i);
  }
  return !g_listeners.empty();
}

bool send(ams::Command cmd) {
  const uint8_t bit = bitFor(cmd);
  if (!ready() || !bit) return false;
  uint8_t report = bit;
  g_input->setValue(&report, 1);
  g_input->notify();
  delay(8);  // A press the phone can see before the release.
  report = 0;
  g_input->setValue(&report, 1);
  g_input->notify();
  return true;
}

}  // namespace hid_remote
