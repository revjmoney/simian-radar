#pragma once
#include <stddef.h>
#include <stdint.h>
#include "Config.h"
#include "Signatures.h"

struct FollowAlert { uint8_t key[8]; BleKind kind; int8_t rssi; };

// Flags a tracker you don't own that stays with you over time AND distance.
class FollowDetector {
 public:
  void observe(const uint8_t key[8], BleKind k, int8_t rssi, uint32_t nowMs, bool hasFix,
               double lat, double lon);
  bool poll(uint32_t nowMs, FollowAlert* out);
  void ignore(const uint8_t key[8]);
  size_t size() const;

 private:
  struct Entry {
    bool used;
    uint8_t key[8];
    BleKind kind;
    int8_t rssi;
    uint32_t firstMs, lastMs;
    bool hasStart;
    double startLat, startLon;
    float maxTravelM;
    bool alerted;
  };
  static const size_t kMaxIgnored = 16;
  Entry entries_[kMaxBleTracked] = {};
  uint8_t ignored_[kMaxIgnored][8] = {};
  size_t ignoredCount_ = 0;
  bool isIgnored(const uint8_t key[8]) const;
};
