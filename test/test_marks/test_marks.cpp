#include <unity.h>
#include <string.h>
#include <vector>
#include "MarkStore.h"

struct MemIO : MarkIO {
  std::vector<uint8_t> file;
  bool failSave = false;
  bool load(uint8_t* buf, size_t cap, size_t* len) override {
    if (file.empty() || file.size() > cap) return false;
    memcpy(buf, file.data(), file.size());
    *len = file.size();
    return true;
  }
  bool save(const uint8_t* buf, size_t len) override {
    if (failSave) return false;
    file.assign(buf, buf + len);
    return true;
  }
};

void setUp() {}
void tearDown() {}

void test_add_and_count() {
  MemIO io;
  MarkStore s(io);
  TEST_ASSERT_TRUE(s.begin());
  TEST_ASSERT_EQUAL_INT((int)AddResult::Ok, (int)s.add(true, 33.6912, -78.8870, 1793500000, 1));
  TEST_ASSERT_EQUAL_UINT(1, s.count());
  TEST_ASSERT_EQUAL_INT32(336912000, s.at(0).latE7);
  TEST_ASSERT_EQUAL_INT32(-788870000, s.at(0).lonE7);
}
void test_no_fix_rejected() {
  MemIO io;
  MarkStore s(io);
  s.begin();
  TEST_ASSERT_EQUAL_INT((int)AddResult::NoFix, (int)s.add(false, 0, 0, 0, 0));
  TEST_ASSERT_EQUAL_UINT(0, s.count());
}
void test_store_full() {
  static MemIO io;
  static MarkStore s(io);
  s.begin();
  for (int i = 0; i < 500; i++) s.add(true, 33.0 + i * 0.001, -78.0, 1, 1);
  TEST_ASSERT_EQUAL_INT((int)AddResult::Full, (int)s.add(true, 34.0, -78.0, 1, 1));
  TEST_ASSERT_EQUAL_UINT(500, s.count());
}
void test_persist_roundtrip() {
  MemIO io;
  {
    MarkStore s(io);
    s.begin();
    s.add(true, 33.5, -78.5, 42, 2);
    s.add(true, 33.6, -78.6, 43, 1);
  }
  MarkStore s2(io);
  TEST_ASSERT_TRUE(s2.begin());
  TEST_ASSERT_EQUAL_UINT(2, s2.count());
  TEST_ASSERT_EQUAL_UINT32(43, s2.at(1).unixTime);
  TEST_ASSERT_EQUAL_UINT8(2, s2.at(0).brand);
}
void test_corrupt_file_starts_empty() {
  MemIO io;
  io.file = {'J', 'U', 'N', 'K', 1, 0, 5, 0};
  MarkStore s(io);
  s.begin();
  TEST_ASSERT_EQUAL_UINT(0, s.count());
}
void test_truncated_file_starts_empty() {
  MemIO io;
  {
    MarkStore s(io);
    s.begin();
    s.add(true, 33.5, -78.5, 42, 2);
  }
  io.file.resize(io.file.size() - 3);
  MarkStore s2(io);
  s2.begin();
  TEST_ASSERT_EQUAL_UINT(0, s2.count());
}
void test_io_error_reported() {
  MemIO io;
  io.failSave = true;
  MarkStore s(io);
  s.begin();
  TEST_ASSERT_EQUAL_INT((int)AddResult::IoError, (int)s.add(true, 33.5, -78.5, 42, 2));
  TEST_ASSERT_EQUAL_UINT(0, s.count());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_add_and_count);
  RUN_TEST(test_no_fix_rejected);
  RUN_TEST(test_store_full);
  RUN_TEST(test_persist_roundtrip);
  RUN_TEST(test_corrupt_file_starts_empty);
  RUN_TEST(test_truncated_file_starts_empty);
  RUN_TEST(test_io_error_reported);
  return UNITY_END();
}
