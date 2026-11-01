#include "BleScan.h"
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <string.h>

static const size_t kMaxSeen = 64;
static const uint32_t kExpireMs = 30000;

struct RawAdv {
  uint8_t addr[6];
  int8_t rssi;
  uint8_t mfgLen;
  uint8_t mfg[31];
  uint8_t nSvc;
  uint16_t svc16[4];
  char name[20];
};

struct SeenSlot { BleSeen s; uint32_t lastMs; bool used; };
static SeenSlot seen[kMaxSeen];
static QueueHandle_t queue;

class Callbacks : public NimBLEAdvertisedDeviceCallbacks {
  void onResult(NimBLEAdvertisedDevice* d) override {
    RawAdv r = {};
    memcpy(r.addr, d->getAddress().getNative(), 6);
    r.rssi = (int8_t)d->getRSSI();
    std::string m = d->getManufacturerData();
    r.mfgLen = (uint8_t)(m.size() < sizeof r.mfg ? m.size() : sizeof r.mfg);
    memcpy(r.mfg, m.data(), r.mfgLen);
    for (int i = 0; i < (int)d->getServiceUUIDCount() && r.nSvc < 4; i++) {
      NimBLEUUID u = d->getServiceUUID(i);
      if (u.bitSize() == 16) r.svc16[r.nSvc++] = u.getNative()->u16.value;
    }
    std::string n = d->getName();
    strncpy(r.name, n.c_str(), sizeof r.name - 1);
    xQueueSend(queue, &r, 0);  // drop when loop() is behind
  }
};
static Callbacks callbacks;

void bleScanBegin() {
  queue = xQueueCreate(32, sizeof(RawAdv));
  NimBLEDevice::init("");
  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setAdvertisedDeviceCallbacks(&callbacks, true);
  scan->setActiveScan(false);  // listen only, no scan requests
  scan->setInterval(100);
  scan->setWindow(60);
  scan->setMaxResults(0);  // don't keep a results list; the callback is enough
  scan->start(0, nullptr, false);
}

static void remember(const BleAdv& a, BleKind k, const uint8_t key[8], uint32_t nowMs) {
  SeenSlot* slot = nullptr;
  SeenSlot* oldest = &seen[0];
  for (auto& x : seen) {
    if (x.used && memcmp(x.s.key, key, 8) == 0) { slot = &x; break; }
    if (!x.used) oldest = &x;
    else if (oldest->used && x.lastMs < oldest->lastMs) oldest = &x;
  }
  if (!slot) {
    slot = oldest;
    slot->used = true;
    memcpy(slot->s.key, key, 8);
  }
  const char* label = bleLabel(k);
  if (!*label) label = a.name && *a.name ? a.name : "(unnamed)";
  strncpy(slot->s.label, label, sizeof slot->s.label - 1);
  slot->s.label[sizeof slot->s.label - 1] = 0;
  slot->s.rssi = a.rssi;
  slot->s.kind = k;
  slot->lastMs = nowMs;
}

void blePoll(void (*cb)(const BleAdv& adv, BleKind kind, const uint8_t key[8]), uint32_t nowMs) {
  RawAdv r;
  while (xQueueReceive(queue, &r, 0) == pdTRUE) {
    BleAdv a = {{0}, r.rssi, r.mfg, r.mfgLen, r.svc16, r.nSvc, r.name};
    memcpy(a.addr, r.addr, 6);
    BleKind k = classifyBle(a);
    uint8_t key[8];
    trackerKey(a, k, key);
    remember(a, k, key, nowMs);
    if (cb) cb(a, k, key);
  }
}

size_t bleList(BleSeen* out, size_t max, uint32_t nowMs) {
  static BleSeen all[kMaxSeen];
  size_t n = 0;
  for (auto& x : seen) {
    if (x.used && nowMs - x.lastMs > kExpireMs) x.used = false;
    if (x.used) all[n++] = x.s;
  }
  auto flagged = [](BleKind k) { return k != BleKind::Other && k != BleKind::FindMyNearOwner; };
  for (size_t i = 1; i < n; i++)
    for (size_t j = i; j > 0; j--) {
      bool fa = flagged(all[j - 1].kind), fb = flagged(all[j].kind);
      if (!((fb && !fa) || (fb == fa && all[j].rssi > all[j - 1].rssi))) break;
      BleSeen t = all[j - 1]; all[j - 1] = all[j]; all[j] = t;
    }
  if (n > max) n = max;
  memcpy(out, all, n * sizeof(BleSeen));
  return n;
}
