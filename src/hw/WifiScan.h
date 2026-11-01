#pragma once
#include <stddef.h>
#include <stdint.h>

struct ApInfo { char ssid[33]; uint8_t bssid[6]; int8_t rssi; uint8_t channel; bool flock; };

// Listen-only WiFi: promiscuous mode on management frames, hopping channels 1-11. Reads only the
// frame header addresses and the SSID element. Never associates with anything.
void wifiScanBegin();
void wifiScanHop(uint32_t nowMs);
size_t wifiScanResults(ApInfo* out, size_t max, uint32_t nowMs);
bool wifiFlockHeardSince(uint32_t sinceMs);  // a Flock signature was heard after sinceMs
