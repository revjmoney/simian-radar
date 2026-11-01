#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Fixed-size table of things heard recently, keyed by 6 bytes. A full table evicts the least
// recently seen entry, so busy places cannot grow memory.
struct Seen {
  bool used;
  uint8_t key[6];
  uint8_t maker;     // Maker or BleKind, caller's choice
  int8_t rssi;
  uint32_t firstMs, lastMs;
  char text[24];
};

template <size_t N>
class SeenTable {
 public:
  Seen* upsert(const uint8_t key[6], int8_t rssi, uint32_t now) {
    Seen* oldest = &rows_[0];
    for (auto& r : rows_) {
      if (r.used && memcmp(r.key, key, 6) == 0) {
        r.rssi = rssi;
        r.lastMs = now;
        return &r;
      }
      if (!r.used) { if (oldest->used) oldest = &r; }
      else if (oldest->used && (int32_t)(r.lastMs - oldest->lastMs) < 0) oldest = &r;
    }
    *oldest = Seen{};
    oldest->used = true;
    memcpy(oldest->key, key, 6);
    oldest->rssi = rssi;
    oldest->firstMs = oldest->lastMs = now;
    return oldest;
  }
  // Rows heard within expireMs, strongest first.
  size_t list(Seen* out, size_t max, uint32_t now, uint32_t expireMs) {
    size_t n = 0;
    for (auto& r : rows_) {
      if (r.used && now - r.lastMs > expireMs) r.used = false;
      if (!r.used || max == 0) continue;
      size_t pos = n;
      while (pos > 0 && out[pos - 1].rssi < r.rssi) pos--;
      if (pos >= max) continue;
      size_t last = n < max ? n : max - 1;
      for (size_t k = last; k > pos; k--) out[k] = out[k - 1];
      out[pos] = r;
      if (n < max) n++;
    }
    return n;
  }
  size_t count(uint32_t now, uint32_t expireMs) const {
    size_t n = 0;
    for (const auto& r : rows_) n += r.used && now - r.lastMs <= expireMs;
    return n;
  }
  void clear() { memset(rows_, 0, sizeof rows_); }
  size_t size() const {
    size_t n = 0;
    for (const auto& r : rows_) n += r.used;
    return n;
  }

 private:
  Seen rows_[N] = {};
};
