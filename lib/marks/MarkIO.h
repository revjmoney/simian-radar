#pragma once
#include <stddef.h>
#include <stdint.h>

// Storage backend for marks: LittleFS on the device, memory in tests.
struct MarkIO {
  virtual bool load(uint8_t* buf, size_t cap, size_t* len) = 0;
  virtual bool save(const uint8_t* buf, size_t len) = 0;
  virtual ~MarkIO() = default;
};
