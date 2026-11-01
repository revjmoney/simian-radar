#pragma once
#include "MarkIO.h"

// /marks.bin on LittleFS, written to a temp file then renamed so a power cut can't half-write it.
struct LittleFsMarkIO : MarkIO {
  bool load(uint8_t* buf, size_t cap, size_t* len) override;
  bool save(const uint8_t* buf, size_t len) override;
};
