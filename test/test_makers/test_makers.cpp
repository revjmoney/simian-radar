#include <unity.h>
#include "Makers.h"

void setUp() {}
void tearDown() {}

static Maker of(uint32_t oui) {
  uint8_t mac[6] = {(uint8_t)(oui >> 16), (uint8_t)(oui >> 8), (uint8_t)oui, 1, 2, 3};
  return makerOf(mac);
}

void test_axon_bodycam_oui() { TEST_ASSERT_EQUAL_INT((int)Maker::Axon, (int)of(0x0025DF)); }
void test_flock_oui() { TEST_ASSERT_EQUAL_INT((int)Maker::Flock, (int)of(0xB41E52)); }
void test_camera_ouis() {
  TEST_ASSERT_EQUAL_INT((int)Maker::Ring, (int)of(0x00B463));
  TEST_ASSERT_EQUAL_INT((int)Maker::Blink, (int)of(0x70AD43));
  TEST_ASSERT_EQUAL_INT((int)Maker::Wyze, (int)of(0x2CAA8E));
  TEST_ASSERT_EQUAL_INT((int)Maker::Arlo, (int)of(0x486264));
  TEST_ASSERT_EQUAL_INT((int)Maker::Eufy, (int)of(0x007F1D));
  TEST_ASSERT_EQUAL_INT((int)Maker::Hikvision, (int)of(0x00BC99));
  TEST_ASSERT_EQUAL_INT((int)Maker::Dahua, (int)of(0x08EDED));
  TEST_ASSERT_EQUAL_INT((int)Maker::Reolink, (int)of(0xEC71DB));
  TEST_ASSERT_EQUAL_INT((int)Maker::Tuya, (int)of(0x00337A));
  TEST_ASSERT_TRUE(isCameraMaker(Maker::Hikvision));
  TEST_ASSERT_FALSE(isCameraMaker(Maker::Apple));
}
void test_apple_oui() { TEST_ASSERT_EQUAL_INT((int)Maker::Apple, (int)of(0x000393)); }
void test_random_bit() {
  uint8_t mac[6] = {0xDA, 0xA1, 0x19, 1, 2, 3};
  TEST_ASSERT_TRUE(isRandomMac(mac));
  TEST_ASSERT_EQUAL_INT((int)Maker::Random, (int)makerOf(mac));
}
void test_unknown() { TEST_ASSERT_EQUAL_INT((int)Maker::Unknown, (int)of(0x000001)); }
void test_ssid_patterns() {
  TEST_ASSERT_EQUAL_INT((int)Maker::Hikvision, (int)cameraFromSsid("HIK-12345"));
  TEST_ASSERT_EQUAL_INT((int)Maker::Wyze, (int)cameraFromSsid("Wyze_Cam_Setup"));
  TEST_ASSERT_EQUAL_INT((int)Maker::Ring, (int)cameraFromSsid("Ring-ab12"));
  TEST_ASSERT_EQUAL_INT((int)Maker::Tuya, (int)cameraFromSsid("SmartLife-1A2B"));
  TEST_ASSERT_EQUAL_INT((int)Maker::Unknown, (int)cameraFromSsid("IPC"));  // too generic alone
  TEST_ASSERT_EQUAL_INT((int)Maker::Unknown, (int)cameraFromSsid("Spectrum-5G"));
  TEST_ASSERT_EQUAL_INT((int)Maker::Unknown, (int)cameraFromSsid(nullptr));
}
void test_names() {
  TEST_ASSERT_EQUAL_STRING("AXON", makerName(Maker::Axon));
  TEST_ASSERT_EQUAL_STRING("random", makerName(Maker::Random));
  TEST_ASSERT_EQUAL_STRING("?", makerName(Maker::Unknown));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_axon_bodycam_oui);
  RUN_TEST(test_flock_oui);
  RUN_TEST(test_camera_ouis);
  RUN_TEST(test_apple_oui);
  RUN_TEST(test_random_bit);
  RUN_TEST(test_unknown);
  RUN_TEST(test_ssid_patterns);
  RUN_TEST(test_names);
  return UNITY_END();
}
