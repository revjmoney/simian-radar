#pragma once
#include <U8g2lib.h>
#include "AppLogic.h"
#include "../hw/BleScan.h"
#include "../hw/WifiScan.h"

// Draws one full frame for the 64x128 portrait OLED. Caller does clearBuffer/sendBuffer.
void drawFrame(U8G2& d, const AppLogic& app, const ApInfo* aps, size_t nAps, const BleSeen* ble,
               size_t nBle, uint8_t sats, float vbat, uint32_t camCount, size_t markCount);
