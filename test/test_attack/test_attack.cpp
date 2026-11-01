#include <unity.h>
#include <string.h>
#include "AttackDetector.h"

void setUp() {}
void tearDown() {}

static WifiFrame mgmt(uint8_t subtype, const uint8_t a2[6], const char* ssid = nullptr, bool open = false) {
  WifiFrame f = {};
  f.type = 0;
  f.subtype = subtype;
  memcpy(f.a2, a2, 6);
  memcpy(f.a3, a2, 6);
  f.hasA2 = f.hasA3 = true;
  if (ssid) { strcpy(f.ssid, ssid); f.hasSsid = true; }
  f.open = open;
  return f;
}
static const uint8_t kAp[6] = {0x10, 0x20, 0x30, 1, 2, 3};
static const uint8_t kAp2[6] = {0x10, 0x20, 0x30, 1, 2, 4};  // same maker, second BSSID
static const uint8_t kRogue[6] = {0x44, 0x55, 0x66, 7, 8, 9};

void test_deauth_flood_threshold() {
  AttackDetector d;
  AttackEvent e;
  for (int i = 0; i < 10; i++) d.onWifi(mgmt(kWifiDeauth, kAp), 1000 + i * 50);
  TEST_ASSERT_FALSE(d.poll(1600, &e));
  d.onWifi(mgmt(kWifiDeauth, kAp), 1700);
  TEST_ASSERT_TRUE(d.poll(1700, &e));
  TEST_ASSERT_EQUAL_INT((int)AttackKind::DeauthFlood, (int)e.kind);
  TEST_ASSERT_EQUAL_MEMORY(kAp, e.mac, 6);
}
void test_slow_deauths_not_flood() {
  AttackDetector d;
  AttackEvent e;
  for (int i = 0; i < 30; i++) d.onWifi(mgmt(kWifiDeauth, kAp), 1000 + i * 500);
  TEST_ASSERT_FALSE(d.poll(20000, &e));
}
void test_evil_twin_two_makers_one_open() {
  AttackDetector d;
  AttackEvent e;
  d.onWifi(mgmt(kWifiBeacon, kAp, "CoffeeShop", false), 1000);
  d.onWifi(mgmt(kWifiBeacon, kRogue, "CoffeeShop", true), 1100);
  TEST_ASSERT_TRUE(d.poll(1100, &e));
  TEST_ASSERT_EQUAL_INT((int)AttackKind::EvilTwin, (int)e.kind);
  TEST_ASSERT_EQUAL_STRING("CoffeeShop", e.ssid);
}
void test_mesh_wifi_same_maker_not_evil_twin() {
  AttackDetector d;
  AttackEvent e;
  d.onWifi(mgmt(kWifiBeacon, kAp, "Home", false), 1000);
  d.onWifi(mgmt(kWifiBeacon, kAp2, "Home", true), 1100);  // same OUI, e.g. a mesh guest node
  TEST_ASSERT_FALSE(d.poll(1100, &e));
}
void test_two_secured_makers_not_evil_twin() {
  AttackDetector d;
  AttackEvent e;
  d.onWifi(mgmt(kWifiBeacon, kAp, "Office", false), 1000);
  d.onWifi(mgmt(kWifiBeacon, kRogue, "Office", false), 1100);
  TEST_ASSERT_FALSE(d.poll(1100, &e));
}
void test_karma_5_ssids() {
  AttackDetector d;
  AttackEvent e;
  const char* ssids[] = {"a", "b", "c", "d", "e"};
  for (int i = 0; i < 4; i++) d.onWifi(mgmt(kWifiProbeResp, kRogue, ssids[i]), 1000 + i);
  TEST_ASSERT_FALSE(d.poll(1010, &e));
  d.onWifi(mgmt(kWifiProbeResp, kRogue, ssids[4]), 1020);
  TEST_ASSERT_TRUE(d.poll(1020, &e));
  TEST_ASSERT_EQUAL_INT((int)AttackKind::Karma, (int)e.kind);
}
void test_same_ssid_repeated_not_karma() {
  AttackDetector d;
  AttackEvent e;
  for (int i = 0; i < 20; i++) d.onWifi(mgmt(kWifiProbeResp, kAp, "Home"), 1000 + i);
  TEST_ASSERT_FALSE(d.poll(1100, &e));
}
void test_pwnagotchi_bssid() {
  AttackDetector d;
  AttackEvent e;
  const uint8_t pwn[6] = {0xde, 0xad, 0xbe, 0xef, 0xde, 0xad};
  d.onWifi(mgmt(kWifiBeacon, pwn, "{\"name\":\"pwny\"}"), 1000);
  TEST_ASSERT_TRUE(d.poll(1000, &e));
  TEST_ASSERT_EQUAL_INT((int)AttackKind::Pwnagotchi, (int)e.kind);
}
void test_flipper_name() {
  AttackDetector d;
  AttackEvent e;
  BleAdv a = {};
  a.name = "Flipper Zorro";
  d.onBle(a, 1000);
  TEST_ASSERT_TRUE(d.poll(1000, &e));
  TEST_ASSERT_EQUAL_INT((int)AttackKind::Flipper, (int)e.kind);
}
void test_ble_spam_many_addresses() {
  AttackDetector d;
  AttackEvent e;
  const uint8_t m[] = {0x4C, 0x00, 0x07, 0x19, 0x01};
  BleAdv a = {};
  a.mfg = m;
  a.mfgLen = sizeof m;
  for (int i = 0; i < 19; i++) {
    a.addr[5] = (uint8_t)i;
    d.onBle(a, 1000 + i * 100);
  }
  TEST_ASSERT_FALSE(d.poll(3000, &e));
  a.addr[5] = 99;
  d.onBle(a, 3000);
  TEST_ASSERT_TRUE(d.poll(3000, &e));
  TEST_ASSERT_EQUAL_INT((int)AttackKind::BleSpam, (int)e.kind);
}
void test_one_airpods_case_not_spam() {
  AttackDetector d;
  AttackEvent e;
  const uint8_t m[] = {0x4C, 0x00, 0x07, 0x19, 0x01};
  BleAdv a = {};
  a.mfg = m;
  a.mfgLen = sizeof m;
  for (int i = 0; i < 100; i++) d.onBle(a, 1000 + i * 100);  // same address over and over
  TEST_ASSERT_FALSE(d.poll(12000, &e));
}
void test_attack_poll_once_per_kind_per_2min() {
  AttackDetector d;
  AttackEvent e;
  BleAdv a = {};
  a.name = "Flipper A";
  d.onBle(a, 1000);
  TEST_ASSERT_TRUE(d.poll(1000, &e));
  d.onBle(a, 5000);
  TEST_ASSERT_FALSE(d.poll(5000, &e));
  d.onBle(a, 125000);
  TEST_ASSERT_TRUE(d.poll(125000, &e));
}
void test_recent_keeps_newest_first() {
  AttackDetector d;
  AttackEvent e;
  BleAdv a = {};
  a.name = "Flipper A";
  d.onBle(a, 1000);
  const uint8_t pwn[6] = {0xde, 0xad, 0xbe, 0xef, 0xde, 0xad};
  d.onWifi(mgmt(kWifiBeacon, pwn, "x"), 2000);
  while (d.poll(2000, &e)) {}
  AttackEvent r[4];
  TEST_ASSERT_EQUAL_UINT(2, d.recent(r, 4));
  TEST_ASSERT_EQUAL_INT((int)AttackKind::Pwnagotchi, (int)r[0].kind);
}
void test_labels() {
  TEST_ASSERT_EQUAL_STRING("DEAUTH FLOOD", attackName(AttackKind::DeauthFlood));
  TEST_ASSERT_EQUAL_STRING("BLE SPAM", attackName(AttackKind::BleSpam));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_deauth_flood_threshold);
  RUN_TEST(test_slow_deauths_not_flood);
  RUN_TEST(test_evil_twin_two_makers_one_open);
  RUN_TEST(test_mesh_wifi_same_maker_not_evil_twin);
  RUN_TEST(test_two_secured_makers_not_evil_twin);
  RUN_TEST(test_karma_5_ssids);
  RUN_TEST(test_same_ssid_repeated_not_karma);
  RUN_TEST(test_pwnagotchi_bssid);
  RUN_TEST(test_flipper_name);
  RUN_TEST(test_ble_spam_many_addresses);
  RUN_TEST(test_one_airpods_case_not_spam);
  RUN_TEST(test_attack_poll_once_per_kind_per_2min);
  RUN_TEST(test_recent_keeps_newest_first);
  RUN_TEST(test_labels);
  return UNITY_END();
}
