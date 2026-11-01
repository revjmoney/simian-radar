#include <unity.h>
#include "Config.h"

void setUp() {}
void tearDown() {}

void test_alert_radius_is_1000ft() { TEST_ASSERT_EQUAL_FLOAT(304.8f, kAlertRadiusM); }

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_alert_radius_is_1000ft);
  return UNITY_END();
}
