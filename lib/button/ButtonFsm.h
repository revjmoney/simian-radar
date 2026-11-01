#pragma once
#include <stdint.h>
#include "Config.h"

enum class BtnEvent { None, ShortPress, HoldProgress, HoldDone };

// One button: short tap, or a hold that fills a progress bar and fires at kHoldMarkMs.
class ButtonFsm {
 public:
  BtnEvent update(bool pressed, uint32_t nowMs);
  float holdFraction() const { return fraction_; }

 private:
  static const uint32_t kDebounceMs = 30;
  bool raw_ = false, stable_ = false, doneFired_ = false;
  uint32_t rawSince_ = 0, pressStart_ = 0;
  float fraction_ = 0;
};
