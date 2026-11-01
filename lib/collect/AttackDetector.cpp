#include "AttackDetector.h"
#include <string.h>

static const uint32_t kReportEveryMs = 2UL * 60UL * 1000UL;
static const uint32_t kKarmaWindowMs = 60000;
static const uint32_t kSpamWindowMs = 10000;
static const uint32_t kSpamSettleMs = 3000;  // a real device repeats its address within this
static const uint32_t kTwinWindowMs = 3UL * 60UL * 1000UL;
static const uint8_t kPwnagotchi[6] = {0xde, 0xad, 0xbe, 0xef, 0xde, 0xad};

const char* attackName(AttackKind k) {
  static const char* names[] = {"DEAUTH FLOOD", "EVIL TWIN", "KARMA AP", "PWNAGOTCHI", "FLIPPER", "BLE SPAM"};
  return k < AttackKind::Count ? names[(int)k] : "";
}

static uint32_t fnv(const uint8_t* p, size_t n) {
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
  return h;
}
static uint32_t fnvStr(const char* s) { return fnv((const uint8_t*)s, strlen(s)); }

void AttackDetector::raise(AttackKind k, const uint8_t mac[6], const char* ssid, uint32_t now) {
  AttackEvent e = {};
  e.kind = k;
  if (mac) memcpy(e.mac, mac, 6);
  if (ssid) strncpy(e.ssid, ssid, sizeof e.ssid - 1);
  e.ms = now;
  if (nPending_ < sizeof pending_ / sizeof pending_[0]) pending_[nPending_++] = e;
  // Recent list, newest first; a repeat of the newest entry just refreshes its time.
  if (nRecent_ && recent_[0].kind == k && memcmp(recent_[0].mac, e.mac, 6) == 0) {
    recent_[0].ms = now;
    return;
  }
  size_t cap = sizeof recent_ / sizeof recent_[0];
  size_t last = nRecent_ < cap ? nRecent_ : cap - 1;
  for (size_t i = last; i > 0; i--) recent_[i] = recent_[i - 1];
  recent_[0] = e;
  if (nRecent_ < cap) nRecent_++;
}

void AttackDetector::onWifi(const WifiFrame& f, uint32_t now) {
  if (f.type != 0 || !f.hasA2) return;

  if (f.subtype == kWifiDeauth || f.subtype == kWifiDisassoc) {
    const uint8_t* bssid = f.hasA3 ? f.a3 : f.a2;
    Deauth* d = nullptr;
    Deauth* oldest = &deauth_[0];
    for (auto& x : deauth_) {
      if (x.used && memcmp(x.mac, bssid, 6) == 0) { d = &x; break; }
      if (!x.used) oldest = &x;
      else if (oldest->used && (int32_t)(x.windowMs - oldest->windowMs) < 0) oldest = &x;
    }
    if (!d) {
      d = oldest;
      *d = Deauth{true, {0}, now, 0};
      memcpy(d->mac, bssid, 6);
    }
    if (now - d->windowMs >= 1000) {
      d->windowMs = now;
      d->count = 0;
    }
    if (++d->count > kDeauthPerSec) {
      raise(AttackKind::DeauthFlood, bssid, nullptr, now);
      d->count = 0;
    }
    return;
  }

  if (f.subtype == kWifiBeacon && memcmp(f.a2, kPwnagotchi, 6) == 0) {
    raise(AttackKind::Pwnagotchi, f.a2, nullptr, now);
    return;
  }

  if ((f.subtype == kWifiBeacon || f.subtype == kWifiProbeResp) && f.hasSsid && f.ssid[0]) {
    // Evil twin: the same network name from two different makers, one open and one secured.
    uint32_t h = fnvStr(f.ssid);
    // Both sides must be on the air within a few minutes of each other: a generic name like
    // "linksys" heard open here and secured miles later is two unrelated networks.
    Twin* t = nullptr;
    Twin* oldest = &twins_[0];
    for (auto& x : twins_) {
      if (x.used && x.ssidHash == h) { t = &x; break; }
      if (!x.used) { if (oldest->used) oldest = &x; }
      else if (oldest->used && (int32_t)(x.lastMs - oldest->lastMs) < 0) oldest = &x;
    }
    if (!t) {
      t = oldest;
      *t = Twin{};
      t->used = true;
      t->ssidHash = h;
      strncpy(t->ssid, f.ssid, sizeof t->ssid - 1);
    }
    t->lastMs = now;
    int idx = -1;
    for (int i = 0; i < t->n; i++)
      if (memcmp(t->oui[i], f.a2, 3) == 0) idx = i;
    if (idx >= 0) {
      t->ouiMs[idx] = now;
    } else {
      if (t->n < 4) {
        idx = t->n++;
      } else {
        idx = 0;
        for (int i = 1; i < 4; i++)
          if ((int32_t)(t->ouiMs[i] - t->ouiMs[idx]) < 0) idx = i;
      }
      memcpy(t->oui[idx], f.a2, 3);
      t->open[idx] = f.open;
      t->ouiMs[idx] = now;
      for (int i = 0; i < t->n; i++)
        if (i != idx && t->open[i] != t->open[idx] && now - t->ouiMs[i] <= kTwinWindowMs) {
          raise(AttackKind::EvilTwin, f.a2, t->ssid, now);
          break;
        }
    }
  }

  if (f.subtype == kWifiProbeResp && f.hasSsid && f.ssid[0]) {
    // Karma / Pineapple: one access point claiming to be every network anyone asks for.
    Karma* k = nullptr;
    Karma* oldest = &karma_[0];
    for (auto& x : karma_) {
      if (x.used && memcmp(x.mac, f.a2, 6) == 0) { k = &x; break; }
      if (!x.used) oldest = &x;
      else if (oldest->used && (int32_t)(x.windowMs - oldest->windowMs) < 0) oldest = &x;
    }
    if (!k) {
      k = oldest;
      *k = Karma{};
      k->used = true;
      memcpy(k->mac, f.a2, 6);
      k->windowMs = now;
    }
    if (now - k->windowMs > kKarmaWindowMs) {
      k->windowMs = now;
      k->n = 0;
    }
    uint32_t h = fnvStr(f.ssid);
    bool seen = false;
    for (int i = 0; i < k->n; i++) seen |= k->ssids[i] == h;
    if (!seen && k->n < kKarmaSsids) k->ssids[k->n++] = h;
    if (k->n >= kKarmaSsids) {
      raise(AttackKind::Karma, f.a2, f.ssid, now);
      k->n = 0;
    }
  }
}

static bool isPopupAdvert(const BleAdv& a) {
  if (a.mfg && a.mfgLen >= 3) {
    uint16_t company = (uint16_t)(a.mfg[0] | (a.mfg[1] << 8));
    if (company == 0x004C && (a.mfg[2] == 0x07 || a.mfg[2] == 0x0F)) return true;  // Apple pairing popups
    if (company == 0x0006 && a.mfg[2] == 0x03) return true;                         // Microsoft Swift Pair
  }
  if (a.sdUuid == 0xFE2C) return true;  // Google Fast Pair
  for (size_t i = 0; i < a.svc16Count; i++)
    if (a.svc16[i] == 0xFE2C) return true;
  return false;
}

void AttackDetector::onBle(const BleAdv& a, uint32_t now) {
  if (a.name && strncmp(a.name, "Flipper ", 8) == 0) raise(AttackKind::Flipper, a.addr, a.name, now);
  if (!isPopupAdvert(a)) return;
  // Popup spam tools use a fresh address for nearly every advert; real earbuds keep repeating
  // theirs. Remember how often each address has been heard.
  uint32_t h = fnv(a.addr, 6);
  Spam* oldest = &spam_[0];
  for (auto& s : spam_) {
    if (s.count && s.addrHash == h) {
      if (s.count < 0xFFFF) s.count++;
      return;
    }
    if (!s.count) { if (oldest->count) oldest = &s; }
    else if (oldest->count && (int32_t)(s.firstMs - oldest->firstMs) < 0) oldest = &s;
  }
  *oldest = {h, now, 1};
}

void AttackDetector::checkSpam(uint32_t now) {
  size_t once = 0;
  for (const auto& s : spam_)
    once += s.count == 1 && now - s.firstMs >= kSpamSettleMs && now - s.firstMs <= kSpamWindowMs;
  if (once >= kSpamAddresses) {
    raise(AttackKind::BleSpam, nullptr, nullptr, now);
    memset(spam_, 0, sizeof spam_);
  }
}

bool AttackDetector::poll(uint32_t now, AttackEvent* out) {
  checkSpam(now);
  while (nPending_) {
    AttackEvent e = pending_[0];
    memmove(pending_, pending_ + 1, --nPending_ * sizeof pending_[0]);
    int k = (int)e.kind;
    if (reportedEver_[k] && now - lastReported_[k] < kReportEveryMs) continue;
    reportedEver_[k] = true;
    lastReported_[k] = now;
    *out = e;
    return true;
  }
  return false;
}

size_t AttackDetector::recent(AttackEvent* out, size_t max) const {
  size_t n = nRecent_ < max ? nRecent_ : max;
  memcpy(out, recent_, n * sizeof recent_[0]);
  return n;
}
