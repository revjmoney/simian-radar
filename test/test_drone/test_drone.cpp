#include <unity.h>
#include <string.h>
#include <vector>
#include "DroneTable.h"
#include "opendroneid.h"

void setUp() {}
void tearDown() {}

static const uint8_t kMac[6] = {0x60, 0x60, 0x1F, 1, 2, 3};

static std::vector<uint8_t> basicId(const char* serial) {
  ODID_BasicID_data d;
  odid_initBasicIDData(&d);
  d.IDType = ODID_IDTYPE_SERIAL_NUMBER;
  d.UAType = ODID_UATYPE_HELICOPTER_OR_MULTIROTOR;
  strncpy(d.UASID, serial, sizeof d.UASID - 1);
  ODID_BasicID_encoded e;
  TEST_ASSERT_EQUAL_INT(ODID_SUCCESS, encodeBasicIDMessage(&e, &d));
  const uint8_t* p = (const uint8_t*)&e;
  return std::vector<uint8_t>(p, p + ODID_MESSAGE_SIZE);
}
static std::vector<uint8_t> location(double lat, double lon, float alt) {
  ODID_Location_data d;
  odid_initLocationData(&d);
  d.Status = ODID_STATUS_AIRBORNE;
  d.Latitude = lat;
  d.Longitude = lon;
  d.AltitudeGeo = alt;
  d.SpeedHorizontal = 5;
  ODID_Location_encoded e;
  TEST_ASSERT_EQUAL_INT(ODID_SUCCESS, encodeLocationMessage(&e, &d));
  const uint8_t* p = (const uint8_t*)&e;
  return std::vector<uint8_t>(p, p + ODID_MESSAGE_SIZE);
}
static std::vector<uint8_t> systemMsg(double lat, double lon) {
  ODID_System_data d;
  odid_initSystemData(&d);
  d.OperatorLatitude = lat;
  d.OperatorLongitude = lon;
  ODID_System_encoded e;
  TEST_ASSERT_EQUAL_INT(ODID_SUCCESS, encodeSystemMessage(&e, &d));
  const uint8_t* p = (const uint8_t*)&e;
  return std::vector<uint8_t>(p, p + ODID_MESSAGE_SIZE);
}
// WiFi vendor IE body: counter byte, then pack header (type 0xF), message size, count, messages.
static std::vector<uint8_t> wifiPack(const std::vector<std::vector<uint8_t>>& msgs, uint8_t count = 0) {
  std::vector<uint8_t> p = {0x07, 0xF2, ODID_MESSAGE_SIZE, (uint8_t)(count ? count : msgs.size())};
  for (const auto& m : msgs) p.insert(p.end(), m.begin(), m.end());
  return p;
}

void test_wifi_pack_full_drone() {
  DroneTable t;
  auto p = wifiPack({basicId("SN-1581F4XFC23"), location(33.70, -78.88, 120), systemMsg(33.69, -78.87)});
  t.onWifiIe(kMac, p.data(), p.size(), -60, 1000);
  Drone d[2];
  TEST_ASSERT_EQUAL_UINT(1, t.list(d, 2, 1000));
  TEST_ASSERT_TRUE(d[0].hasId);
  TEST_ASSERT_EQUAL_STRING("SN-1581F4XFC23", d[0].id);
  TEST_ASSERT_TRUE(d[0].hasPos);
  TEST_ASSERT_FLOAT_WITHIN(1e-5, 33.70, d[0].lat);
  TEST_ASSERT_FLOAT_WITHIN(1.0f, 120.0f, d[0].altM);
  TEST_ASSERT_TRUE(d[0].hasPilot);
  TEST_ASSERT_FLOAT_WITHIN(1e-5, -78.87, d[0].pilotLon);
}
void test_ble_messages_join_by_address() {
  DroneTable t;
  auto id = basicId("BLE-DRONE-7");
  std::vector<uint8_t> a = {0x0D, 0x01};
  a.insert(a.end(), id.begin(), id.end());
  auto loc = location(33.5, -78.5, 50);
  std::vector<uint8_t> b = {0x0D, 0x02};
  b.insert(b.end(), loc.begin(), loc.end());
  t.onBleServiceData(kMac, a.data(), a.size(), -70, 1000);
  t.onBleServiceData(kMac, b.data(), b.size(), -68, 1100);
  Drone d[2];
  TEST_ASSERT_EQUAL_UINT(1, t.list(d, 2, 1100));
  TEST_ASSERT_EQUAL_STRING("BLE-DRONE-7", d[0].id);
  TEST_ASSERT_TRUE(d[0].hasPos);
}
void test_wrong_app_code_ignored() {
  DroneTable t;
  auto loc = location(33.5, -78.5, 50);
  std::vector<uint8_t> b = {0x0C, 0x02};
  b.insert(b.end(), loc.begin(), loc.end());
  t.onBleServiceData(kMac, b.data(), b.size(), -68, 1000);
  Drone d[2];
  TEST_ASSERT_EQUAL_UINT(0, t.list(d, 2, 1000));
}
void test_unknown_message_type_skipped() {
  DroneTable t;
  std::vector<uint8_t> junk(ODID_MESSAGE_SIZE, 0);
  junk[0] = 0xA2;  // message type 10 does not exist
  auto p = wifiPack({junk, location(33.7, -78.9, 10)});
  t.onWifiIe(kMac, p.data(), p.size(), -60, 1000);
  Drone d[2];
  TEST_ASSERT_EQUAL_UINT(1, t.list(d, 2, 1000));
  TEST_ASSERT_TRUE(d[0].hasPos);
  TEST_ASSERT_FALSE(d[0].hasId);
}
void test_pack_count_too_big_skipped() {
  DroneTable t;
  auto p = wifiPack({location(33.7, -78.9, 10)}, 9);  // claims 9 messages, carries 1
  t.onWifiIe(kMac, p.data(), p.size(), -60, 1000);
  Drone d[2];
  TEST_ASSERT_EQUAL_UINT(0, t.list(d, 2, 1000));
}
void test_pack_bad_message_size_skipped() {
  DroneTable t;
  auto p = wifiPack({location(33.7, -78.9, 10)});
  p[2] = 30;
  t.onWifiIe(kMac, p.data(), p.size(), -60, 1000);
  Drone d[2];
  TEST_ASSERT_EQUAL_UINT(0, t.list(d, 2, 1000));
}
void test_truncated_pack() {
  DroneTable t;
  auto p = wifiPack({location(33.7, -78.9, 10)});
  t.onWifiIe(kMac, p.data(), 3, -60, 1000);
  t.onWifiIe(kMac, p.data(), p.size() - 1, -60, 1000);
  std::vector<uint8_t> shortBle = {0x0D, 0x01, 0x10};
  t.onBleServiceData(kMac, shortBle.data(), shortBle.size(), -60, 1000);
  Drone d[2];
  TEST_ASSERT_EQUAL_UINT(0, t.list(d, 2, 1000));
}
void test_drones_expire_and_cap() {
  DroneTable t;
  uint8_t mac[6] = {0x60, 0, 0, 0, 0, 0};
  auto p = wifiPack({location(33.7, -78.9, 10)});
  for (int i = 0; i < 20; i++) {
    mac[5] = (uint8_t)i;
    t.onWifiIe(mac, p.data(), p.size(), (int8_t)(-90 + i), 1000 + i);
  }
  Drone d[10];
  TEST_ASSERT_EQUAL_UINT(8, t.list(d, 10, 1100));
  TEST_ASSERT_EQUAL_UINT(0, t.list(d, 10, 1100 + 61000));
}

void test_nested_pack_skipped() {
  DroneTable t;
  std::vector<uint8_t> nested(ODID_MESSAGE_SIZE, 0);
  nested[0] = 0xF2; nested[1] = ODID_MESSAGE_SIZE; nested[2] = 9;  // a pack inside a pack slot
  auto p = wifiPack({location(33.7, -78.9, 10), nested});
  t.onWifiIe(kMac, p.data(), p.size(), -60, 1000);
  Drone d[2];
  TEST_ASSERT_EQUAL_UINT(1, t.list(d, 2, 1000));  // the real location still counts
  TEST_ASSERT_TRUE(d[0].hasPos);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_wifi_pack_full_drone);
  RUN_TEST(test_ble_messages_join_by_address);
  RUN_TEST(test_wrong_app_code_ignored);
  RUN_TEST(test_unknown_message_type_skipped);
  RUN_TEST(test_pack_count_too_big_skipped);
  RUN_TEST(test_pack_bad_message_size_skipped);
  RUN_TEST(test_truncated_pack);
  RUN_TEST(test_drones_expire_and_cap);
  RUN_TEST(test_nested_pack_skipped);
  return UNITY_END();
}
