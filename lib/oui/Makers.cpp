#include "Makers.h"
#include <ctype.h>
#include <stddef.h>
#include <string.h>

struct OuiEntry { uint32_t oui; Maker maker; };
static const OuiEntry kTable[] = {
#include "MakersTable.inc"
};

bool isRandomMac(const uint8_t mac[6]) { return mac[0] & 0x02; }

Maker makerOf(const uint8_t mac[6]) {
  if (isRandomMac(mac)) return Maker::Random;
  uint32_t oui = ((uint32_t)mac[0] << 16) | (mac[1] << 8) | mac[2];
  size_t lo = 0, hi = sizeof kTable / sizeof kTable[0];
  while (lo < hi) {
    size_t mid = (lo + hi) / 2;
    if (kTable[mid].oui < oui) lo = mid + 1;
    else hi = mid;
  }
  return lo < sizeof kTable / sizeof kTable[0] && kTable[lo].oui == oui ? kTable[lo].maker : Maker::Unknown;
}

bool isCameraMaker(Maker m) { return m >= Maker::Ring && m <= Maker::Tuya; }

static bool startsNoCase(const char* s, const char* p) {
  for (; *p; s++, p++)
    if (tolower((unsigned char)*s) != *p) return false;
  return true;
}

// Setup / default network names used by camera makers. Deliberately excludes generic names like
// "IPC" that ordinary routers and printers also use.
Maker cameraFromSsid(const char* ssid) {
  static const struct { const char* prefix; Maker m; } kSsids[] = {
      {"hik-", Maker::Hikvision}, {"hikvision", Maker::Hikvision}, {"wyze", Maker::Wyze},
      {"ring-", Maker::Ring},     {"ring setup", Maker::Ring},     {"blink", Maker::Blink},
      {"arlo", Maker::Arlo},      {"eufy", Maker::Eufy},           {"reolink", Maker::Reolink},
      {"smartlife-", Maker::Tuya}, {"tuya", Maker::Tuya},
  };
  if (!ssid) return Maker::Unknown;
  for (const auto& e : kSsids)
    if (startsNoCase(ssid, e.prefix)) return e.m;
  return Maker::Unknown;
}

const char* makerName(Maker m) {
  static const char* names[] = {"?",       "random",  "AXON",    "FLOCK",  "RING",     "BLINK",  "WYZE",
                                "ARLO",    "EUFY",    "HIKVISION", "DAHUA", "REOLINK", "NEST",   "TUYA",
                                "Apple",   "Samsung", "Google",  "Amazon", "Microsoft", "Intel", "Motorola",
                                "Huawei",  "Xiaomi",  "OnePlus", "LG",     "Sony",     "Espressif"};
  size_t i = (size_t)m;
  return i < sizeof names / sizeof names[0] ? names[i] : "?";
}
