#include "Gps.h"
#include <Arduino.h>
#include <TinyGPSPlus.h>
#include "Config.h"

static TinyGPSPlus gps;

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
  while (Serial1.available()) gps.encode(Serial1.read());
  out->sats = gps.satellites.isValid() ? gps.satellites.value() : 0;
  out->valid = gps.location.isValid() && gps.location.age() < 2000;
  if (!out->valid) return false;
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
