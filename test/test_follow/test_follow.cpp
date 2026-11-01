#include <unity.h>
#include <string.h>
#include "FollowDetector.h"

void setUp() {}
void tearDown() {}

static const uint32_t MIN = 60000;
static const double kLat = 33.6891, kLon = -78.8867;
static const double kHalfMileLat = 0.0073;  // ~0.5 mi of latitude

static void key(uint8_t k[8], uint8_t v) { memset(k, v, 8); }

// Drives north at a steady pace, seeing the tracker every minute.
static void ride(FollowDetector& f, const uint8_t k[8], BleKind kind, uint32_t minutes, double latPerMin) {
  for (uint32_t m = 0; m <= minutes; m++)
    f.observe(k, kind, -60, m * MIN, true, kLat + m * latPerMin, kLon);
}

void test_alerts_after_10min_and_half_mile() {
  FollowDetector f;
  uint8_t k[8]; key(k, 1);
  ride(f, k, BleKind::FindMySeparated, 11, kHalfMileLat / 10);
  FollowAlert a;
  TEST_ASSERT_TRUE(f.poll(11 * MIN, &a));
  TEST_ASSERT_EQUAL_MEMORY(k, a.key, 8);
  TEST_ASSERT_EQUAL_INT((int)BleKind::FindMySeparated, (int)a.kind);
}
void test_no_alert_if_stationary_30min() {
  FollowDetector f;
  uint8_t k[8]; key(k, 2);
  ride(f, k, BleKind::Tile, 30, 0);
  FollowAlert a;
  TEST_ASSERT_FALSE(f.poll(30 * MIN, &a));
}
void test_no_alert_if_moved_but_only_5min() {
  FollowDetector f;
  uint8_t k[8]; key(k, 3);
  ride(f, k, BleKind::FindMySeparated, 5, kHalfMileLat);
  FollowAlert a;
  TEST_ASSERT_FALSE(f.poll(5 * MIN, &a));
}
void test_no_alert_if_last_seen_over_2min_ago() {
  FollowDetector f;
  uint8_t k[8]; key(k, 4);
  ride(f, k, BleKind::FindMySeparated, 11, kHalfMileLat / 5);
  FollowAlert a;
  TEST_ASSERT_FALSE(f.poll(14 * MIN, &a));
}
void test_alerts_once_only() {
  FollowDetector f;
  uint8_t k[8]; key(k, 5);
  ride(f, k, BleKind::SmartTag, 12, kHalfMileLat / 5);
  FollowAlert a;
  TEST_ASSERT_TRUE(f.poll(12 * MIN, &a));
  f.observe(k, BleKind::SmartTag, -60, 12 * MIN + 1000, true, kLat + 0.05, kLon);
  TEST_ASSERT_FALSE(f.poll(12 * MIN + 2000, &a));
}
void test_near_owner_never_alerts() {
  FollowDetector f;
  uint8_t k[8]; key(k, 6);
  ride(f, k, BleKind::FindMyNearOwner, 30, kHalfMileLat / 5);
  FollowAlert a;
  TEST_ASSERT_FALSE(f.poll(30 * MIN, &a));
  TEST_ASSERT_EQUAL_UINT(0, f.size());
}
void test_glasses_never_tracked() {
  FollowDetector f;
  uint8_t k[8]; key(k, 7);
  ride(f, k, BleKind::GlassesMeta, 30, kHalfMileLat / 5);
  TEST_ASSERT_EQUAL_UINT(0, f.size());
}
void test_ignore_suppresses() {
  FollowDetector f;
  uint8_t k[8]; key(k, 8);
  f.ignore(k);
  ride(f, k, BleKind::FindMySeparated, 12, kHalfMileLat / 5);
  FollowAlert a;
  TEST_ASSERT_FALSE(f.poll(12 * MIN, &a));
}
void test_table_caps_at_64() {
  FollowDetector f;
  uint8_t k[8];
  for (int i = 0; i < 200; i++) {
    memset(k, 0, 8);
    k[0] = (uint8_t)i; k[1] = (uint8_t)(i >> 8);
    f.observe(k, BleKind::FindMySeparated, -70, (uint32_t)i * 100, true, kLat, kLon);
  }
  TEST_ASSERT_EQUAL_UINT(64, f.size());
}
void test_unseen_5min_dropped() {
  FollowDetector f;
  uint8_t k[8]; key(k, 9);
  f.observe(k, BleKind::Tile, -60, 0, true, kLat, kLon);
  FollowAlert a;
  f.poll(6 * MIN, &a);
  TEST_ASSERT_EQUAL_UINT(0, f.size());
}
void test_no_fix_does_not_start_distance() {
  FollowDetector f;
  uint8_t k[8]; key(k, 10);
  f.observe(k, BleKind::FindMySeparated, -60, 0, false, 0.0, 0.0);
  f.observe(k, BleKind::FindMySeparated, -60, 11 * MIN, true, kLat, kLon);
  FollowAlert a;
  TEST_ASSERT_FALSE(f.poll(11 * MIN, &a));
}

void test_google_tag_tracked() {
  FollowDetector f;
  uint8_t k[8]; key(k, 11);
  ride(f, k, BleKind::GoogleFmdn, 11, kHalfMileLat / 10);
  FollowAlert a;
  TEST_ASSERT_TRUE(f.poll(11 * MIN, &a));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_alerts_after_10min_and_half_mile);
  RUN_TEST(test_no_alert_if_stationary_30min);
  RUN_TEST(test_no_alert_if_moved_but_only_5min);
  RUN_TEST(test_no_alert_if_last_seen_over_2min_ago);
  RUN_TEST(test_alerts_once_only);
  RUN_TEST(test_near_owner_never_alerts);
  RUN_TEST(test_glasses_never_tracked);
  RUN_TEST(test_ignore_suppresses);
  RUN_TEST(test_table_caps_at_64);
  RUN_TEST(test_unseen_5min_dropped);
  RUN_TEST(test_no_fix_does_not_start_distance);
  RUN_TEST(test_google_tag_tracked);
  return UNITY_END();
}
