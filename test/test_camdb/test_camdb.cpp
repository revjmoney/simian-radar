#include <unity.h>
#include <stdio.h>
#include <string.h>
#include <vector>
#include "CamDb.h"

static std::vector<uint8_t> img;

static std::vector<uint8_t> loadFixture() {
  std::vector<uint8_t> v;
  FILE* f = fopen("test/fixtures/small.bin", "rb");
  TEST_ASSERT_NOT_NULL_MESSAGE(f, "fixture missing");
  uint8_t buf[4096];
  size_t n;
  while ((n = fread(buf, 1, sizeof buf, f)) > 0) v.insert(v.end(), buf, buf + n);
  fclose(f);
  return v;
}

void setUp() { img = loadFixture(); }
void tearDown() {}

static const double kLat = 33.6891, kLon = -78.8867;

void test_open_ok() {
  CamDb db;
  TEST_ASSERT_TRUE(db.open(img.data(), img.size()));
  TEST_ASSERT_EQUAL_UINT32(6, db.count());
}
void test_open_rejects_bad_magic() {
  img[0] = 'X';
  CamDb db;
  TEST_ASSERT_FALSE(db.open(img.data(), img.size()));
  TEST_ASSERT_FALSE(db.isOpen());
}
void test_open_rejects_short_buffer() {
  CamDb db;
  TEST_ASSERT_FALSE(db.open(img.data(), 20));
  TEST_ASSERT_FALSE(db.open(img.data(), img.size() - 1));
}
void test_open_rejects_wrong_version() {
  img[4] = 2;
  CamDb db;
  TEST_ASSERT_FALSE(db.open(img.data(), img.size()));
}
void test_open_rejects_index_past_end() {
  uint32_t big = 999999;
  memcpy(&img[32 + 4], &big, 4);  // second index entry points past the records
  CamDb db;
  TEST_ASSERT_FALSE(db.open(img.data(), img.size()));
}
void test_nearest_sorted() {
  CamDb db;
  db.open(img.data(), img.size());
  NearCam out[4];
  uint32_t within = 0;
  size_t n = db.nearest(kLat, kLon, 8046.7f, out, 4, &within);
  TEST_ASSERT_EQUAL_UINT(4, n);
  TEST_ASSERT_FLOAT_WITHIN(5.0f, 161.0f, out[0].distM);
  TEST_ASSERT_EQUAL_STRING("FLOCK", brandLabel(out[0].rec.brand));
  TEST_ASSERT_EQUAL_UINT16(90, out[0].rec.dir);
  for (size_t i = 1; i < n; i++) TEST_ASSERT_TRUE(out[i - 1].distM <= out[i].distM);
}
void test_within_count_5mi() {
  CamDb db;
  db.open(img.data(), img.size());
  NearCam out[2];
  uint32_t within = 0;
  db.nearest(kLat, kLon, 8046.7f, out, 2, &within);
  TEST_ASSERT_EQUAL_UINT32(4, within);  // the 6 mi and Anchorage cameras are excluded
}
void test_finds_camera_across_cell_edge() {
  CamDb db;
  db.open(img.data(), img.size());
  NearCam out[1];
  uint32_t within = 0;
  size_t n = db.nearest(33.7499, -78.8867, 100.0f, out, 1, &within);
  TEST_ASSERT_EQUAL_UINT(1, n);
  TEST_ASSERT_EQUAL_STRING("ALPR", brandLabel(out[0].rec.brand));
}
void test_alaska_latitude() {
  CamDb db;
  db.open(img.data(), img.size());
  NearCam out[2];
  uint32_t within = 0;
  size_t n = db.nearest(61.2181, -149.9003, 8046.7f, out, 2, &within);
  TEST_ASSERT_EQUAL_UINT(1, n);
}
void test_empty_area_returns_zero() {
  CamDb db;
  db.open(img.data(), img.size());
  NearCam out[2];
  uint32_t within = 7;
  TEST_ASSERT_EQUAL_UINT(0, db.nearest(45.0, -100.0, 8046.7f, out, 2, &within));
  TEST_ASSERT_EQUAL_UINT32(0, within);
  TEST_ASSERT_EQUAL_UINT(0, db.nearest(0.0, 0.0, 8046.7f, out, 2, &within));
}
void test_vigilant_label() { TEST_ASSERT_EQUAL_STRING("VIGILANT", brandLabel(2)); }

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_open_ok);
  RUN_TEST(test_open_rejects_bad_magic);
  RUN_TEST(test_open_rejects_short_buffer);
  RUN_TEST(test_open_rejects_wrong_version);
  RUN_TEST(test_open_rejects_index_past_end);
  RUN_TEST(test_nearest_sorted);
  RUN_TEST(test_within_count_5mi);
  RUN_TEST(test_finds_camera_across_cell_edge);
  RUN_TEST(test_alaska_latitude);
  RUN_TEST(test_empty_area_returns_zero);
  RUN_TEST(test_vigilant_label);
  return UNITY_END();
}
