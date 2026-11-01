#pragma once
#include <stdint.h>

struct Fix {
  bool valid;
  double lat, lon;
  float speedMps;
  float courseDeg;
  uint8_t sats;
  uint32_t unixTime;
};
