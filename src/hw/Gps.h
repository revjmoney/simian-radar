#pragma once
#include "Fix.h"

bool gpsBegin();
bool gpsPoll(Fix* out);
uint32_t gpsChars();  // NMEA characters received so far: proves the module is talking  // feeds NMEA; true once a fix with age < 2 s is available
