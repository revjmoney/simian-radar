#include <unity.h>
#include <string.h>
#include "Signatures.h"

void setUp() {}
void tearDown() {}

static BleAdv adv(const uint8_t* mfg, size_t mfgLen, const uint16_t* svc = nullptr, size_t nSvc = 0,
                  const char* name = nullptr) {
  BleAdv a = {{1, 2, 3, 4, 5, 6}, -50, mfg, mfgLen, svc, nSvc, name};
  return a;
}

void test_meta_glasses() {
  const uint8_t m[] = {0xAB, 0x01, 0x00};
  TEST_ASSERT_EQUAL_INT((int)BleKind::GlassesMeta, (int)classifyBle(adv(m, 3)));
}
void test_meta_tech_glasses() {
  const uint8_t m[] = {0x8E, 0x05};
  TEST_ASSERT_EQUAL_INT((int)BleKind::GlassesMeta, (int)classifyBle(adv(m, 2)));
}
void test_luxottica_glasses() {
  const uint8_t m[] = {0x53, 0x0D, 0x01};
  TEST_ASSERT_EQUAL_INT((int)BleKind::GlassesMeta, (int)classifyBle(adv(m, 3)));
}
void test_snap_glasses() {
  const uint8_t m[] = {0xC2, 0x03};
  TEST_ASSERT_EQUAL_INT((int)BleKind::GlassesSnap, (int)classifyBle(adv(m, 2)));
}
static uint8_t sep[29];
static void makeSeparated(uint8_t keyByte) {
  memset(sep, keyByte, sizeof sep);
  sep[0] = 0x4C; sep[1] = 0x00; sep[2] = 0x12; sep[3] = 0x19; sep[4] = 0x10;
}
void test_findmy_separated() {
  makeSeparated(0x77);
  TEST_ASSERT_EQUAL_INT((int)BleKind::FindMySeparated, (int)classifyBle(adv(sep, sizeof sep)));
}
void test_findmy_near_owner() {
  const uint8_t m[] = {0x4C, 0x00, 0x12, 0x02, 0x00, 0x01};
  TEST_ASSERT_EQUAL_INT((int)BleKind::FindMyNearOwner, (int)classifyBle(adv(m, sizeof m)));
}
void test_apple_other_is_other() {
  const uint8_t m[] = {0x4C, 0x00, 0x10, 0x05, 0x01};  // nearby-info, an iPhone
  TEST_ASSERT_EQUAL_INT((int)BleKind::Other, (int)classifyBle(adv(m, sizeof m)));
}
void test_tile() {
  const uint16_t s[] = {0xFEED};
  TEST_ASSERT_EQUAL_INT((int)BleKind::Tile, (int)classifyBle(adv(nullptr, 0, s, 1)));
}
void test_smarttag() {
  const uint16_t s[] = {0x180F, 0xFD5A};
  TEST_ASSERT_EQUAL_INT((int)BleKind::SmartTag, (int)classifyBle(adv(nullptr, 0, s, 2)));
}
void test_phone_is_other() {
  const uint8_t m[] = {0x75, 0x00, 0x01};
  TEST_ASSERT_EQUAL_INT((int)BleKind::Other, (int)classifyBle(adv(m, sizeof m)));
}
void test_flock_ble_mfg() {
  const uint8_t m[] = {0xC8, 0x09, 0x00};
  TEST_ASSERT_EQUAL_INT((int)BleKind::Flock, (int)classifyBle(adv(m, sizeof m)));
}
void test_flock_ble_names() {
  TEST_ASSERT_EQUAL_INT((int)BleKind::Flock, (int)classifyBle(adv(nullptr, 0, nullptr, 0, "Penguin-1234567890")));
  TEST_ASSERT_EQUAL_INT((int)BleKind::Flock, (int)classifyBle(adv(nullptr, 0, nullptr, 0, "FS Ext Battery")));
}
void test_flock_ble_raven_service() {
  const uint16_t s[] = {0x3200};
  TEST_ASSERT_EQUAL_INT((int)BleKind::Flock, (int)classifyBle(adv(nullptr, 0, s, 1)));
}
void test_short_mfg_is_other() {
  const uint8_t m[] = {0xAB};
  TEST_ASSERT_EQUAL_INT((int)BleKind::Other, (int)classifyBle(adv(m, 1)));
}
void test_flock_wifi_ssid() {
  const uint8_t b[6] = {0x11, 0x22, 0x33, 0, 0, 0};
  TEST_ASSERT_TRUE(isFlockWifi("Flock-3A9F21", b));
  TEST_ASSERT_TRUE(isFlockWifi("Penguin-5521", b));
  TEST_ASSERT_TRUE(isFlockWifi("FS Ext Battery", b));
  TEST_ASSERT_FALSE(isFlockWifi("NETGEAR42", b));
  TEST_ASSERT_FALSE(isFlockWifi("Flocks Coffee", b));
  TEST_ASSERT_FALSE(isFlockWifi(nullptr, b));
}
void test_flock_wifi_ouis() {
  const uint8_t flock[6] = {0xB4, 0x1E, 0x52, 1, 2, 3};
  const uint8_t qca[6] = {0x00, 0x03, 0x7F, 0x50, 0x00, 0x01};
  const uint8_t esp[6] = {0xA4, 0xCF, 0x12, 1, 2, 3};
  TEST_ASSERT_TRUE(isFlockWifi("", flock));
  TEST_ASSERT_TRUE(isFlockWifi("", qca));
  TEST_ASSERT_FALSE(isFlockWifi("", esp));
}
void test_tracker_key_stable_across_addresses() {
  makeSeparated(0x42);
  BleAdv a = adv(sep, sizeof sep), b = adv(sep, sizeof sep);
  b.addr[5] = 0x99;
  uint8_t ka[8], kb[8];
  TEST_ASSERT_TRUE(trackerKey(a, BleKind::FindMySeparated, ka));
  TEST_ASSERT_TRUE(trackerKey(b, BleKind::FindMySeparated, kb));
  TEST_ASSERT_EQUAL_MEMORY(ka, kb, 8);
}
void test_tracker_key_tile_uses_address() {
  const uint16_t s[] = {0xFEED};
  BleAdv a = adv(nullptr, 0, s, 1), b = adv(nullptr, 0, s, 1);
  b.addr[0] = 0x55;
  uint8_t ka[8], kb[8];
  trackerKey(a, BleKind::Tile, ka);
  trackerKey(b, BleKind::Tile, kb);
  TEST_ASSERT_FALSE(memcmp(ka, kb, 8) == 0);
}
void test_labels() {
  TEST_ASSERT_EQUAL_STRING("META GLASS", bleLabel(BleKind::GlassesMeta));
  TEST_ASSERT_EQUAL_STRING("AIRTAG", bleLabel(BleKind::FindMySeparated));
  TEST_ASSERT_EQUAL_STRING("", bleLabel(BleKind::Other));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_meta_glasses);
  RUN_TEST(test_meta_tech_glasses);
  RUN_TEST(test_luxottica_glasses);
  RUN_TEST(test_snap_glasses);
  RUN_TEST(test_findmy_separated);
  RUN_TEST(test_findmy_near_owner);
  RUN_TEST(test_apple_other_is_other);
  RUN_TEST(test_tile);
  RUN_TEST(test_smarttag);
  RUN_TEST(test_phone_is_other);
  RUN_TEST(test_flock_ble_mfg);
  RUN_TEST(test_flock_ble_names);
  RUN_TEST(test_flock_ble_raven_service);
  RUN_TEST(test_short_mfg_is_other);
  RUN_TEST(test_flock_wifi_ssid);
  RUN_TEST(test_flock_wifi_ouis);
  RUN_TEST(test_tracker_key_stable_across_addresses);
  RUN_TEST(test_tracker_key_tile_uses_address);
  RUN_TEST(test_labels);
  return UNITY_END();
}
