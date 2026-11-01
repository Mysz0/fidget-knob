#include "ancs_codec.h"

namespace ancs {
namespace {
void put32(std::vector<uint8_t>* v, uint32_t x) {
  for (int i = 0; i < 4; ++i) v->push_back((uint8_t)(x >> (8 * i)));
}
void put16(std::vector<uint8_t>* v, uint16_t x) {
  v->push_back((uint8_t)x);
  v->push_back((uint8_t)(x >> 8));
}
uint32_t get32(const uint8_t* d) {
  return (uint32_t)d[0] | ((uint32_t)d[1] << 8) | ((uint32_t)d[2] << 16) | ((uint32_t)d[3] << 24);
}
}  // namespace

bool parseSource(const uint8_t* d, size_t len, SourceEvent* out) {
  if (!d || len < 8 || d[0] > 2) return false;
  out->event = (Event)d[0];
  out->flags = d[1];
  out->category = d[2];
  out->uid = get32(d + 4);
  return true;
}

std::vector<uint8_t> getAttributes(uint32_t uid, uint16_t maxTitle, uint16_t maxMessage) {
  std::vector<uint8_t> v{0};
  put32(&v, uid);
  v.push_back(0);  // App identifier (no length).
  v.push_back(1);  // Title.
  put16(&v, maxTitle);
  v.push_back(3);  // Message.
  put16(&v, maxMessage);
  return v;
}

std::vector<uint8_t> getAppName(const std::string& appId) {
  std::vector<uint8_t> v{1};
  v.insert(v.end(), appId.begin(), appId.end());
  v.push_back(0);
  v.push_back(0);  // Display name.
  return v;
}

std::vector<uint8_t> performAction(uint32_t uid, bool positive) {
  std::vector<uint8_t> v{2};
  put32(&v, uid);
  v.push_back(positive ? 0 : 1);
  return v;
}

void Assembler::reset() {
  buf_.clear();
  command_ = 0xFF;
}

bool Assembler::feed(const uint8_t* d, size_t len) {
  if (!d || len == 0) return false;
  // A packet starting a new reply while one is half done: the old one is lost.
  if (buf_.empty()) command_ = d[0];
  buf_.insert(buf_.end(), d, d + len);
  if (buf_.size() > 1024) {  // Never grow without bound on garbage.
    reset();
    return false;
  }
  if (!tryParse()) return false;
  buf_.clear();
  return true;
}

// Whole once every attribute asked for has arrived in full.
bool Assembler::tryParse() {
  const uint8_t* d = buf_.data();
  const size_t n = buf_.size();
  size_t i = 0;
  if (command_ == 0) {
    if (n < 5) return false;
    Notification out;
    out.uid = get32(d + 1);
    i = 5;
    int got = 0;
    while (got < 3) {
      if (i + 3 > n) return false;
      const uint8_t id = d[i];
      const uint16_t l = d[i + 1] | (d[i + 2] << 8);
      if (i + 3 + l > n) return false;
      const std::string value((const char*)d + i + 3, l);
      if (id == 0) out.appId = value;
      else if (id == 1) out.title = value;
      else if (id == 3) out.message = value;
      i += 3 + l;
      ++got;
    }
    n_ = out;
    return true;
  }
  if (command_ == 1) {
    // [1][app id ... 0][0][len16][name]
    size_t z = 1;
    while (z < n && d[z] != 0) ++z;
    if (z + 4 > n) return false;
    const uint16_t l = d[z + 2] | (d[z + 3] << 8);
    if (z + 4 + l > n) return false;
    appId_.assign((const char*)d + 1, z - 1);
    appName_.assign((const char*)d + z + 4, l);
    return true;
  }
  reset();  // Unknown reply: drop it.
  return false;
}

}  // namespace ancs
