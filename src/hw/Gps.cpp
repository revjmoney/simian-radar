#include "Gps.h"
#include <Arduino.h>
#include <TinyGPSPlus.h>
#include "Config.h"

static TinyGPSPlus gps;

// Satellites in view, from the GSV sentences TinyGPSPlus doesn't keep.
struct SatSlot { SatInfo s; uint32_t ms; bool used; };
static SatSlot sats[40];
static char line[100];
static size_t lineLen = 0;

static void onLine(uint32_t now) {
  SatInfo got[4];
  size_t n = parseGsv(line, got, 4);
  for (size_t i = 0; i < n; i++) {
    SatSlot* slot = nullptr;
    SatSlot* oldest = &sats[0];
    for (auto& x : sats) {
      if (x.used && x.s.prn == got[i].prn && x.s.sys == got[i].sys) { slot = &x; break; }
      if (!x.used) oldest = &x;
      else if (oldest->used && (int32_t)(x.ms - oldest->ms) < 0) oldest = &x;
    }
    if (!slot) slot = oldest;
    slot->s = got[i];
    slot->ms = now;
    slot->used = true;
  }
}

// Days since 1970-01-01 for a civil date (Howard Hinnant's algorithm).
static uint32_t unixFrom(int y, unsigned m, unsigned d, unsigned hh, unsigned mm, unsigned ss) {
  y -= m <= 2;
  int era = y / 400;
  unsigned yoe = (unsigned)(y - era * 400);
  unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  long days = (long)era * 146097 + (long)doe - 719468;
  return (uint32_t)(days * 86400L + hh * 3600L + mm * 60L + ss);
}

bool gpsBegin() {
  pinMode(kPinGpsEn, OUTPUT);
  digitalWrite(kPinGpsEn, LOW);  // active LOW: power the L76K
  pinMode(kPinGpsReset, OUTPUT);
  digitalWrite(kPinGpsReset, LOW);
  delay(20);
  digitalWrite(kPinGpsReset, HIGH);
  Serial1.begin(9600, SERIAL_8N1, kPinEspRxFromGps, kPinEspTxToGps);
  return true;
}

bool gpsPoll(Fix* out) {
  uint32_t now = millis();
  while (Serial1.available()) {
    char c = Serial1.read();
    gps.encode(c);
    if (c == '
' || c == '') {
      line[lineLen] = 0;
      if (lineLen) onLine(now);
      lineLen = 0;
    } else if (lineLen < sizeof line - 1) {
      line[lineLen++] = c;
    }
  }
  out->sats = gps.satellites.isValid() ? gps.satellites.value() : 0;
  out->valid = gps.location.isValid() && gps.location.age() < 2000;
  if (!out->valid || !gps.location.isUpdated()) return false;  // report each new fix once (~1 Hz)
  out->lat = gps.location.lat();
  out->lon = gps.location.lng();
  out->speedMps = gps.speed.isValid() ? (float)gps.speed.mps() : 0;
  out->courseDeg = gps.course.isValid() ? (float)gps.course.deg() : 0;
  out->unixTime = gps.date.isValid() && gps.time.isValid()
                      ? unixFrom(gps.date.year(), gps.date.month(), gps.date.day(), gps.time.hour(),
                                 gps.time.minute(), gps.time.second())
                      : 0;
  return true;
}

uint32_t gpsChars() { return gps.charsProcessed(); }

size_t gpsSats(SatInfo* out, size_t max, uint32_t now) {
  size_t n = 0;
  for (auto& x : sats) {
    if (x.used && now - x.ms > 5000) x.used = false;
    if (x.used && n < max) out[n++] = x.s;
  }
  return n;
}

float gpsHdop() { return gps.hdop.isValid() ? gps.hdop.hdop() : 0; }
float gpsAltM() { return gps.altitude.isValid() ? (float)gps.altitude.meters() : 0; }
