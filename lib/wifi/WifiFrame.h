#pragma once
#include <stddef.h>
#include <stdint.h>

// Header-level view of an 802.11 frame. Only addresses, the SSID element and the Remote ID vendor
// element are read; frame bodies are never looked at.
struct WifiFrame {
  uint8_t type;     // 0 management, 1 control, 2 data
  uint8_t subtype;  // 0-15 (beacon 8, probe request 4, probe response 5, disassoc 10, deauth 12)
  uint8_t a1[6], a2[6], a3[6];
  bool hasA2, hasA3;
  char ssid[33];
  bool hasSsid;
  bool open;               // beacon/probe response without the privacy bit
  const uint8_t* ridIe;    // Remote ID payload after the vendor IE type byte (counter + pack)
  size_t ridLen;
};

enum : uint8_t { kWifiBeacon = 8, kWifiProbeReq = 4, kWifiProbeResp = 5, kWifiDisassoc = 10, kWifiDeauth = 12 };

bool parseWifiFrame(const uint8_t* f, size_t len, WifiFrame* out);
