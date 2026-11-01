#include <unity.h>

#include "ancs_codec.h"

void setUp() {}
void tearDown() {}

void test_source() {
  const uint8_t d[] = {0, ancs::kFlagPreExisting, 4, 1, 0x78, 0x56, 0x34, 0x12};
  ancs::SourceEvent e;
  TEST_ASSERT_TRUE(ancs::parseSource(d, sizeof d, &e));
  TEST_ASSERT_EQUAL(ancs::Event::Added, e.event);
  TEST_ASSERT_EQUAL_HEX32(0x12345678, e.uid);
  TEST_ASSERT_TRUE(e.flags & ancs::kFlagPreExisting);
  TEST_ASSERT_FALSE(ancs::parseSource(d, 7, &e));
}

void test_requests() {
  auto a = ancs::getAttributes(0x01020304, 32, 100);
  const uint8_t want[] = {0, 4, 3, 2, 1, 0, 1, 32, 0, 3, 100, 0};
  TEST_ASSERT_EQUAL(sizeof want, a.size());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(want, a.data(), sizeof want);
  auto p = ancs::performAction(7, false);
  TEST_ASSERT_EQUAL(6, p.size());
  TEST_ASSERT_EQUAL_UINT8(1, p[5]);
  auto n = ancs::getAppName("com.apple.MobileSMS");
  TEST_ASSERT_EQUAL(1 + 19 + 1 + 1, n.size());
}

void test_reply_in_fragments() {
  // [0][uid=9][0 len3 "app"][1 len2 "Hi"][3 len5 "there"]
  const uint8_t full[] = {0, 9, 0, 0, 0, 0, 3, 0, 'a', 'p', 'p', 1, 2, 0, 'H', 'i',
                          3, 5, 0, 't', 'h', 'e', 'r', 'e'};
  ancs::Assembler a;
  TEST_ASSERT_FALSE(a.feed(full, 10));
  TEST_ASSERT_FALSE(a.feed(full + 10, 8));
  TEST_ASSERT_TRUE(a.feed(full + 18, sizeof full - 18));
  TEST_ASSERT_EQUAL_UINT32(9, a.notification().uid);
  TEST_ASSERT_EQUAL_STRING("app", a.notification().appId.c_str());
  TEST_ASSERT_EQUAL_STRING("Hi", a.notification().title.c_str());
  TEST_ASSERT_EQUAL_STRING("there", a.notification().message.c_str());
  TEST_ASSERT_FALSE(a.isAppReply());
}

void test_app_name_reply() {
  const uint8_t r[] = {1, 'a', 'b', 0, 0, 4, 0, 'N', 'a', 'm', 'e'};
  ancs::Assembler a;
  TEST_ASSERT_TRUE(a.feed(r, sizeof r));
  TEST_ASSERT_TRUE(a.isAppReply());
  TEST_ASSERT_EQUAL_STRING("ab", a.appId().c_str());
  TEST_ASSERT_EQUAL_STRING("Name", a.appName().c_str());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_source);
  RUN_TEST(test_requests);
  RUN_TEST(test_reply_in_fragments);
  RUN_TEST(test_app_name_reply);
  return UNITY_END();
}
