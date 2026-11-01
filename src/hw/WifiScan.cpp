#include "WifiScan.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <string.h>
#include "Makers.h"
#include "Signatures.h"

static const size_t kMaxAps = 48;
static const uint32_t kExpireMs = 30000;
static const uint32_t kHopMs = 250;
static const size_t kMgmtCopy = 320;   // enough for the SSID and a Remote ID vendor element
static const size_t kHeaderCopy = 24;  // data/control: addresses only

struct RawFrame { uint16_t len; int8_t rssi; uint8_t channel; uint8_t data[kMgmtCopy]; };
struct ApSlot { ApInfo info; uint32_t lastMs; bool used; };

static QueueHandle_t queue;
static ApSlot aps[kMaxAps];
static volatile uint32_t frameCount[12];
static uint32_t flockHeardMs = 0;
static bool flockEver = false;
static uint8_t channel = 1;
static uint32_t lastHopMs = 0;

// Runs in the WiFi task: keep it short. Counts everything; queues management frames, and only the
// data/control frames from camera or Axon makers (the only ones the lists need).
static void onFrame(void* buf, wifi_promiscuous_pkt_type_t type) {
  const wifi_promiscuous_pkt_t* pkt = (const wifi_promiscuous_pkt_t*)buf;
  uint8_t ch = pkt->rx_ctrl.channel;
  if (ch < 12) frameCount[ch]++;
  size_t len = pkt->rx_ctrl.sig_len;
  if (len < 10) return;
  size_t copy;
  if (type == WIFI_PKT_MGMT) {
    copy = len < kMgmtCopy ? len : kMgmtCopy;
  } else {
    if (len < 16) return;
    Maker m = makerOf(pkt->payload + 10);
    if (!isCameraMaker(m) && m != Maker::Axon) return;
    copy = len < kHeaderCopy ? len : kHeaderCopy;
  }
  RawFrame r;
  r.len = (uint16_t)copy;
  r.rssi = pkt->rx_ctrl.rssi;
  r.channel = ch;
  memcpy(r.data, pkt->payload, copy);
  xQueueSend(queue, &r, 0);  // dropped when loop() is behind
}

void wifiScanBegin() {
  queue = xQueueCreate(24, sizeof(RawFrame));
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  wifi_promiscuous_filter_t filt = {WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA |
                                    WIFI_PROMIS_FILTER_MASK_CTRL};
  esp_wifi_set_promiscuous_filter(&filt);
  esp_wifi_set_promiscuous_rx_cb(onFrame);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
}

void wifiScanHop(uint32_t nowMs, WifiDwellFn onDwell) {
  if (nowMs - lastHopMs < kHopMs) return;
  uint32_t dwell = nowMs - lastHopMs;
  lastHopMs = nowMs;
  uint32_t frames = frameCount[channel];
  frameCount[channel] = 0;
  if (onDwell) onDwell(channel, dwell, frames);
  channel = channel % 11 + 1;
  esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
}

static void rememberAp(const WifiFrame& f, int8_t rssi, uint8_t ch, bool flock, uint32_t now) {
  const uint8_t* mac = f.subtype == kWifiProbeReq ? f.a2 : f.a3;
  ApSlot* slot = nullptr;
  ApSlot* oldest = &aps[0];
  for (auto& a : aps) {
    if (a.used && memcmp(a.info.bssid, mac, 6) == 0) { slot = &a; break; }
    if (!a.used) oldest = &a;
    else if (oldest->used && (int32_t)(a.lastMs - oldest->lastMs) < 0) oldest = &a;
  }
  if (!slot) {
    slot = oldest;
    memset(slot, 0, sizeof *slot);  // don't inherit the previous occupant's Flock flag
    slot->used = true;
    memcpy(slot->info.bssid, mac, 6);
  }
  if (f.hasSsid && f.ssid[0]) strncpy(slot->info.ssid, f.ssid, 32);
  else if (!slot->info.ssid[0]) strcpy(slot->info.ssid, "(hidden)");
  slot->info.rssi = rssi;
  slot->info.channel = ch;
  slot->info.flock = slot->info.flock || flock;
  slot->lastMs = now;
}

void wifiPoll(uint32_t nowMs, WifiFrameFn onFrameFn) {
  RawFrame r;
  while (xQueueReceive(queue, &r, 0) == pdTRUE) {
    WifiFrame f;
    if (!parseWifiFrame(r.data, r.len, &f)) continue;
    if (f.type == 0 && f.hasA2) {
      bool flock = isFlockWifi(f.hasSsid ? f.ssid : "", f.a2);
      if (flock) {
        flockHeardMs = nowMs;
        flockEver = true;
      }
      bool isAp = f.subtype == kWifiBeacon || f.subtype == kWifiProbeResp;
      if (isAp || flock) rememberAp(f, r.rssi, r.channel, flock, nowMs);
    }
    if (onFrameFn) onFrameFn(f, r.rssi, r.channel, nowMs);
  }
}

size_t wifiScanResults(ApInfo* out, size_t max, uint32_t nowMs) {
  static ApInfo all[kMaxAps];
  size_t n = 0;
  for (auto& a : aps) {
    if (a.used && nowMs - a.lastMs > kExpireMs) a.used = false;
    if (a.used) all[n++] = a.info;
  }
  // Flock first, then strongest signal.
  for (size_t i = 1; i < n; i++)
    for (size_t j = i; j > 0; j--) {
      const ApInfo &a = all[j - 1], &b = all[j];
      bool swap = (b.flock && !a.flock) || (b.flock == a.flock && b.rssi > a.rssi);
      if (!swap) break;
      ApInfo t = all[j - 1]; all[j - 1] = all[j]; all[j] = t;
    }
  if (n > max) n = max;
  memcpy(out, all, n * sizeof(ApInfo));
  return n;
}

bool wifiFlockHeardSince(uint32_t sinceMs) { return flockEver && (int32_t)(flockHeardMs - sinceMs) > 0; }
