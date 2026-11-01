#include "Sky.h"
#include <stdlib.h>
#include <string.h>
#include "Geo.h"

static const uint32_t kRepeatMs = 2UL * 60UL * 1000UL;
static const uint8_t kStrongSnr = 30;      // dB-Hz: a clear-sky satellite
static const float kJamDropDb = 10;
static const uint32_t kNoFixMs = 5000;
static const uint32_t kStrongWindowMs = 60000;
static const uint32_t kSatsFreshMs = 3000;
static const double kMaxSpeedMps = 67.06;  // 150 mph
static const double kJumpM = 500;
static const double kCheckGapS = 30;       // longer gaps (tunnels, power off) are not judged
static const double kClockSlackS = 10;

size_t parseGsv(const char* sentence, SatInfo* out, size_t max) {
  if (!sentence || strlen(sentence) < 6 || sentence[0] != '$' || strncmp(sentence + 3, "GSV", 3) != 0) return 0;
  char sys;
  if (!strncmp(sentence + 1, "GP", 2)) sys = 'G';
  else if (!strncmp(sentence + 1, "GL", 2)) sys = 'R';
  else if (!strncmp(sentence + 1, "GB", 2) || !strncmp(sentence + 1, "BD", 2)) sys = 'B';
  else if (!strncmp(sentence + 1, "GA", 2)) sys = 'E';
  else sys = '?';

  // Split into fields, keeping empty ones, stopping at the checksum.
  const char* fields[24];
  size_t lens[24], nf = 0;
  const char* p = sentence;
  while (nf < 24) {
    const char* end = p;
    while (*end && *end != ',' && *end != '*') end++;
    fields[nf] = p;
    lens[nf++] = end - p;
    if (*end != ',') break;
    p = end + 1;
  }
  size_t n = 0;
  for (size_t f = 4; f + 3 < nf && n < max; f += 4) {  // groups of prn, elevation, azimuth, snr
    if (!lens[f]) continue;
    out[n].prn = (uint8_t)atoi(fields[f]);
    out[n].sys = sys;
    out[n].snr = lens[f + 3] ? (uint8_t)atoi(fields[f + 3]) : 0;
    n++;
  }
  return n;
}

void SkyGuard::onSats(const SatInfo* s, size_t n, uint32_t now) {
  uint8_t best[4] = {0, 0, 0, 0};
  for (size_t i = 0; i < n; i++) {
    uint8_t v = s[i].snr;
    for (int j = 0; j < 4; j++)
      if (v > best[j]) { uint8_t t = best[j]; best[j] = v; v = t; }
  }
  float mean = (best[0] + best[1] + best[2] + best[3]) / 4.0f;
  lastSnr_ = mean;
  lastSatsMs_ = now;
  if (noFix_ || mean < kStrongSnr) return;
  // Remember how strong the sky looked while the fix was good.
  if (!hadStrong_ || mean > strongSnr_ || now - strongMs_ > kStrongWindowMs) strongSnr_ = mean;
  if (mean >= strongSnr_ - 3) strongMs_ = now;
  hadStrong_ = true;
}

void SkyGuard::tick(uint32_t now, bool hasFix) {
  if (hasFix) {
    noFix_ = false;
    return;
  }
  if (!noFix_) {
    noFix_ = true;
    noFixSinceMs_ = now;
  }
  bool jammed = now - noFixSinceMs_ >= kNoFixMs && hadStrong_ && now - strongMs_ < kStrongWindowMs &&
                now - lastSatsMs_ <= kSatsFreshMs && lastSnr_ > 0 && lastSnr_ <= strongSnr_ - kJamDropDb;
  if (jammed && (!jamEver_ || now - lastJamMs_ >= kRepeatMs)) {
    jamEver_ = true;
    lastJamMs_ = now;
    pending_ = "JAMMING?";
  }
}

void SkyGuard::onFix(const Fix& f, uint32_t now) {
  if (!f.valid) return;
  if (havePrev_) {
    double dt = (now - prevMs_) / 1000.0;
    bool spoof = false;
    if (dt > 0 && dt <= kCheckGapS) {
      double d = haversineM(prev_.lat, prev_.lon, f.lat, f.lon);
      spoof = d > kJumpM && d / dt > kMaxSpeedMps;
    }
    if (f.unixTime && prev_.unixTime) {
      double clock = (double)(int32_t)(f.unixTime - prev_.unixTime);
      // A clock off by a whole day is the receiver reporting the new time before the new date
      // (GGA arrives before RMC at midnight UTC), not spoofing.
      double err = clock - dt;
      bool dayLag = err > -86400 - kClockSlackS && err < -86400 + kClockSlackS;
      spoof |= !dayLag && (err > kClockSlackS || -err > kClockSlackS);
    }
    if (spoof && (!spoofEver_ || now - lastSpoofMs_ >= kRepeatMs)) {
      spoofEver_ = true;
      lastSpoofMs_ = now;
      pending_ = "SPOOFING?";
    }
  }
  prev_ = f;
  prevMs_ = now;
  havePrev_ = true;
}

bool SkyGuard::poll(uint32_t now, const char** text) {
  (void)now;
  if (!pending_) return false;
  *text = pending_;
  pending_ = nullptr;
  return true;
}
