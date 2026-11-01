#pragma once
#include <stddef.h>
#include <stdint.h>

enum class BleKind { Other, GlassesMeta, GlassesSnap, FindMySeparated, FindMyNearOwner, Tile, SmartTag, Flock,
                     GoogleFmdn };

struct BleAdv {
  uint8_t addr[6];
  int8_t rssi;
  const uint8_t* mfg;       // manufacturer data incl. the 2-byte company ID
  size_t mfgLen;
  const uint16_t* svc16;    // 16-bit service UUIDs
  size_t svc16Count;
  const char* name;         // may be null
  uint16_t sdUuid;          // first 16-bit service data UUID (0 if none)
  const uint8_t* sd;        // its data, after the UUID
  size_t sdLen;
};

BleKind classifyBle(const BleAdv& a);
// Stable id for a device across address changes where the payload allows it.
bool trackerKey(const BleAdv& a, BleKind k, uint8_t out[8]);
bool isFlockWifi(const char* ssid, const uint8_t bssid[6]);
const char* bleLabel(BleKind k);
