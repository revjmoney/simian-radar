#include "CamDb.h"
#include <math.h>
#include <string.h>
#include "Geo.h"

static const size_t kHeaderLen = 32;
static const size_t kRecLen = 12;

const char* brandLabel(uint8_t brand) {
  static const char* labels[] = {"ALPR", "FLOCK", "VIGILANT", "GENETEC", "AXON",
                                 "ELSAG", "REKOR", "VERKADA", "NEOLOGY"};
  return brand < sizeof(labels) / sizeof(labels[0]) ? labels[brand] : "ALPR";
}

template <typename T>
static T rd(const uint8_t* p) {
  T v;
  memcpy(&v, p, sizeof v);
  return v;
}

bool CamDb::open(const uint8_t* data, size_t len) {
  data_ = nullptr;
  count_ = 0;
  if (!data || len < kHeaderLen || memcmp(data, "SRDB", 4) != 0) return false;
  if (rd<uint16_t>(data + 4) != 1) return false;
  uint16_t q = rd<uint16_t>(data + 6), rows = rd<uint16_t>(data + 12), cols = rd<uint16_t>(data + 14);
  uint32_t count = rd<uint32_t>(data + 16);
  size_t cells = (size_t)rows * cols;
  if (cells > 0 && q == 0) return false;
  size_t indexLen = (cells + 1) * 4;
  if (len != kHeaderLen + indexLen + (size_t)count * kRecLen) return false;
  const uint8_t* index = data + kHeaderLen;
  uint32_t prev = 0;
  for (size_t i = 0; i <= cells; i++) {
    uint32_t v = rd<uint32_t>(index + i * 4);
    if (v < prev || v > count) return false;
    prev = v;
  }
  if (prev != count || rd<uint32_t>(index) != 0) return false;
  q_ = q; rows_ = rows; cols_ = cols; count_ = count;
  lat0_ = rd<int16_t>(data + 8);
  lon0_ = rd<int16_t>(data + 10);
  index_ = index;
  records_ = index + indexLen;
  data_ = data;
  return true;
}

uint32_t CamDb::idx(size_t cell) const { return rd<uint32_t>(index_ + cell * 4); }

CamRec CamDb::rec(uint32_t i) const {
  const uint8_t* p = records_ + (size_t)i * kRecLen;
  CamRec r;
  r.latE7 = rd<int32_t>(p);
  r.lonE7 = rd<int32_t>(p + 4);
  r.dir = rd<uint16_t>(p + 8);
  r.brand = p[10];
  r.flags = p[11];
  return r;
}

size_t CamDb::nearest(double lat, double lon, float maxM, NearCam* out, size_t maxOut,
                      uint32_t* withinCount) const {
  size_t n = 0;
  uint32_t within = 0;
  if (data_ && rows_ && cols_) {
    long r0 = (long)floor((lat - lat0_) * q_), c0 = (long)floor((lon - lon0_) * q_);
    for (long r = r0 - 1; r <= r0 + 1; r++) {
      if (r < 0 || r >= rows_) continue;
      for (long c = c0 - 1; c <= c0 + 1; c++) {
        if (c < 0 || c >= cols_) continue;
        size_t cell = (size_t)r * cols_ + c;
        for (uint32_t i = idx(cell); i < idx(cell + 1); i++) {
          CamRec cr = rec(i);
          double clat = cr.latE7 / 1e7, clon = cr.lonE7 / 1e7;
          float d = (float)haversineM(lat, lon, clat, clon);
          if (d > maxM) continue;
          within++;
          // Insertion into the sorted top-maxOut list.
          size_t pos = n;
          while (pos > 0 && out[pos - 1].distM > d) pos--;
          if (pos >= maxOut) continue;
          size_t last = n < maxOut ? n : maxOut - 1;
          for (size_t k = last; k > pos; k--) out[k] = out[k - 1];
          out[pos] = {cr, d, (float)bearingDeg(lat, lon, clat, clon)};
          if (n < maxOut) n++;
        }
      }
    }
  }
  if (withinCount) *withinCount = within;
  return n;
}
