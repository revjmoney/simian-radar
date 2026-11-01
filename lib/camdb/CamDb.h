#pragma once
#include <stddef.h>
#include <stdint.h>

struct CamRec { int32_t latE7; int32_t lonE7; uint16_t dir; uint8_t brand; uint8_t flags; };
struct NearCam { CamRec rec; float distM; float bearingDeg; };

const char* brandLabel(uint8_t brand);

// Read-only view over a camdb.bin image (see tools/camdb/build_camdb.py for the format).
class CamDb {
 public:
  bool open(const uint8_t* data, size_t len);
  bool isOpen() const { return data_ != nullptr; }
  uint32_t count() const { return count_; }
  // Cameras within maxM of (lat, lon), nearest first. Searches the 3x3 cells around the position.
  size_t nearest(double lat, double lon, float maxM, NearCam* out, size_t maxOut,
                 uint32_t* withinCount) const;

 private:
  const uint8_t* data_ = nullptr;
  const uint8_t* index_ = nullptr;
  const uint8_t* records_ = nullptr;
  uint16_t q_ = 0, rows_ = 0, cols_ = 0;
  int16_t lat0_ = 0, lon0_ = 0;
  uint32_t count_ = 0;
  uint32_t idx(size_t cell) const;
  CamRec rec(uint32_t i) const;
};
