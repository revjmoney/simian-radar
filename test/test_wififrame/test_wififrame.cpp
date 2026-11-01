#include <unity.h>
#include <string.h>
#include <vector>
#include "WifiFrame.h"

void setUp() {}
void tearDown() {}

static const uint8_t A1[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
static const uint8_t A2[6] = {0xb4, 0x1e, 0x52, 1, 2, 3};
static const uint8_t A3[6] = {0x10, 0x20, 0x30, 4, 5, 6};

static std::vector<uint8_t> mgmt(uint8_t subtype, size_t fixed, uint16_t capab = 0) {
  std::vector<uint8_t> f = {(uint8_t)(subtype << 4), 0, 0, 0};
  f.insert(f.end(), A1, A1 + 6);
  f.insert(f.end(), A2, A2 + 6);
  f.insert(f.end(), A3, A3 + 6);
  f.push_back(0); f.push_back(0);  // sequence
  for (size_t i = 0; i < fixed; i++) f.push_back(0);
  if (fixed == 12) { f[24 + 10] = capab & 0xff; f[24 + 11] = capab >> 8; }
  return f;
}
static void ie(std::vector<uint8_t>& f, uint8_t id, const std::vector<uint8_t>& body) {
  f.push_back(id);
  f.push_back((uint8_t)body.size());
  f.insert(f.end(), body.begin(), body.end());
}
static std::vector<uint8_t> str(const char* s) { return std::vector<uint8_t>(s, s + strlen(s)); }

void test_beacon_ssid_and_open() {
  auto f = mgmt(kWifiBeacon, 12, 0x0001);
  ie(f, 0, str("Flock-3A9F21"));
  WifiFrame w;
  TEST_ASSERT_TRUE(parseWifiFrame(f.data(), f.size(), &w));
  TEST_ASSERT_EQUAL_UINT8(0, w.type);
  TEST_ASSERT_EQUAL_UINT8(kWifiBeacon, w.subtype);
  TEST_ASSERT_TRUE(w.hasSsid);
  TEST_ASSERT_EQUAL_STRING("Flock-3A9F21", w.ssid);
  TEST_ASSERT_EQUAL_MEMORY(A2, w.a2, 6);
  TEST_ASSERT_TRUE(w.open);
}
void test_beacon_privacy_not_open() {
  auto f = mgmt(kWifiBeacon, 12, 0x0011);
  ie(f, 0, str("Home"));
  WifiFrame w;
  parseWifiFrame(f.data(), f.size(), &w);
  TEST_ASSERT_FALSE(w.open);
}
void test_probe_request_ssid() {
  auto f = mgmt(kWifiProbeReq, 0);
  ie(f, 0, str("Starbucks"));
  WifiFrame w;
  TEST_ASSERT_TRUE(parseWifiFrame(f.data(), f.size(), &w));
  TEST_ASSERT_EQUAL_UINT8(kWifiProbeReq, w.subtype);
  TEST_ASSERT_EQUAL_STRING("Starbucks", w.ssid);
}
void test_probe_request_wildcard() {
  auto f = mgmt(kWifiProbeReq, 0);
  ie(f, 0, {});
  WifiFrame w;
  parseWifiFrame(f.data(), f.size(), &w);
  TEST_ASSERT_TRUE(w.hasSsid);
  TEST_ASSERT_EQUAL_STRING("", w.ssid);
}
void test_probe_response_ssid() {
  auto f = mgmt(kWifiProbeResp, 12);
  ie(f, 0, str("FreeWifi"));
  WifiFrame w;
  parseWifiFrame(f.data(), f.size(), &w);
  TEST_ASSERT_EQUAL_STRING("FreeWifi", w.ssid);
}
void test_deauth() {
  auto f = mgmt(kWifiDeauth, 2);
  WifiFrame w;
  TEST_ASSERT_TRUE(parseWifiFrame(f.data(), f.size(), &w));
  TEST_ASSERT_EQUAL_UINT8(kWifiDeauth, w.subtype);
  TEST_ASSERT_FALSE(w.hasSsid);
  TEST_ASSERT_EQUAL_MEMORY(A3, w.a3, 6);
}
void test_data_frame_addresses() {
  auto f = mgmt(0, 0);
  f[0] = 0x08;  // type 2 data, subtype 0
  WifiFrame w;
  TEST_ASSERT_TRUE(parseWifiFrame(f.data(), f.size(), &w));
  TEST_ASSERT_EQUAL_UINT8(2, w.type);
  TEST_ASSERT_TRUE(w.hasA2);
  TEST_ASSERT_EQUAL_MEMORY(A2, w.a2, 6);
}
void test_rts_has_two_addresses() {
  std::vector<uint8_t> f = {0xb4, 0, 0, 0};  // type 1 control, subtype 11 RTS
  f.insert(f.end(), A1, A1 + 6);
  f.insert(f.end(), A2, A2 + 6);
  WifiFrame w;
  TEST_ASSERT_TRUE(parseWifiFrame(f.data(), f.size(), &w));
  TEST_ASSERT_EQUAL_UINT8(1, w.type);
  TEST_ASSERT_TRUE(w.hasA2);
  TEST_ASSERT_FALSE(w.hasA3);
}
void test_cts_one_address() {
  std::vector<uint8_t> f = {0xc4, 0, 0, 0};
  f.insert(f.end(), A1, A1 + 6);
  WifiFrame w;
  TEST_ASSERT_TRUE(parseWifiFrame(f.data(), f.size(), &w));
  TEST_ASSERT_FALSE(w.hasA2);
}
void test_remote_id_vendor_ie() {
  auto f = mgmt(kWifiBeacon, 12);
  ie(f, 0, str("drone"));
  ie(f, 221, {0xfa, 0x0b, 0xbc, 0x0d, 0x07, 0xf2, 0x19, 0x01});
  WifiFrame w;
  parseWifiFrame(f.data(), f.size(), &w);
  TEST_ASSERT_NOT_NULL(w.ridIe);
  TEST_ASSERT_EQUAL_UINT(4, w.ridLen);
  TEST_ASSERT_EQUAL_UINT8(0x07, w.ridIe[0]);
}
void test_other_vendor_ie_ignored() {
  auto f = mgmt(kWifiBeacon, 12);
  ie(f, 221, {0x00, 0x50, 0xf2, 0x04, 1, 2});
  WifiFrame w;
  parseWifiFrame(f.data(), f.size(), &w);
  TEST_ASSERT_NULL(w.ridIe);
}
void test_truncated_ie_len_past_end() {
  auto f = mgmt(kWifiBeacon, 12);
  f.push_back(0);
  f.push_back(30);  // claims 30 bytes, none follow
  f.push_back('A');
  WifiFrame w;
  TEST_ASSERT_TRUE(parseWifiFrame(f.data(), f.size(), &w));
  TEST_ASSERT_FALSE(w.hasSsid);
}
void test_ssid_over_32_clamped() {
  auto f = mgmt(kWifiBeacon, 12);
  ie(f, 0, str("0123456789012345678901234567890123456789"));
  WifiFrame w;
  parseWifiFrame(f.data(), f.size(), &w);
  TEST_ASSERT_EQUAL_UINT(32, strlen(w.ssid));
}
void test_truncated_header() {
  uint8_t f[12] = {0x80};
  WifiFrame w;
  TEST_ASSERT_FALSE(parseWifiFrame(f, sizeof f, &w));
  TEST_ASSERT_FALSE(parseWifiFrame(f, 3, &w));
}
void test_truncated_beacon_fixed_fields() {
  auto f = mgmt(kWifiBeacon, 5);
  WifiFrame w;
  TEST_ASSERT_TRUE(parseWifiFrame(f.data(), f.size(), &w));
  TEST_ASSERT_FALSE(w.hasSsid);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_beacon_ssid_and_open);
  RUN_TEST(test_beacon_privacy_not_open);
  RUN_TEST(test_probe_request_ssid);
  RUN_TEST(test_probe_request_wildcard);
  RUN_TEST(test_probe_response_ssid);
  RUN_TEST(test_deauth);
  RUN_TEST(test_data_frame_addresses);
  RUN_TEST(test_rts_has_two_addresses);
  RUN_TEST(test_cts_one_address);
  RUN_TEST(test_remote_id_vendor_ie);
  RUN_TEST(test_other_vendor_ie_ignored);
  RUN_TEST(test_truncated_ie_len_past_end);
  RUN_TEST(test_ssid_over_32_clamped);
  RUN_TEST(test_truncated_header);
  RUN_TEST(test_truncated_beacon_fixed_fields);
  return UNITY_END();
}
