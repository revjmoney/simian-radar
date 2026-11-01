#include <unity.h>
#include <string.h>
#include "Geo.h"

void setUp() {}
void tearDown() {}

void test_haversine_myrtle_to_calabash() {
  double d = haversineM(33.6891, -78.8867, 33.893, -78.465);
  TEST_ASSERT_FLOAT_WITHIN(450.0, 45000.0, d);
}
void test_bearing_due_east() { TEST_ASSERT_FLOAT_WITHIN(0.5, 90.0, bearingDeg(0, 0, 0, 1)); }
void test_relative_arrow_wraps_forward() { TEST_ASSERT_EQUAL_INT(20, relativeArrowDeg(10, 350)); }
void test_relative_arrow_wraps_back() { TEST_ASSERT_EQUAL_INT(340, relativeArrowDeg(350, 10)); }
void test_compass8() {
  TEST_ASSERT_EQUAL_STRING("N", compass8(22.4));
  TEST_ASSERT_EQUAL_STRING("NE", compass8(22.6));
  TEST_ASSERT_EQUAL_STRING("N", compass8(359));
}
void test_format_feet() { char b[8]; formatDistance(152.4, b, sizeof b); TEST_ASSERT_EQUAL_STRING("500ft", b); }
void test_format_tenths_mile() { char b[8]; formatDistance(643.7, b, sizeof b); TEST_ASSERT_EQUAL_STRING("0.4mi", b); }
void test_format_whole_miles() { char b[8]; formatDistance(19312, b, sizeof b); TEST_ASSERT_EQUAL_STRING("12mi", b); }

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_haversine_myrtle_to_calabash);
  RUN_TEST(test_bearing_due_east);
  RUN_TEST(test_relative_arrow_wraps_forward);
  RUN_TEST(test_relative_arrow_wraps_back);
  RUN_TEST(test_compass8);
  RUN_TEST(test_format_feet);
  RUN_TEST(test_format_tenths_mile);
  RUN_TEST(test_format_whole_miles);
  return UNITY_END();
}
