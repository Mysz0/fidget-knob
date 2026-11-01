#include <unity.h>

#include "ascii_fold.h"

using knob::foldToAscii;

void setUp() {}
void tearDown() {}

void test_plain_ascii_passes_through() {
  TEST_ASSERT_EQUAL_STRING("Blinding Lights", foldToAscii("Blinding Lights", 40).c_str());
}

void test_polish_letters_fold() {
  TEST_ASSERT_EQUAL_STRING("Zolc i Lodz", foldToAscii("Żółć i Łódź", 40).c_str());
}

void test_western_accents_fold() {
  TEST_ASSERT_EQUAL_STRING("Madchen Cafe Strasse",
                           foldToAscii("Mädchen Café Straße", 40).c_str());
}

void test_emoji_and_cjk_are_dropped() {
  TEST_ASSERT_EQUAL_STRING("Song", foldToAscii("Song \xF0\x9F\x94\xA5", 40).c_str());
  TEST_ASSERT_EQUAL_STRING("", foldToAscii("\xE6\x9D\xB1\xE4\xBA\xAC", 40).c_str());
}

void test_long_text_is_cut_with_ellipsis() {
  TEST_ASSERT_EQUAL_STRING("Abcdefg...", foldToAscii("Abcdefghijklmnop", 10).c_str());
}

void test_whitespace_collapses_and_trims() {
  TEST_ASSERT_EQUAL_STRING("A B", foldToAscii("  A \xF0\x9F\x94\xA5  B ", 40).c_str());
}

void test_invalid_utf8_does_not_crash() {
  TEST_ASSERT_EQUAL_STRING("ab", foldToAscii(std::string("a\xC3") + "b", 40).c_str());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_plain_ascii_passes_through);
  RUN_TEST(test_polish_letters_fold);
  RUN_TEST(test_western_accents_fold);
  RUN_TEST(test_emoji_and_cjk_are_dropped);
  RUN_TEST(test_long_text_is_cut_with_ellipsis);
  RUN_TEST(test_whitespace_collapses_and_trims);
  RUN_TEST(test_invalid_utf8_does_not_crash);
  return UNITY_END();
}
