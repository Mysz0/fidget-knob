#include "wifi_fast.h"

#include <Arduino.h>
#include <WiFi.h>

namespace wifi_fast {
namespace {

// Cached association parameters. RTC_DATA_ATTR puts these in RTC slow memory,
// which is one of the few things that survives deep sleep.
RTC_DATA_ATTR struct {
  uint32_t magic;
  uint8_t bssid[6];
  int32_t channel;
  uint32_t ip;
  uint32_t gateway;
  uint32_t subnet;
  uint32_t dns;
  uint16_t wakeCount;
} g_cache;

// Bumped whenever the struct layout changes, so a stale cache from older
// firmware is discarded rather than misread.
constexpr uint32_t kMagic = 0x4B4E4231;  // "KNB1"

// Re-run DHCP every so often instead of reusing the address forever. The lease
// we captured will eventually expire, and squatting on it after the router has
// handed it to someone else causes an address conflict that is very annoying to
// diagnose. Renewing periodically keeps us honest at the cost of one slow
// connect in every N.
constexpr uint16_t kRenewEveryWakes = 32;

// How long to give the fast path before falling back. Generous enough to cover
// a slow AP response, short enough that the fallback still beats waiting.
constexpr uint32_t kFastPathBudgetMs = 1500;

bool g_usedFastPath = false;
uint32_t g_lastConnectMs = 0;

bool cacheValid() {
  return g_cache.magic == kMagic && g_cache.channel > 0 && g_cache.ip != 0;
}

void storeCache() {
  const uint8_t* bssid = WiFi.BSSID();
  if (!bssid) return;

  memcpy(g_cache.bssid, bssid, 6);
  g_cache.channel = WiFi.channel();
  g_cache.ip = (uint32_t)WiFi.localIP();
  g_cache.gateway = (uint32_t)WiFi.gatewayIP();
  g_cache.subnet = (uint32_t)WiFi.subnetMask();
  g_cache.dns = (uint32_t)WiFi.dnsIP();
  g_cache.magic = kMagic;
}

bool waitForConnection(uint32_t timeoutMs) {
  const uint32_t deadline = millis() + timeoutMs;
  while (millis() < deadline) {
    if (WiFi.status() == WL_CONNECTED) return true;
    // Yielding rather than spinning lets the WiFi task actually run.
    delay(10);
  }
  return false;
}

}  // namespace

bool connect(const char* ssid, const char* password, uint32_t timeoutMs) {
  const uint32_t started = millis();
  g_usedFastPath = false;

  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);  // Do not wear out flash rewriting credentials.
  WiFi.setAutoReconnect(true);

  ++g_cache.wakeCount;
  const bool renewDue = (g_cache.wakeCount % kRenewEveryWakes) == 0;

  if (cacheValid() && !renewDue) {
    g_usedFastPath = true;

    // Reusing the previous lease skips DHCP entirely.
    WiFi.config(IPAddress(g_cache.ip), IPAddress(g_cache.gateway),
                IPAddress(g_cache.subnet), IPAddress(g_cache.dns));

    // Naming the channel and BSSID skips the full-band scan, which is the
    // single largest component of a cold connect.
    WiFi.begin(ssid, password, g_cache.channel, g_cache.bssid, true);

    if (waitForConnection(min(timeoutMs, kFastPathBudgetMs))) {
      g_lastConnectMs = millis() - started;
      return true;
    }

    // The AP moved channel, the BSSID changed, or the lease is gone. Drop
    // everything and do it the slow way rather than retrying a stale guess.
    WiFi.disconnect(true, true);
    delay(50);
    g_usedFastPath = false;
  }

  // Full path: DHCP and a complete scan.
  //
  // Passing all-zero addresses to config() is how this Arduino core is told to
  // go back to DHCP after a static address has been set.
  WiFi.config(IPAddress((uint32_t)0), IPAddress((uint32_t)0),
              IPAddress((uint32_t)0), IPAddress((uint32_t)0));
  WiFi.begin(ssid, password);

  const uint32_t remaining =
      timeoutMs > (millis() - started) ? timeoutMs - (millis() - started) : 0;

  if (!waitForConnection(remaining)) {
    g_lastConnectMs = millis() - started;
    return false;
  }

  storeCache();
  g_lastConnectMs = millis() - started;
  return true;
}

bool usedFastPath() { return g_usedFastPath; }

uint32_t lastConnectMs() { return g_lastConnectMs; }

void forget() { g_cache.magic = 0; }

}  // namespace wifi_fast
