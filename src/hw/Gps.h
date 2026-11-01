#pragma once
#include <stddef.h>
#include "Fix.h"
#include "Sky.h"

bool gpsBegin();
bool gpsPoll(Fix* out);
uint32_t gpsChars();  // NMEA characters received so far: proves the module is talking  // feeds NMEA; true once a fix with age < 2 s is available
size_t gpsSats(SatInfo* out, size_t max, uint32_t now);  // satellites heard in the last 5 s
float gpsHdop();
float gpsAltM();
