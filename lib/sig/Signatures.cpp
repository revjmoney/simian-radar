#include "Signatures.h"
#include <ctype.h>
#include <string.h>

// Flock signatures: published facts from colonelpanichacks/flock-you (MIT), main.cpp, 2026-09-17
// (firmware-derived set). Generic chip OUIs from that list are left out on purpose: they match
// ordinary ESP32 gear, including our own units.
static const uint8_t kFlockOuis[][3] = {
    {0xB4, 0x1E, 0x52},  // Flock Safety IEEE MA-L
    {0x00, 0x03, 0x7F},  // Qualcomm Atheros QCA9377 firmware default MACs
};
static const char* const kFlockNameKeywords[] = {"penguin", "fs ext battery"};
static const uint16_t kFlockBleCompany = 0x09C8;
static const uint16_t kRavenSvcLo = 0x3100, kRavenSvcHi = 0x3500;

static const uint16_t kAppleCompany = 0x004C;
static const uint8_t kFindMyType = 0x12;

static bool containsNoCase(const char* hay, const char* needle) {
  if (!hay) return false;
  size_t n = strlen(needle);
  for (const char* h = hay; *h; h++) {
    size_t i = 0;
    while (i < n && h[i] && tolower((unsigned char)h[i]) == needle[i]) i++;
    if (i == n) return true;
  }
  return false;
}

static bool startsWithNoCase(const char* s, const char* prefix) {
  if (!s) return false;
  for (; *prefix; s++, prefix++)
    if (tolower((unsigned char)*s) != *prefix) return false;
  return true;
}

static bool hasSvc(const BleAdv& a, uint16_t lo, uint16_t hi) {
  for (size_t i = 0; i < a.svc16Count; i++)
    if (a.svc16[i] >= lo && a.svc16[i] <= hi) return true;
  return false;
}

BleKind classifyBle(const BleAdv& a) {
  if (a.mfg && a.mfgLen >= 2) {
    uint16_t company = (uint16_t)(a.mfg[0] | (a.mfg[1] << 8));
    switch (company) {
      case 0x01AB: case 0x058E: case 0x0D53: return BleKind::GlassesMeta;
      case 0x03C2: return BleKind::GlassesSnap;
      case kFlockBleCompany: return BleKind::Flock;
      case kAppleCompany:
        if (a.mfgLen >= 4 && a.mfg[2] == kFindMyType) {
          if (a.mfg[3] == 0x19) return BleKind::FindMySeparated;
          if (a.mfg[3] == 0x02) return BleKind::FindMyNearOwner;
        }
        break;
    }
  }
  // Google Find My Device network accessories: Eddystone service data, FMDN frame types 0x40/0x41.
  if (a.sdUuid == 0xFEAA && a.sd && a.sdLen >= 1 && (a.sd[0] == 0x40 || a.sd[0] == 0x41))
    return BleKind::GoogleFmdn;
  if (hasSvc(a, 0xFEEC, 0xFEED)) return BleKind::Tile;
  if (hasSvc(a, 0xFD5A, 0xFD5A)) return BleKind::SmartTag;
  if (hasSvc(a, kRavenSvcLo, kRavenSvcHi)) return BleKind::Flock;
  for (const char* k : kFlockNameKeywords)
    if (containsNoCase(a.name, k)) return BleKind::Flock;
  return BleKind::Other;
}

bool trackerKey(const BleAdv& a, BleKind k, uint8_t out[8]) {
  // Separated Find My adverts carry a public-key fragment that stays put while the address rotates.
  if (k == BleKind::FindMySeparated && a.mfg && a.mfgLen >= 13) {
    memcpy(out, a.mfg + 5, 8);
    return true;
  }
  if (k == BleKind::GoogleFmdn && a.sd && a.sdLen >= 9) {  // ephemeral ID, rotates ~17 min
    memcpy(out, a.sd + 1, 8);
    return true;
  }
  memcpy(out, a.addr, 6);
  out[6] = out[7] = 0;
  return true;
}

bool isFlockWifi(const char* ssid, const uint8_t bssid[6]) {
  if (bssid)
    for (const auto& oui : kFlockOuis)
      if (memcmp(bssid, oui, 3) == 0) return true;
  if (startsWithNoCase(ssid, "flock-")) return true;
  for (const char* k : kFlockNameKeywords)
    if (containsNoCase(ssid, k)) return true;
  return false;
}

const char* bleLabel(BleKind k) {
  switch (k) {
    case BleKind::GlassesMeta: return "META GLASS";
    case BleKind::GlassesSnap: return "SNAP GLASS";
    case BleKind::FindMySeparated:
    case BleKind::FindMyNearOwner: return "AIRTAG";
    case BleKind::Tile: return "TILE";
    case BleKind::SmartTag: return "SMARTTAG";
    case BleKind::Flock: return "FLOCK";
    case BleKind::GoogleFmdn: return "GOOGLE TAG";
    default: return "";
  }
}
