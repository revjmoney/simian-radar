#include "FollowDetector.h"
#include <string.h>
#include "Geo.h"

static const uint32_t kForgetMs = 5UL * 60UL * 1000UL;

static bool tracked(BleKind k) {
  return k == BleKind::FindMySeparated || k == BleKind::Tile || k == BleKind::SmartTag ||
         k == BleKind::GoogleFmdn;
}

bool FollowDetector::isIgnored(const uint8_t key[8]) const {
  for (size_t i = 0; i < ignoredCount_; i++)
    if (memcmp(ignored_[i], key, 8) == 0) return true;
  return false;
}

void FollowDetector::ignore(const uint8_t key[8]) {
  if (isIgnored(key)) return;
  memcpy(ignored_[ignoredCount_ % kMaxIgnored], key, 8);
  if (ignoredCount_ < kMaxIgnored) ignoredCount_++;
}

void FollowDetector::observe(const uint8_t key[8], BleKind k, int8_t rssi, uint32_t nowMs,
                             bool hasFix, double lat, double lon) {
  if (!tracked(k)) return;
  Entry* e = nullptr;
  Entry* oldest = &entries_[0];
  for (auto& x : entries_) {
    if (x.used && memcmp(x.key, key, 8) == 0) { e = &x; break; }
    if (!x.used) { if (oldest->used) oldest = &x; }
    else if (oldest->used && (int32_t)(x.lastMs - oldest->lastMs) < 0) oldest = &x;
  }
  if (!e) {
    e = oldest;  // a free slot, or the least recently seen one when the table is full
    *e = Entry{};
    e->used = true;
    memcpy(e->key, key, 8);
    e->firstMs = nowMs;
  }
  e->kind = k;
  e->rssi = rssi;
  e->lastMs = nowMs;
  if (!hasFix) return;
  if (!e->hasStart) {
    e->hasStart = true;
    e->startLat = lat;
    e->startLon = lon;
    return;
  }
  float d = (float)haversineM(e->startLat, e->startLon, lat, lon);
  if (d > e->maxTravelM) e->maxTravelM = d;
}

bool FollowDetector::poll(uint32_t nowMs, FollowAlert* out) {
  for (auto& e : entries_)
    if (e.used && nowMs - e.lastMs > kForgetMs) e.used = false;
  for (auto& e : entries_) {
    if (!e.used || e.alerted || isIgnored(e.key)) continue;
    if (nowMs - e.firstMs < kFollowMinMs || e.maxTravelM < kFollowMinTravelM) continue;
    if (nowMs - e.lastMs > kFollowRecentMs) continue;
    e.alerted = true;
    memcpy(out->key, e.key, 8);
    out->kind = e.kind;
    out->rssi = e.rssi;
    return true;
  }
  return false;
}

size_t FollowDetector::size() const {
  size_t n = 0;
  for (const auto& e : entries_) n += e.used;
  return n;
}
