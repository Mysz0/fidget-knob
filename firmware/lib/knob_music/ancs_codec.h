#pragma once

#include <stdint.h>
#include <stddef.h>

#include <string>
#include <vector>

// Apple Notification Center Service: the byte formats, with no Bluetooth.
// The iPhone tells the knob a notification exists (Notification Source,
// 8 bytes); the knob asks for its text (Control Point); the text arrives on
// Data Source, possibly split over several packets.
namespace ancs {

constexpr const char* kServiceUuid = "7905F431-B5CE-4E99-A40F-4B1E122D00D0";
constexpr const char* kNotificationSourceUuid = "9FBF120D-6301-42D9-8C58-25E699A21DBD";
constexpr const char* kControlPointUuid = "69D1D8F3-45E1-49A8-9821-9BBDFDAAD9D9";
constexpr const char* kDataSourceUuid = "22EAC6E9-24D6-4BB5-BE44-B36ACE7C7BFB";

enum class Event : uint8_t { Added = 0, Modified = 1, Removed = 2 };
constexpr uint8_t kFlagSilent = 1 << 0;
constexpr uint8_t kFlagPreExisting = 1 << 2;

struct SourceEvent {
  Event event;
  uint8_t flags;
  uint8_t category;  // 1 incoming call, 4 social, 6 email...
  uint32_t uid;
};
bool parseSource(const uint8_t* d, size_t len, SourceEvent* out);

// "Get Notification Attributes" for the app id, title and message.
std::vector<uint8_t> getAttributes(uint32_t uid, uint16_t maxTitle, uint16_t maxMessage);
// "Get App Attributes": the app's display name ("Messages").
std::vector<uint8_t> getAppName(const std::string& appId);
// "Perform Notification Action": negative dismisses it on the phone.
std::vector<uint8_t> performAction(uint32_t uid, bool positive);

struct Notification {
  uint32_t uid = 0;
  std::string appId, title, message;
};

// Reassembles Data Source packets. feed() returns true once a reply is whole;
// then notification() (command 0) or appName() (command 1) holds it.
class Assembler {
 public:
  bool feed(const uint8_t* d, size_t len);
  bool isAppReply() const { return command_ == 1; }
  const Notification& notification() const { return n_; }
  const std::string& appId() const { return appId_; }
  const std::string& appName() const { return appName_; }
  void reset();

 private:
  bool tryParse();
  std::vector<uint8_t> buf_;
  uint8_t command_ = 0xFF;
  Notification n_;
  std::string appId_, appName_;
};

}  // namespace ancs
