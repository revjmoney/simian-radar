#pragma once
#include "Makers.h"
#include "SeenTable.h"
#include "Signatures.h"
#include "WifiFrame.h"

// Lists behind the PHONE, SPYCAM, CHAN, TAGS and BODYCAM modes.
class Collectors {
 public:
  static const uint32_t kExpireMs = 60000;
  void onWifi(const WifiFrame& f, int8_t rssi, uint8_t channel, uint32_t now);
  void onDwell(uint8_t channel, uint32_t ms);  // the sniffer spent ms listening on channel
  void onFrameCount(uint8_t channel, uint32_t frames);  // every frame heard there, parsed or not
  void onBle(const BleAdv& a, BleKind k, const uint8_t key[8], uint32_t now);

  size_t phones(Seen* out, size_t max, uint32_t now) { return phones_.list(out, max, now, kExpireMs); }
  size_t phoneCount(uint32_t now) const { return phones_.count(now, kExpireMs); }
  size_t spycams(Seen* out, size_t max, uint32_t now) { return cams_.list(out, max, now, kExpireMs); }
  size_t tags(Seen* out, size_t max, uint32_t now) { return tags_.list(out, max, now, kExpireMs); }
  size_t bodycams(Seen* out, size_t max, uint32_t now) { return bodycams_.list(out, max, now, kExpireMs); }
  void channelRates(float out[11]) const;  // frames per second per channel 1-11
  size_t phoneTableSize() const { return phones_.size(); }

 private:
  SeenTable<48> phones_;
  SeenTable<24> cams_;
  SeenTable<24> tags_;
  SeenTable<16> bodycams_;
  uint32_t frames_[11] = {}, dwellMs_[11] = {};
};
