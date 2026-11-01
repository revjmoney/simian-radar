#pragma once
#include <stddef.h>
#include <stdint.h>
#include "Fix.h"

struct SatInfo { uint8_t prn; char sys; uint8_t snr; };  // sys: G GPS, R GLONASS, B BeiDou, E Galileo

// Parses one $xxGSV sentence (up to 4 satellites). Returns how many satellites it filled in.
size_t parseGsv(const char* sentence, SatInfo* out, size_t max);

// Warns about GPS jamming (strong satellites all fade and the fix drops) and spoofing (the position
// or the clock jumps further than a real vehicle could).
class SkyGuard {
 public:
  void onSats(const SatInfo* s, size_t n, uint32_t now);
  void onFix(const Fix& f, uint32_t now);
  void tick(uint32_t now, bool hasFix);
  bool poll(uint32_t now, const char** text);  // once per kind per 2 minutes

 private:
  float strongSnr_ = 0;        // mean of the 4 best satellites while the fix was good
  uint32_t strongMs_ = 0;
  bool hadStrong_ = false;
  float lastSnr_ = 0;
  uint32_t lastSatsMs_ = 0;
  uint32_t noFixSinceMs_ = 0;
  bool noFix_ = false;
  bool havePrev_ = false;
  Fix prev_ = {};
  uint32_t prevMs_ = 0;
  const char* pending_ = nullptr;
  uint32_t lastJamMs_ = 0, lastSpoofMs_ = 0;
  bool jamEver_ = false, spoofEver_ = false;
};
