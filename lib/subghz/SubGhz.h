#pragma once
#include <stddef.h>
#include <stdint.h>

// Decoders for what the SX1262 hears on 902-928 MHz. Headers and public broadcasts only; nothing
// encrypted is touched.

// Meshtastic: 16-byte unencrypted header (dest, sender, packet id, flags, channel hash, next hop,
// relay). hops = how many times the packet has been relayed so far.
bool parseMeshtastic(const uint8_t* p, size_t n, uint32_t* from, uint32_t* to, uint8_t* hops);

// MeshCore advert (layout from MeshCore Packet.cpp / Mesh.cpp / AdvertDataHelpers.cpp, MIT).
bool parseMeshCoreAdvert(const uint8_t* p, size_t n, char name[32], double* lat, double* lon, bool* hasPos);

// LoRaWAN PHYPayload: message type (top 3 bits of MHDR) and, for data frames, the device address.
bool parseLoRaWan(const uint8_t* p, size_t n, uint8_t* mtype, uint32_t* devAddr);
const char* netName(uint32_t devAddr);  // TTN, Helium, or "LoRaWAN"

// Z-Wave R1/R2 frame (9.6/40 kbps): home ID, source node, header type. Checks length and checksum.
bool parseZWave(const uint8_t* p, size_t n, uint32_t* homeId, uint8_t* src, uint8_t* type);

// Fine Offset WH24/WH65 family weather stations (Ambient, Ecowitt, Misol). Frame after the 2D D4 sync.
struct WxReading {
  uint8_t id;
  bool hasTemp, hasHum, hasWind, lowBatt;
  float tempC, windMs;
  uint8_t hum;
  uint16_t windDir;
};
bool parseWeather(const uint8_t* p, size_t n, WxReading* out);

// 902-928 MHz sweep bookkeeping: spots RFID / toll-tag readers, which hop strong carriers across
// the whole band for as long as they run.
class SweepStats {
 public:
  static const int kBins = 52;  // 0.5 MHz each
  void add(float mhz, float rssi, uint32_t now);
  bool rfidLikely(uint32_t now);
  float peak(int bin) const { return bin >= 0 && bin < kBins ? peak_[bin] : -140; }
  float level(int bin) const { return bin >= 0 && bin < kBins ? level_[bin] : -140; }

 private:
  float level_[kBins] = {}, peak_[kBins] = {};
  uint32_t strongMs_[kBins] = {};
  bool hopping_ = false;
  uint32_t hopSinceMs_ = 0;
  bool init_ = false;
};
