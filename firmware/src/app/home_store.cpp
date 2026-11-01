#include "home_store.h"

#include <Preferences.h>

#include "config.h"

namespace home_store {
namespace {

std::string g_ssid, g_password, g_host, g_token, g_cert, g_lamp, g_cycle;
// The last network that actually connected, to fall back to.
std::string g_goodSsid, g_goodPassword;

std::string read(Preferences& p, const char* key) {
  if (!p.isKey(key)) return "";
  return std::string(p.getString(key, "").c_str());
}

const char* orDefault(const std::string& v, const char* fallback) {
  return v.empty() ? fallback : v.c_str();
}

}  // namespace

void load() {
  Preferences p;
  if (!p.begin("home", /*readOnly=*/true)) return;  // Never set up.
  g_ssid = read(p, "ssid");
  g_password = read(p, "pass");
  g_host = read(p, "host");
  g_token = read(p, "token");
  g_cert = read(p, "cert");
  g_lamp = read(p, "lamp");
  g_cycle = read(p, "cycle");
  g_goodSsid = read(p, "goodSsid");
  g_goodPassword = read(p, "goodPass");
  p.end();
}

void save(const knob::HomeConfig& c) {
  Preferences p;
  p.begin("home", /*readOnly=*/false);
  auto put = [&](knob::HomeConfig::Key k, const char* key, const std::string& v,
                 std::string* mirror) {
    if (!c.has(k)) return;
    p.putString(key, v.c_str());
    *mirror = v;
  };
  put(knob::HomeConfig::kSsid, "ssid", c.ssid, &g_ssid);
  put(knob::HomeConfig::kPassword, "pass", c.password, &g_password);
  put(knob::HomeConfig::kHubHost, "host", c.hubHost, &g_host);
  put(knob::HomeConfig::kHubToken, "token", c.hubToken, &g_token);
  put(knob::HomeConfig::kHubCert, "cert", c.hubCertPem, &g_cert);
  put(knob::HomeConfig::kLampId, "lamp", c.lampId, &g_lamp);
  put(knob::HomeConfig::kLampCycle, "cycle", c.lampCycle, &g_cycle);
  p.end();
}

void markWifiGood() {
  if (g_goodSsid == ssid() && g_goodPassword == password()) return;
  g_goodSsid = ssid();
  g_goodPassword = password();
  Preferences p;
  p.begin("home", /*readOnly=*/false);
  p.putString("goodSsid", g_goodSsid.c_str());
  p.putString("goodPass", g_goodPassword.c_str());
  p.end();
}

bool revertWifi() {
  if (g_goodSsid.empty() || (g_goodSsid == ssid() && g_goodPassword == password())) {
    return false;
  }
  knob::HomeConfig c;
  c.ssid = g_goodSsid;
  c.password = g_goodPassword;
  c.present = (1u << knob::HomeConfig::kSsid) | (1u << knob::HomeConfig::kPassword);
  save(c);
  return true;
}

const char* ssid() { return orDefault(g_ssid, WIFI_SSID); }
const char* password() { return orDefault(g_password, WIFI_PASSWORD); }
const char* hubHost() { return orDefault(g_host, DIRIGERA_HOST); }
const char* hubToken() { return orDefault(g_token, DIRIGERA_TOKEN); }
const char* hubCertPem() { return orDefault(g_cert, DIRIGERA_CERT_PEM); }
uint16_t hubPort() { return DIRIGERA_PORT; }
const char* lampId() { return g_lamp.c_str(); }
const char* lampCycle() { return g_cycle.c_str(); }

}  // namespace home_store
