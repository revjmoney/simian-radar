#pragma once
#include <stddef.h>
#include <stdint.h>
#include "Config.h"
#include "MarkIO.h"

struct Mark { int32_t latE7; int32_t lonE7; uint32_t unixTime; uint8_t brand; uint8_t flags; };
enum class AddResult { Ok, Full, NoFix, IoError };

// Cameras the user marked with the 5 s hold, kept for review in the phone app.
// File layout: "SRMK", u16 version, u16 count, then 14-byte records.
class MarkStore {
 public:
  explicit MarkStore(MarkIO& io) : io_(io) {}
  bool begin();
  AddResult add(bool hasFix, double lat, double lon, uint32_t unixTime, uint8_t brand);
  size_t count() const { return count_; }
  const Mark& at(size_t i) const { return marks_[i]; }

 private:
  static const size_t kHeaderLen = 8, kRecLen = 14;
  MarkIO& io_;
  Mark marks_[kMaxMarks] = {};
  size_t count_ = 0;
  bool persist();
};
