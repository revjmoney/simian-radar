#pragma once
#include <stddef.h>
#include <stdint.h>
#include "WifiFrame.h"

struct ApInfo { char ssid[33]; uint8_t bssid[6]; int8_t rssi; uint8_t channel; bool flock; };

// Listen-only WiFi: promiscuous mode, hopping channels 1-11. The radio callback only counts frames
// and copies headers (management frames with their elements, data/control frames' first 24 bytes)
// into a queue; everything is parsed in loop() by wifiPoll(). Never associates with anything and
// never transmits.
typedef void (*WifiFrameFn)(const WifiFrame& f, int8_t rssi, uint8_t channel, uint32_t now);
typedef void (*WifiDwellFn)(uint8_t channel, uint32_t ms, uint32_t frames);

void wifiScanBegin();
void wifiScanHop(uint32_t nowMs, WifiDwellFn onDwell);  // reports the channel it just left
void wifiPoll(uint32_t nowMs, WifiFrameFn onFrame);
size_t wifiScanResults(ApInfo* out, size_t max, uint32_t nowMs);
bool wifiFlockHeardSince(uint32_t sinceMs);  // a Flock signature was heard after sinceMs
