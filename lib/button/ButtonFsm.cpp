#include "ButtonFsm.h"

BtnEvent ButtonFsm::update(bool pressed, uint32_t nowMs) {
  if (pressed != raw_) {
    raw_ = pressed;
    rawSince_ = nowMs;
  }
  if (raw_ != stable_ && nowMs - rawSince_ >= kDebounceMs) {
    stable_ = raw_;
    if (stable_) {
      pressStart_ = rawSince_;  // time the contact actually closed
      doneFired_ = false;
      fraction_ = 0;
    } else {
      uint32_t held = rawSince_ - pressStart_;
      fraction_ = 0;
      return held < kShortPressMaxMs ? BtnEvent::ShortPress : BtnEvent::None;
    }
  }
  if (!stable_ || doneFired_ || raw_ != stable_) return BtnEvent::None;  // released, debouncing
  uint32_t held = nowMs - pressStart_;
  if (held >= kHoldMarkMs) {
    doneFired_ = true;
    fraction_ = 1;
    return BtnEvent::HoldDone;
  }
  if (held < kShortPressMaxMs) return BtnEvent::None;
  fraction_ = (float)(held - kShortPressMaxMs) / (float)(kHoldMarkMs - kShortPressMaxMs);
  return BtnEvent::HoldProgress;
}
