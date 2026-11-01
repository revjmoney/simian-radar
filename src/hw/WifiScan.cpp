#include "WifiScan.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <string.h>
#include "Signatures.h"

static const size_t kMaxAps = 48;
static const uint32_t kExpireMs = 30000;
static const uint32_t kHopMs = 250;

struct ApSlot { ApInfo info; uint32_t lastMs; bool used; };
static ApSlot aps[kMaxAps];
static volatile uint32_t flockHeardMs = 0;
static volatile bool flockEver = false;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static uint8_t channel = 1;
static uint32_t lastHopMs = 0;

static void record(const uint8_t* mac, const char* ssid, int8_t rssi, uint8_t ch, bool flock, bool isAp) {
  uint32_t now = millis();
  portENTER_CRITICAL_ISR(&lock);
  if (flock) {
    flockHeardMs = now;
    flockEver = true;
  }
  if (isAp || flock) {
    ApSlot* slot = nullptr;
    ApSlot* oldest = &aps[0];
    for (auto& a : aps) {
      if (a.used && memcmp(a.info.bssid, mac, 6) == 0) { slot = &a; break; }
      if (!a.used) oldest = &a;
      else if (oldest->used && a.lastMs < oldest->lastMs) oldest = &a;
    }
    if (!slot) {
      slot = oldest;
      memset(slot, 0, sizeof *slot);  // don't inherit the previous occupant's Flock flag
      slot->used = true;
      memcpy(slot->info.bssid, mac, 6);
    }
    strncpy(slot->info.ssid, ssid, 32);
    slot->info.ssid[32] = 0;
    slot->info.rssi = rssi;
    slot->info.channel = ch;
    slot->info.flock = slot->info.flock || flock;
    slot->lastMs = now;
  }
  portEXIT_CRITICAL_ISR(&lock);
}

static void IRAM_ATTR onFrame(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (type != WIFI_PKT_MGMT) return;
  const wifi_promiscuous_pkt_t* pkt = (const wifi_promiscuous_pkt_t*)buf;
  const uint8_t* f = pkt->payload;
  size_t len = pkt->rx_ctrl.sig_len;
  if (len < 24) return;
  uint8_t subtype = f[0] & 0xF0;
  size_t ieOff;
  if (subtype == 0x80 || subtype == 0x50) ieOff = 36;  // beacon, probe response
  else if (subtype == 0x40) ieOff = 24;                 // probe request
  else return;
  const uint8_t* addr2 = f + 10;
  char ssid[33] = "";
  if (len > ieOff + 2 && f[ieOff] == 0) {  // element 0 = SSID
    size_t n = f[ieOff + 1];
    if (n > 32) n = 32;
    if (ieOff + 2 + n <= len) {
      memcpy(ssid, f + ieOff + 2, n);
      ssid[n] = 0;
    }
  }
  bool flock = isFlockWifi(ssid, addr2);
  record(subtype == 0x40 ? addr2 : f + 16, ssid[0] || subtype != 0x80 ? ssid : "(hidden)",
         pkt->rx_ctrl.rssi, pkt->rx_ctrl.channel, flock, subtype != 0x40);
}

void wifiScanBegin() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  wifi_promiscuous_filter_t filt = {WIFI_PROMIS_FILTER_MASK_MGMT};
  esp_wifi_set_promiscuous_filter(&filt);
  esp_wifi_set_promiscuous_rx_cb(onFrame);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
}

void wifiScanHop(uint32_t nowMs) {
  if (nowMs - lastHopMs < kHopMs) return;
  lastHopMs = nowMs;
  channel = channel % 11 + 1;
  esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
}

size_t wifiScanResults(ApInfo* out, size_t max, uint32_t nowMs) {
  static ApInfo all[kMaxAps];
  size_t n = 0;
  portENTER_CRITICAL(&lock);
  for (auto& a : aps) {
    if (a.used && nowMs - a.lastMs > kExpireMs) a.used = false;
    if (a.used) all[n++] = a.info;
  }
  portEXIT_CRITICAL(&lock);
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

bool wifiFlockHeardSince(uint32_t sinceMs) {
  portENTER_CRITICAL(&lock);
  bool heard = flockEver && (int32_t)(flockHeardMs - sinceMs) > 0;
  portEXIT_CRITICAL(&lock);
  return heard;
}
