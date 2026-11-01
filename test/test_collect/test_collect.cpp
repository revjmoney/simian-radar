#include <unity.h>
#include <string.h>
#include "Collectors.h"

void setUp() {}
void tearDown() {}

static WifiFrame frame(uint8_t type, uint8_t subtype, const uint8_t a2[6], const char* ssid = nullptr) {
  WifiFrame f = {};
  f.type = type;
  f.subtype = subtype;
  memcpy(f.a2, a2, 6);
  f.hasA2 = true;
  if (ssid) { strcpy(f.ssid, ssid); f.hasSsid = true; }
  return f;
}
static const uint8_t kPhone[6] = {0x00, 0x03, 0x93, 1, 2, 3};    // Apple
static const uint8_t kRandom[6] = {0xDA, 0x11, 0x22, 1, 2, 3};
static const uint8_t kHik[6] = {0x00, 0xBC, 0x99, 1, 2, 3};
static const uint8_t kRouter[6] = {0x10, 0x20, 0x30, 1, 2, 3};
static const uint8_t kAxon[6] = {0x00, 0x25, 0xDF, 1, 2, 3};

void test_phone_from_probe_req() {
  Collectors c;
  c.onWifi(frame(0, kWifiProbeReq, kPhone, "Starbucks"), -40, 6, 1000);
  Seen s[4];
  TEST_ASSERT_EQUAL_UINT(1, c.phones(s, 4, 1000));
  TEST_ASSERT_EQUAL_STRING("Starbucks", s[0].text);
  TEST_ASSERT_EQUAL_INT((int)Maker::Apple, s[0].maker);
}
void test_phone_random_mac_labelled() {
  Collectors c;
  c.onWifi(frame(0, kWifiProbeReq, kRandom, ""), -50, 6, 1000);
  Seen s[4];
  c.phones(s, 4, 1000);
  TEST_ASSERT_EQUAL_INT((int)Maker::Random, s[0].maker);
}
void test_beacons_are_not_phones() {
  Collectors c;
  c.onWifi(frame(0, kWifiBeacon, kRouter, "Home"), -50, 6, 1000);
  TEST_ASSERT_EQUAL_UINT(0, c.phoneCount(1000));
}
void test_spycam_by_oui() {
  Collectors c;
  c.onWifi(frame(2, 0, kHik), -60, 1, 1000);  // a data frame from a Hikvision client
  Seen s[4];
  TEST_ASSERT_EQUAL_UINT(1, c.spycams(s, 4, 1000));
  TEST_ASSERT_EQUAL_STRING("HIKVISION", s[0].text);
}
void test_spycam_by_ssid() {
  Collectors c;
  c.onWifi(frame(0, kWifiBeacon, kRouter, "Wyze_Cam_Setup"), -60, 1, 1000);
  Seen s[4];
  TEST_ASSERT_EQUAL_UINT(1, c.spycams(s, 4, 1000));
  TEST_ASSERT_EQUAL_STRING("WYZE", s[0].text);
}
void test_ordinary_router_not_spycam() {
  Collectors c;
  c.onWifi(frame(0, kWifiBeacon, kRouter, "Spectrum-5G"), -60, 1, 1000);
  Seen s[4];
  TEST_ASSERT_EQUAL_UINT(0, c.spycams(s, 4, 1000));
}
void test_channel_rates() {
  Collectors c;
  c.onFrameCount(1, 50);  // the sniffer counts every frame, parsed or not
  c.onDwell(1, 1000);
  c.onDwell(6, 500);
  float r[11];
  c.channelRates(r);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 50.0f, r[0]);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, r[5]);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, r[10]);  // never dwelt there: no data, no divide by zero
}
void test_channel_out_of_range_ignored() {
  Collectors c;
  c.onFrameCount(14, 10);
  c.onFrameCount(0, 10);
  c.onDwell(14, 1000);
  float r[11];
  c.channelRates(r);
  for (float v : r) TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, v);
}
void test_phone_table_caps() {
  Collectors c;
  uint8_t mac[6] = {0x00, 0x03, 0x93, 0, 0, 0};
  for (int i = 0; i < 1000; i++) {
    mac[4] = (uint8_t)(i >> 8); mac[5] = (uint8_t)i;
    c.onWifi(frame(0, kWifiProbeReq, mac, ""), -60, 6, (uint32_t)i);
  }
  TEST_ASSERT_EQUAL_UINT(48, c.phoneTableSize());
}
void test_list_strongest_first_and_expiry() {
  Collectors c;
  uint8_t a[6] = {0x00, 0x03, 0x93, 0, 0, 1}, b[6] = {0x00, 0x03, 0x93, 0, 0, 2};
  c.onWifi(frame(0, kWifiProbeReq, a, ""), -70, 6, 1000);
  c.onWifi(frame(0, kWifiProbeReq, b, ""), -30, 6, 1000);
  Seen s[4];
  TEST_ASSERT_EQUAL_UINT(2, c.phones(s, 4, 1000));
  TEST_ASSERT_EQUAL_INT8(-30, s[0].rssi);
  TEST_ASSERT_EQUAL_UINT(0, c.phones(s, 4, 1000 + 61000));
}
void test_bodycam_ble() {
  Collectors c;
  BleAdv adv = {};
  memcpy(adv.addr, kAxon, 6);
  adv.rssi = -55;
  uint8_t key[8] = {};
  memcpy(key, kAxon, 6);
  c.onBle(adv, BleKind::Other, key, 1000);
  Seen s[4];
  TEST_ASSERT_EQUAL_UINT(1, c.bodycams(s, 4, 1000));
  TEST_ASSERT_EQUAL_STRING("AXON", s[0].text);
}
void test_tags_only_trackers_and_glasses() {
  Collectors c;
  BleAdv adv = {};
  adv.rssi = -50;
  uint8_t k1[8] = {1}, k2[8] = {2}, k3[8] = {3};
  c.onBle(adv, BleKind::FindMySeparated, k1, 1000);
  c.onBle(adv, BleKind::GlassesMeta, k2, 1000);
  c.onBle(adv, BleKind::Other, k3, 1000);
  Seen s[4];
  TEST_ASSERT_EQUAL_UINT(2, c.tags(s, 4, 1000));
}

void test_seen_table_clear() {
  SeenTable<4> t;
  uint8_t k[6] = {1, 2, 3, 4, 5, 6};
  t.upsert(k, -50, 1000);
  t.clear();
  TEST_ASSERT_EQUAL_UINT(0, t.size());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_phone_from_probe_req);
  RUN_TEST(test_phone_random_mac_labelled);
  RUN_TEST(test_beacons_are_not_phones);
  RUN_TEST(test_spycam_by_oui);
  RUN_TEST(test_spycam_by_ssid);
  RUN_TEST(test_ordinary_router_not_spycam);
  RUN_TEST(test_channel_rates);
  RUN_TEST(test_channel_out_of_range_ignored);
  RUN_TEST(test_phone_table_caps);
  RUN_TEST(test_list_strongest_first_and_expiry);
  RUN_TEST(test_bodycam_ble);
  RUN_TEST(test_tags_only_trackers_and_glasses);
  RUN_TEST(test_seen_table_clear);
  return UNITY_END();
}
