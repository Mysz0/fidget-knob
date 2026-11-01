#pragma once

#include <stdint.h>

// Fast WiFi reconnect across deep sleep.
//
// The original power plan was to never disconnect at all: keep the association
// alive through automatic light sleep, so a twist reaches the hub without any
// reconnect. That turns out to be impossible on the stock Arduino framework -
// its precompiled core is built with CONFIG_PM_ENABLE off, so esp_pm_configure()
// returns ESP_ERR_NOT_SUPPORTED and automatic light sleep never engages. There
// is no runtime workaround; it is a compile-time option of a binary we do not
// build.
//
// So instead of avoiding the reconnect, make it cheap. A cold WiFi.begin() costs
// roughly 1.5-2.5 s, and almost all of that is discovery rather than the actual
// association:
//
//   full channel scan   ~600-1200 ms   avoided by remembering channel + BSSID
//   DHCP DISCOVER/OFFER ~200-800 ms    avoided by reusing the previous lease
//   auth + assoc        ~100-200 ms    unavoidable
//
// Remembering the first two in RTC memory - which survives deep sleep - gets a
// warm reconnect down to roughly 300-600 ms. Not the sub-100 ms that a live
// association would have given, but far short of the 2 s that made deep sleep
// unacceptable in the first place.

namespace wifi_fast {

// Connects, using the remembered channel, BSSID and address if they are valid.
// Falls back to a full scan and DHCP automatically if the fast path fails, so a
// moved access point or an expired lease costs one slow connect, not a failure.
//
// Returns true if connected within `timeoutMs`.
bool connect(const char* ssid, const char* password, uint32_t timeoutMs);

// True if the last connect() used the remembered parameters rather than a full
// scan. Useful for logging how often the fast path actually pays off.
bool usedFastPath();

// Milliseconds the last connect() took.
uint32_t lastConnectMs();

// Forgets the cached parameters, forcing a full scan and DHCP next time.
void forget();

}  // namespace wifi_fast
