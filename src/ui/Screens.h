#pragma once
#include "AppLogic.h"
#include "Canvas.h"
#include "SeenTable.h"
#include "Sky.h"
#include "SubGhz.h"
#include "../hw/BleScan.h"
#include "../hw/WifiScan.h"

// Everything the screens need for one frame. main() fills in the parts for the mode on screen.
struct ScreenData {
  const ApInfo* aps = nullptr;
  size_t nAps = 0;
  const BleSeen* ble = nullptr;
  size_t nBle = 0;
  const Seen* rows = nullptr;  // PHONE SPYCAM ATTACK TAGS BODYCAM DRONE MESH LORAWAN ZWAVE WX
  size_t nRows = 0;
  bool rowBars = true;         // draw a signal bar under each row
  bool rowFlagAll = false;     // invert every row (attack list)
  const float* chan = nullptr; // 11 channel rates
  const SweepStats* sweep = nullptr;
  bool rfid = false;
  const SatInfo* sats = nullptr;
  size_t nSats = 0;
  float hdop = 0, altM = 0;
  const char* footer = "";
  uint8_t satCount = 0;
  float vbat = 0;
  uint32_t camCount = 0;
  size_t markCount = 0;
  bool showBanner = false;  // big mode name for a moment after a tap
};

// Draws one full frame for the 64x128 portrait OLED. Caller clears and sends the buffer.
void drawFrame(Canvas& d, const AppLogic& app, const ScreenData& s);
