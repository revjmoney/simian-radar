#include "DroneTable.h"
#include <string.h>
#include "opendroneid.h"

static const uint8_t kBleAppCode = 0x0D;
static const uint8_t kPackType = 0xF;

struct Decoded {
  bool hasId, hasPos, hasPilot;
  char id[21];
  double lat, lon, pilotLat, pilotLon;
  float altM, speedMps;
};

static void decodeOne(const uint8_t* msg, Decoded* out) {
  static ODID_UAS_Data uas;  // ~1 KB: keep it off the stack
  memset(&uas, 0, sizeof uas);
  switch (decodeOpenDroneID(&uas, msg)) {
    case ODID_MESSAGETYPE_BASIC_ID:
      if (uas.BasicIDValid[0] && uas.BasicID[0].UASID[0]) {
        strncpy(out->id, uas.BasicID[0].UASID, sizeof out->id - 1);
        out->hasId = true;
      }
      break;
    case ODID_MESSAGETYPE_LOCATION:
      if (uas.LocationValid && (uas.Location.Latitude != 0 || uas.Location.Longitude != 0)) {
        out->lat = uas.Location.Latitude;
        out->lon = uas.Location.Longitude;
        out->altM = uas.Location.AltitudeGeo;
        out->speedMps = uas.Location.SpeedHorizontal;
        out->hasPos = true;
      }
      break;
    case ODID_MESSAGETYPE_SYSTEM:
      if (uas.SystemValid && (uas.System.OperatorLatitude != 0 || uas.System.OperatorLongitude != 0)) {
        out->pilotLat = uas.System.OperatorLatitude;
        out->pilotLon = uas.System.OperatorLongitude;
        out->hasPilot = true;
      }
      break;
    default:
      break;  // unknown or unused message types are skipped
  }
}

// m points at the first message byte: either one 25-byte message or a pack of them.
static bool decodeMessages(const uint8_t* m, size_t len, Decoded* out) {
  if (len < ODID_MESSAGE_SIZE && !(len >= 3 && (m[0] >> 4) == kPackType)) return false;
  if ((m[0] >> 4) == kPackType) {
    if (len < 3 || m[1] != ODID_MESSAGE_SIZE) return false;
    size_t count = m[2];
    if (count == 0 || count > ODID_PACK_MAX_MESSAGES || 3 + count * ODID_MESSAGE_SIZE > len) return false;
    for (size_t i = 0; i < count; i++) decodeOne(m + 3 + i * ODID_MESSAGE_SIZE, out);
  } else {
    decodeOne(m, out);
  }
  return out->hasId || out->hasPos || out->hasPilot;
}

Drone* DroneTable::slot(const uint8_t mac[6], int8_t rssi, uint32_t now) {
  Drone* oldest = &drones_[0];
  for (auto& d : drones_) {
    if (d.used && memcmp(d.mac, mac, 6) == 0) {
      d.rssi = rssi;
      d.lastMs = now;
      return &d;
    }
    if (!d.used) { if (oldest->used) oldest = &d; }
    else if (oldest->used && (int32_t)(d.lastMs - oldest->lastMs) < 0) oldest = &d;
  }
  *oldest = Drone{};
  oldest->used = true;
  memcpy(oldest->mac, mac, 6);
  oldest->rssi = rssi;
  oldest->lastMs = now;
  return oldest;
}

static void merge(Drone* d, const Decoded& x) {
  if (x.hasId) { strncpy(d->id, x.id, sizeof d->id - 1); d->hasId = true; }
  if (x.hasPos) { d->lat = x.lat; d->lon = x.lon; d->altM = x.altM; d->speedMps = x.speedMps; d->hasPos = true; }
  if (x.hasPilot) { d->pilotLat = x.pilotLat; d->pilotLon = x.pilotLon; d->hasPilot = true; }
}

void DroneTable::onWifiIe(const uint8_t mac[6], const uint8_t* p, size_t len, int8_t rssi, uint32_t now) {
  Decoded x = {};
  if (!p || len < 2 || !decodeMessages(p + 1, len - 1, &x)) return;  // skip the counter byte
  merge(slot(mac, rssi, now), x);
}

void DroneTable::onBleServiceData(const uint8_t mac[6], const uint8_t* p, size_t len, int8_t rssi, uint32_t now) {
  Decoded x = {};
  if (!p || len < 3 || p[0] != kBleAppCode || !decodeMessages(p + 2, len - 2, &x)) return;
  merge(slot(mac, rssi, now), x);
}

size_t DroneTable::list(Drone* out, size_t max, uint32_t now) {
  size_t n = 0;
  for (auto& d : drones_) {
    if (d.used && now - d.lastMs > kExpireMs) d.used = false;
    if (!d.used || max == 0) continue;
    size_t pos = n;
    while (pos > 0 && out[pos - 1].rssi < d.rssi) pos--;
    if (pos >= max) continue;
    size_t last = n < max ? n : max - 1;
    for (size_t k = last; k > pos; k--) out[k] = out[k - 1];
    out[pos] = d;
    if (n < max) n++;
  }
  return n;
}
