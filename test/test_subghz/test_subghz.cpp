#include <unity.h>
#include <string.h>
#include <vector>
#include "SubGhz.h"

void setUp() {}
void tearDown() {}

static void le32(std::vector<uint8_t>& v, uint32_t x) {
  for (int i = 0; i < 4; i++) v.push_back((uint8_t)(x >> (8 * i)));
}

void test_meshtastic_header() {
  std::vector<uint8_t> p;
  le32(p, 0xFFFFFFFF);     // broadcast
  le32(p, 0xDEADBEEF);     // sender node
  le32(p, 0x12345678);     // packet id
  p.push_back((3 << 5) | 1);  // hop_start 3, hop_limit 1: relayed twice
  p.push_back(0x08); p.push_back(0); p.push_back(0);
  p.push_back(0xAA);       // encrypted body
  uint32_t from, to;
  uint8_t hops;
  TEST_ASSERT_TRUE(parseMeshtastic(p.data(), p.size(), &from, &to, &hops));
  TEST_ASSERT_EQUAL_HEX32(0xDEADBEEF, from);
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFF, to);
  TEST_ASSERT_EQUAL_UINT8(2, hops);
}
void test_truncated_meshtastic() {
  uint8_t p[15] = {};
  uint32_t a, b;
  uint8_t h;
  TEST_ASSERT_FALSE(parseMeshtastic(p, sizeof p, &a, &b, &h));
}

static std::vector<uint8_t> advert(uint8_t route, uint8_t pathLen, uint8_t flags, const char* name,
                                   int32_t lat = 0, int32_t lon = 0) {
  std::vector<uint8_t> p = {(uint8_t)((0x04 << 2) | route)};
  if (route == 0 || route == 3) for (int i = 0; i < 4; i++) p.push_back(0x11);
  p.push_back(pathLen);
  int pathBytes = (pathLen & 63) * ((pathLen >> 6) + 1);
  for (int i = 0; i < pathBytes; i++) p.push_back(0x22);
  for (int i = 0; i < 32; i++) p.push_back(0x33);  // public key
  le32(p, 1793500000);                              // timestamp
  for (int i = 0; i < 64; i++) p.push_back(0x44);  // signature
  p.push_back(flags);
  if (flags & 0x10) { le32(p, (uint32_t)lat); le32(p, (uint32_t)lon); }
  if (name) p.insert(p.end(), name, name + strlen(name));
  return p;
}
void test_meshcore_advert_with_position() {
  auto p = advert(1, 2, 0x80 | 0x10 | 1, "RevJMoney", 33689100, -78886700);
  char name[32];
  double lat, lon;
  bool hasPos;
  TEST_ASSERT_TRUE(parseMeshCoreAdvert(p.data(), p.size(), name, &lat, &lon, &hasPos));
  TEST_ASSERT_EQUAL_STRING("RevJMoney", name);
  TEST_ASSERT_TRUE(hasPos);
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 33.6891, lat);
  TEST_ASSERT_FLOAT_WITHIN(1e-6, -78.8867, lon);
}
void test_meshcore_advert_transport_route_no_pos() {
  auto p = advert(0, 0, 0x80 | 2, "Calabash RPT");
  char name[32];
  double lat, lon;
  bool hasPos;
  TEST_ASSERT_TRUE(parseMeshCoreAdvert(p.data(), p.size(), name, &lat, &lon, &hasPos));
  TEST_ASSERT_EQUAL_STRING("Calabash RPT", name);
  TEST_ASSERT_FALSE(hasPos);
}
void test_meshcore_other_payload_type_rejected() {
  auto p = advert(1, 0, 0x80, "x");
  p[0] = (0x02 << 2) | 1;  // a text message, not an advert
  char name[32];
  double lat, lon;
  bool hasPos;
  TEST_ASSERT_FALSE(parseMeshCoreAdvert(p.data(), p.size(), name, &lat, &lon, &hasPos));
}
void test_truncated_meshcore_advert() {
  auto p = advert(1, 3, 0x80 | 0x10, "x", 1, 2);
  char name[32];
  double lat, lon;
  bool hasPos;
  TEST_ASSERT_FALSE(parseMeshCoreAdvert(p.data(), 50, name, &lat, &lon, &hasPos));
  TEST_ASSERT_FALSE(parseMeshCoreAdvert(p.data(), 1 + 1 + 3 + 100 + 4, name, &lat, &lon, &hasPos));
  std::vector<uint8_t> badPath = {(0x04 << 2) | 1, 0xFF};  // path length claims more than exists
  TEST_ASSERT_FALSE(parseMeshCoreAdvert(badPath.data(), badPath.size(), name, &lat, &lon, &hasPos));
}
void test_meshcore_long_name_clamped() {
  auto p = advert(1, 0, 0x80, "0123456789012345678901234567890123456789");
  char name[32];
  double lat, lon;
  bool hasPos;
  TEST_ASSERT_TRUE(parseMeshCoreAdvert(p.data(), p.size(), name, &lat, &lon, &hasPos));
  TEST_ASSERT_EQUAL_UINT(31, strlen(name));
}

void test_lorawan_uplink_devaddr_and_net() {
  std::vector<uint8_t> p = {0x40};  // unconfirmed data up
  le32(p, 0x260B1234);              // TTN prefix 0x26
  p.push_back(0x80); p.push_back(1); p.push_back(0);  // FCtrl, FCnt
  p.push_back(0x01);                                 // FPort
  p.push_back(0xAB);                                 // encrypted payload
  le32(p, 0xCAFEF00D);                               // MIC
  uint8_t mtype;
  uint32_t addr;
  TEST_ASSERT_TRUE(parseLoRaWan(p.data(), p.size(), &mtype, &addr));
  TEST_ASSERT_EQUAL_UINT8(2, mtype);
  TEST_ASSERT_EQUAL_HEX32(0x260B1234, addr);
  TEST_ASSERT_EQUAL_STRING("TTN", netName(addr));
  TEST_ASSERT_EQUAL_STRING("Helium", netName(0x48001234));
  TEST_ASSERT_EQUAL_STRING("Helium", netName(0xFC014C99));
  TEST_ASSERT_EQUAL_STRING("LoRaWAN", netName(0x01020304));
}
void test_lorawan_join_request() {
  std::vector<uint8_t> p(23, 0x55);
  p[0] = 0x00;
  uint8_t mtype;
  uint32_t addr = 99;
  TEST_ASSERT_TRUE(parseLoRaWan(p.data(), p.size(), &mtype, &addr));
  TEST_ASSERT_EQUAL_UINT8(0, mtype);
  TEST_ASSERT_EQUAL_UINT32(0, addr);
}
void test_truncated_lorawan() {
  uint8_t p[11] = {0x40};
  uint8_t mtype;
  uint32_t addr;
  TEST_ASSERT_FALSE(parseLoRaWan(p, sizeof p, &mtype, &addr));
  uint8_t j[22] = {0x00};
  TEST_ASSERT_FALSE(parseLoRaWan(j, sizeof j, &mtype, &addr));
  uint8_t prop[20] = {0xE0};  // proprietary
  TEST_ASSERT_FALSE(parseLoRaWan(prop, sizeof prop, &mtype, &addr));
}

static std::vector<uint8_t> zwave(uint32_t home, uint8_t src, uint8_t type, uint8_t dst) {
  std::vector<uint8_t> p = {(uint8_t)(home >> 24), (uint8_t)(home >> 16), (uint8_t)(home >> 8), (uint8_t)home,
                            src, type, 0x00, 0, dst, 0x20, 0x01};
  p[7] = (uint8_t)(p.size() + 1);  // length includes the checksum
  uint8_t cs = 0xFF;
  for (uint8_t b : p) cs ^= b;
  p.push_back(cs);
  return p;
}
void test_zwave_frame() {
  auto p = zwave(0xC0FFEE01, 5, 0x41, 1);
  uint32_t home;
  uint8_t src, type;
  TEST_ASSERT_TRUE(parseZWave(p.data(), p.size(), &home, &src, &type));
  TEST_ASSERT_EQUAL_HEX32(0xC0FFEE01, home);
  TEST_ASSERT_EQUAL_UINT8(5, src);
  TEST_ASSERT_EQUAL_UINT8(1, type);  // singlecast
}
void test_zwave_trailing_bytes_ok() {
  auto p = zwave(0xC0FFEE01, 7, 0x41, 1);
  p.push_back(0x00); p.push_back(0x55);  // fixed-length receive buffer padding
  uint32_t home;
  uint8_t src, type;
  TEST_ASSERT_TRUE(parseZWave(p.data(), p.size(), &home, &src, &type));
  TEST_ASSERT_EQUAL_UINT8(7, src);
}
void test_zwave_bad_checksum() {
  auto p = zwave(0xC0FFEE01, 5, 0x41, 1);
  p.back() ^= 1;
  uint32_t home;
  uint8_t src, type;
  TEST_ASSERT_FALSE(parseZWave(p.data(), p.size(), &home, &src, &type));
}
void test_truncated_zwave() {
  auto p = zwave(0xC0FFEE01, 5, 0x41, 1);
  uint32_t home;
  uint8_t src, type;
  TEST_ASSERT_FALSE(parseZWave(p.data(), 8, &home, &src, &type));
  p[7] = 40;  // length byte claims more than received
  TEST_ASSERT_FALSE(parseZWave(p.data(), p.size(), &home, &src, &type));
}

static uint8_t crc8(const uint8_t* b, size_t n) {
  uint8_t c = 0;
  for (size_t i = 0; i < n; i++) {
    c ^= b[i];
    for (int k = 0; k < 8; k++) c = c & 0x80 ? (uint8_t)((c << 1) ^ 0x31) : (uint8_t)(c << 1);
  }
  return c;
}
static std::vector<uint8_t> wh24(uint16_t tempRaw, uint8_t hum, uint16_t windRaw, uint16_t dir) {
  std::vector<uint8_t> b(17, 0);
  b[0] = 0x24;
  b[1] = 0x9A;
  b[2] = dir & 0xFF;
  b[3] = (uint8_t)(((dir >> 1) & 0x80) | ((windRaw >> 4) & 0x10) | ((tempRaw >> 8) & 0x07));
  b[4] = tempRaw & 0xFF;
  b[5] = hum;
  b[6] = windRaw & 0xFF;
  b[7] = 0xFF;
  b[15] = crc8(b.data(), 15);
  uint8_t sum = 0;
  for (int i = 0; i < 16; i++) sum += b[i];
  b[16] = sum;
  return b;
}
void test_weather_wh24() {
  auto b = wh24(400 + 215, 64, 24, 270);  // 21.5 C, 64 %, 3.36 m/s, west
  WxReading w;
  TEST_ASSERT_TRUE(parseWeather(b.data(), b.size(), &w));
  TEST_ASSERT_EQUAL_UINT8(0x9A, w.id);
  TEST_ASSERT_TRUE(w.hasTemp);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 21.5f, w.tempC);
  TEST_ASSERT_EQUAL_UINT8(64, w.hum);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 3.36f, w.windMs);
  TEST_ASSERT_EQUAL_UINT16(270, w.windDir);
}
void test_weather_invalid_fields_flagged() {
  auto b = wh24(0x7FF, 0xFF, 0x1FF, 0x1FF);
  WxReading w;
  TEST_ASSERT_TRUE(parseWeather(b.data(), b.size(), &w));
  TEST_ASSERT_FALSE(w.hasTemp);
  TEST_ASSERT_FALSE(w.hasHum);
  TEST_ASSERT_FALSE(w.hasWind);
}
void test_weather_crc_failure_rejected() {
  auto b = wh24(615, 64, 24, 270);
  b[4] ^= 1;
  WxReading w;
  TEST_ASSERT_FALSE(parseWeather(b.data(), b.size(), &w));
}
void test_truncated_weather() {
  auto b = wh24(615, 64, 24, 270);
  WxReading w;
  TEST_ASSERT_FALSE(parseWeather(b.data(), 16, &w));
  b[0] = 0x31;
  TEST_ASSERT_FALSE(parseWeather(b.data(), b.size(), &w));
}

void test_rfid_needs_5s_hopping() {
  SweepStats s;
  // A reader hopping strong carriers across the band.
  for (uint32_t t = 0; t < 4000; t += 100) s.add(902.25f + (t / 100 % 50) * 0.5f, -45, t);
  TEST_ASSERT_FALSE(s.rfidLikely(4000));
  for (uint32_t t = 4000; t < 7000; t += 100) s.add(902.25f + (t / 100 % 50) * 0.5f, -45, t);
  TEST_ASSERT_TRUE(s.rfidLikely(7000));
}
void test_single_carrier_not_rfid() {
  SweepStats s;
  for (uint32_t t = 0; t < 10000; t += 100) s.add(915.0f, -40, t);
  TEST_ASSERT_FALSE(s.rfidLikely(10000));
}
void test_sweep_out_of_band_ignored() {
  SweepStats s;
  s.add(850.0f, -30, 0);
  s.add(930.0f, -30, 0);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, -140.0f, s.peak(0));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_meshtastic_header);
  RUN_TEST(test_truncated_meshtastic);
  RUN_TEST(test_meshcore_advert_with_position);
  RUN_TEST(test_meshcore_advert_transport_route_no_pos);
  RUN_TEST(test_meshcore_other_payload_type_rejected);
  RUN_TEST(test_truncated_meshcore_advert);
  RUN_TEST(test_meshcore_long_name_clamped);
  RUN_TEST(test_lorawan_uplink_devaddr_and_net);
  RUN_TEST(test_lorawan_join_request);
  RUN_TEST(test_truncated_lorawan);
  RUN_TEST(test_zwave_frame);
  RUN_TEST(test_zwave_trailing_bytes_ok);
  RUN_TEST(test_zwave_bad_checksum);
  RUN_TEST(test_truncated_zwave);
  RUN_TEST(test_weather_wh24);
  RUN_TEST(test_weather_invalid_fields_flagged);
  RUN_TEST(test_weather_crc_failure_rejected);
  RUN_TEST(test_truncated_weather);
  RUN_TEST(test_rfid_needs_5s_hopping);
  RUN_TEST(test_single_carrier_not_rfid);
  RUN_TEST(test_sweep_out_of_band_ignored);
  return UNITY_END();
}
