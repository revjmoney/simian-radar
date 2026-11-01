#include "MarkStore.h"
#include <math.h>
#include <string.h>

static uint8_t buf[8 + kMaxMarks * 14];

bool MarkStore::begin() {
  count_ = 0;
  size_t len = 0;
  if (!io_.load(buf, sizeof buf, &len)) return true;  // no file yet: start empty
  if (len < kHeaderLen || memcmp(buf, "SRMK", 4) != 0) return true;
  uint16_t version, n;
  memcpy(&version, buf + 4, 2);
  memcpy(&n, buf + 6, 2);
  if (version != 1 || n > kMaxMarks || len != kHeaderLen + n * kRecLen) return true;
  for (size_t i = 0; i < n; i++) {
    const uint8_t* p = buf + kHeaderLen + i * kRecLen;
    Mark& m = marks_[i];
    memcpy(&m.latE7, p, 4);
    memcpy(&m.lonE7, p + 4, 4);
    memcpy(&m.unixTime, p + 8, 4);
    m.brand = p[12];
    m.flags = p[13];
  }
  count_ = n;
  return true;
}

bool MarkStore::persist() {
  uint16_t version = 1, n = (uint16_t)count_;
  memcpy(buf, "SRMK", 4);
  memcpy(buf + 4, &version, 2);
  memcpy(buf + 6, &n, 2);
  for (size_t i = 0; i < count_; i++) {
    uint8_t* p = buf + kHeaderLen + i * kRecLen;
    const Mark& m = marks_[i];
    memcpy(p, &m.latE7, 4);
    memcpy(p + 4, &m.lonE7, 4);
    memcpy(p + 8, &m.unixTime, 4);
    p[12] = m.brand;
    p[13] = m.flags;
  }
  return io_.save(buf, kHeaderLen + count_ * kRecLen);
}

AddResult MarkStore::add(bool hasFix, double lat, double lon, uint32_t unixTime, uint8_t brand) {
  if (!hasFix) return AddResult::NoFix;
  if (count_ >= kMaxMarks) return AddResult::Full;
  marks_[count_] = {(int32_t)lround(lat * 1e7), (int32_t)lround(lon * 1e7), unixTime, brand, 0};
  count_++;
  if (!persist()) {
    count_--;
    return AddResult::IoError;
  }
  return AddResult::Ok;
}
