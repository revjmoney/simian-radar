#pragma once
#include "Fix.h"

bool gpsBegin();
bool gpsPoll(Fix* out);  // feeds NMEA; true once a fix with age < 2 s is available
