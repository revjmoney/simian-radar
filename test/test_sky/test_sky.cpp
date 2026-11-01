#include <unity.h>
#include <string.h>
#include "Sky.h"

void setUp() {}
void tearDown() {}

void test_parse_gsv_four_sats() {
  SatInfo s[8];
  size_t n = parseGsv("$GPGSV,3,1,11,10,63,137,17,07,61,098,15,05,59,290,20,08,54,157,30*70", s, 8);
  TEST_ASSERT_EQUAL_UINT(4, n);
  TEST_ASSERT_EQUAL_UINT8(10, s[0].prn);
  TEST_ASSERT_EQUAL_UINT8(17, s[0].snr);
  TEST_ASSERT_EQUAL_CHAR('G', s[0].sys);
  TEST_ASSERT_EQUAL_UINT8(30, s[3].snr);
}
void test_parse_gsv_empty_snr_and_systems() {
  SatInfo s[8];
  size_t n = parseGsv("$BDGSV,1,1,02,201,45,120,,203,30,200,38*6A", s, 8);
  TEST_ASSERT_EQUAL_UINT(2, n);
  TEST_ASSERT_EQUAL_CHAR('B', s[0].sys);
  TEST_ASSERT_EQUAL_UINT8(0, s[0].snr);
  TEST_ASSERT_EQUAL_UINT8(38, s[1].snr);
  TEST_ASSERT_EQUAL_CHAR('R', parseGsv("$GLGSV,1,1,01,70,10,20,25*00", s, 8) ? s[0].sys : '?');
}
void test_parse_gsv_truncated() {
  SatInfo s[8];
  TEST_ASSERT_EQUAL_UINT(1, parseGsv("$GPGSV,3,1,11,10,63,137,17,07,6", s, 8));
  TEST_ASSERT_EQUAL_UINT(0, parseGsv("$GPGSV,3", s, 8));
  TEST_ASSERT_EQUAL_UINT(0, parseGsv("$GPRMC,123519,A", s, 8));
  TEST_ASSERT_EQUAL_UINT(0, parseGsv("", s, 8));
}
void test_parse_gsv_respects_max() {
  SatInfo s[2];
  TEST_ASSERT_EQUAL_UINT(2, parseGsv("$GPGSV,3,1,11,10,63,137,17,07,61,098,15,05,59,290,20,08,54,157,30*70", s, 2));
}

static void sats(SkyGuard& g, uint8_t snr, uint32_t now) {
  SatInfo s[6];
  for (int i = 0; i < 6; i++) s[i] = {(uint8_t)(i + 1), 'G', snr};
  g.onSats(s, 6, now);
}
static Fix fixAt(double lat, double lon, uint32_t unix) { return {true, lat, lon, 20, 0, 8, unix}; }

void test_jamming_when_strong_sats_drop() {
  SkyGuard g;
  const char* t;
  for (uint32_t s = 0; s < 10; s++) {
    sats(g, 40, s * 1000);
    g.onFix(fixAt(33.7, -78.9, 1000 + s), s * 1000);
    g.tick(s * 1000, true);
  }
  for (uint32_t s = 10; s < 20; s++) {
    sats(g, 22, s * 1000);  // everything 18 dB down
    g.tick(s * 1000, false);
  }
  TEST_ASSERT_TRUE(g.poll(20000, &t));
  TEST_ASSERT_EQUAL_STRING("JAMMING?", t);
}
void test_no_jamming_when_indoors_from_start() {
  SkyGuard g;
  const char* t;
  for (uint32_t s = 0; s < 60; s++) {
    sats(g, 15, s * 1000);
    g.tick(s * 1000, false);
  }
  TEST_ASSERT_FALSE(g.poll(60000, &t));
}
void test_no_jamming_in_tunnel_without_snr_report() {
  SkyGuard g;
  const char* t;
  for (uint32_t s = 0; s < 10; s++) {
    sats(g, 40, s * 1000);
    g.onFix(fixAt(33.7, -78.9, 1000 + s), s * 1000);
    g.tick(s * 1000, true);
  }
  for (uint32_t s = 10; s < 20; s++) g.tick(s * 1000, false);  // no satellites heard at all
  TEST_ASSERT_FALSE(g.poll(20000, &t));
}
void test_spoof_on_position_jump() {
  SkyGuard g;
  const char* t;
  g.onFix(fixAt(33.7000, -78.9000, 1000), 1000);
  g.onFix(fixAt(33.7100, -78.9000, 1001), 2000);  // 1.1 km in 1 s
  TEST_ASSERT_TRUE(g.poll(2000, &t));
  TEST_ASSERT_EQUAL_STRING("SPOOFING?", t);
}
void test_spoof_on_clock_jump() {
  SkyGuard g;
  const char* t;
  g.onFix(fixAt(33.7, -78.9, 1000), 1000);
  g.onFix(fixAt(33.7, -78.9, 5000), 2000);  // GPS time jumped an hour in a second
  TEST_ASSERT_TRUE(g.poll(2000, &t));
}
void test_no_spoof_on_normal_drive() {
  SkyGuard g;
  const char* t;
  for (uint32_t s = 0; s < 60; s++) g.onFix(fixAt(33.7 + s * 0.0002, -78.9, 1000 + s), s * 1000);  // ~50 mph
  TEST_ASSERT_FALSE(g.poll(60000, &t));
}
void test_no_spoof_after_long_gap() {
  SkyGuard g;
  const char* t;
  g.onFix(fixAt(33.7, -78.9, 1000), 1000);
  g.onFix(fixAt(33.8, -78.9, 1600), 601000);  // 11 km after 10 minutes in a tunnel: fine
  TEST_ASSERT_FALSE(g.poll(601000, &t));
}
void test_no_spoof_on_midnight_date_lag() {
  SkyGuard g;
  const char* t;
  g.onFix(fixAt(33.7, -78.9, 1793491199), 1000);          // 23:59:59
  g.onFix(fixAt(33.7, -78.9, 1793491200 - 86400), 2000);  // new time, date not updated yet
  TEST_ASSERT_FALSE(g.poll(2000, &t));
}
void test_alert_once_per_2min() {
  SkyGuard g;
  const char* t;
  g.onFix(fixAt(33.70, -78.9, 1000), 1000);
  g.onFix(fixAt(33.71, -78.9, 1001), 2000);
  TEST_ASSERT_TRUE(g.poll(2000, &t));
  g.onFix(fixAt(33.72, -78.9, 1002), 3000);
  TEST_ASSERT_FALSE(g.poll(3000, &t));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_parse_gsv_four_sats);
  RUN_TEST(test_parse_gsv_empty_snr_and_systems);
  RUN_TEST(test_parse_gsv_truncated);
  RUN_TEST(test_parse_gsv_respects_max);
  RUN_TEST(test_jamming_when_strong_sats_drop);
  RUN_TEST(test_no_jamming_when_indoors_from_start);
  RUN_TEST(test_no_jamming_in_tunnel_without_snr_report);
  RUN_TEST(test_spoof_on_position_jump);
  RUN_TEST(test_spoof_on_clock_jump);
  RUN_TEST(test_no_spoof_on_normal_drive);
  RUN_TEST(test_no_spoof_after_long_gap);
  RUN_TEST(test_no_spoof_on_midnight_date_lag);
  RUN_TEST(test_alert_once_per_2min);
  return UNITY_END();
}
