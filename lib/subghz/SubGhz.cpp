#include "SubGhz.h"
#include <string.h>

static uint32_t le32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

bool parseMeshtastic(const uint8_t* p, size_t n, uint32_t* from, uint32_t* to, uint8_t* hops) {
  if (!p || n < 16) return false;
  *to = le32(p);
  *from = le32(p + 4);
  uint8_t flags = p[12];
  uint8_t limit = flags & 0x07, start = flags >> 5;
  *hops = start >= limit ? start - limit : 0;
  return true;
}

// MeshCore constants (Packet.h, AdvertDataHelpers.h).
static const uint8_t kPayloadAdvert = 0x04;
static const size_t kPubKey = 32, kSignature = 64;
static const uint8_t kAdvLatLon = 0x10, kAdvFeat1 = 0x20, kAdvFeat2 = 0x40, kAdvName = 0x80;

bool parseMeshCoreAdvert(const uint8_t* p, size_t n, char name[32], double* lat, double* lon, bool* hasPos) {
  if (!p || n < 2) return false;
  uint8_t header = p[0];
  if (((header >> 2) & 0x0F) != kPayloadAdvert) return false;
  uint8_t route = header & 0x03;
  size_t i = 1;
  if (route == 0 || route == 3) i += 4;  // transport codes
  if (i >= n) return false;
  uint8_t pathLen = p[i++];
  size_t hashSize = (pathLen >> 6) + 1, count = pathLen & 63;
  if (hashSize > 3) return false;
  i += hashSize * count;
  i += kPubKey + 4 + kSignature;
  if (i >= n) return false;  // no app data
  uint8_t flags = p[i++];
  *hasPos = false;
  if (flags & kAdvLatLon) {
    if (i + 8 > n) return false;
    *lat = (int32_t)le32(p + i) / 1e6;
    *lon = (int32_t)le32(p + i + 4) / 1e6;
    *hasPos = *lat != 0 || *lon != 0;
    i += 8;
  }
  if (flags & kAdvFeat1) i += 2;
  if (flags & kAdvFeat2) i += 2;
  if (i > n) return false;
  name[0] = 0;
  if (flags & kAdvName) {
    size_t len = n - i < 31 ? n - i : 31;
    memcpy(name, p + i, len);
    name[len] = 0;
  }
  return true;
}

bool parseLoRaWan(const uint8_t* p, size_t n, uint8_t* mtype, uint32_t* devAddr) {
  if (!p || n < 1) return false;
  *mtype = p[0] >> 5;
  *devAddr = 0;
  switch (*mtype) {
    case 0: return n == 23;              // join request
    case 1: return n == 17 || n == 33;   // join accept (encrypted)
    case 2: case 3: case 4: case 5:      // data up/down
      if (n < 12) return false;
      *devAddr = le32(p + 1);
      return true;
    default:
      return false;                      // rejoin / proprietary
  }
}

const char* netName(uint32_t a) {
  // DevAddr prefixes from the LoRa Alliance NetID allocations.
  if ((a >> 25) == 0x13) return "TTN";                  // NetID 0x000013
  if ((a >> 25) == 0x24) return "Helium";               // legacy NetID 0x000024
  if ((a & 0xFFFFFC00u) == 0xFC014C00u) return "Helium";  // NetID 0xC00053
  return "LoRaWAN";
}

bool parseZWave(const uint8_t* p, size_t n, uint32_t* homeId, uint8_t* src, uint8_t* type) {
  if (!p || n < 10 || p[7] != n) return false;
  uint8_t cs = 0xFF;
  for (size_t i = 0; i + 1 < n; i++) cs ^= p[i];
  if (cs != p[n - 1]) return false;
  *homeId = ((uint32_t)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
  *src = p[4];
  *type = p[5] & 0x0F;
  return true;
}

static uint8_t crc8(const uint8_t* b, size_t n) {
  uint8_t c = 0;
  for (size_t i = 0; i < n; i++) {
    c ^= b[i];
    for (int k = 0; k < 8; k++) c = c & 0x80 ? (uint8_t)((c << 1) ^ 0x31) : (uint8_t)(c << 1);
  }
  return c;
}

// Layout per rtl_433's fineoffset WH24 decoder (facts only).
bool parseWeather(const uint8_t* b, size_t n, WxReading* out) {
  if (!b || n < 17 || b[0] != 0x24) return false;
  uint8_t sum = 0;
  for (int i = 0; i < 16; i++) sum += b[i];
  if (crc8(b, 16) != 0 || sum != b[16]) return false;
  memset(out, 0, sizeof *out);
  out->id = b[1];
  uint16_t dir = b[2] | ((b[3] & 0x80) << 1);
  uint16_t tempRaw = ((b[3] & 0x07) << 8) | b[4];
  uint16_t windRaw = b[6] | ((b[3] & 0x10) << 4);
  out->lowBatt = b[3] & 0x08;
  out->hasTemp = tempRaw != 0x7FF;
  out->tempC = (tempRaw - 400) * 0.1f;
  out->hasHum = b[5] != 0xFF;
  out->hum = b[5];
  out->hasWind = windRaw != 0x1FF && dir != 0x1FF;
  out->windMs = windRaw * 0.125f * 1.12f;
  out->windDir = dir;
  return true;
}

static const float kSweepLo = 902.0f, kStep = 0.5f, kStrongDbm = -60;
static const uint32_t kStrongRecentMs = 2000, kRfidHoldMs = 5000;
static const int kHopBins = 8;  // distinct strong channels at once = something hopping

void SweepStats::add(float mhz, float rssi, uint32_t now) {
  if (!init_) {
    for (int i = 0; i < kBins; i++) level_[i] = peak_[i] = -140;
    init_ = true;
  }
  int bin = (int)((mhz - kSweepLo) / kStep);
  if (mhz < kSweepLo || bin < 0 || bin >= kBins) return;
  level_[bin] = rssi;
  if (rssi > peak_[bin]) peak_[bin] = rssi;
  else peak_[bin] -= 0.2f;  // slow decay so old peaks fade
  if (rssi >= kStrongDbm) strongMs_[bin] = now | 1;
  int strong = 0;
  for (int i = 0; i < kBins; i++) strong += strongMs_[i] && now - strongMs_[i] <= kStrongRecentMs;
  bool hopping = strong >= kHopBins;
  if (hopping && !hopping_) hopSinceMs_ = now;
  hopping_ = hopping;
}

bool SweepStats::rfidLikely(uint32_t now) { return hopping_ && now - hopSinceMs_ >= kRfidHoldMs; }
