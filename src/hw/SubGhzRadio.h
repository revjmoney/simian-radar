#pragma once
#include <stddef.h>
#include <stdint.h>
#include "AppLogic.h"

// Receives from the SX1262 for the 900 MHz modes. Listen only: the radio is only ever put in
// standby, receive, or read for RSSI. There is no transmit path.
struct SubGhzSink {
  // variant: MESH 0 = Meshtastic, 1 = MeshCore; LORAWAN channel index; others 0.
  virtual void onPacket(Mode m, uint8_t variant, const uint8_t* p, size_t n, float rssi, float snr) = 0;
  virtual void onSweep(float mhz, float rssi) = 0;
  virtual ~SubGhzSink() = default;
};

bool subGhzBegin();                      // false if the SX1262 did not answer
void subGhzEnter(Mode m, uint32_t now);  // tune for a 900 MHz mode (no-op for other modes)
void subGhzLeave();                      // standby
void subGhzPoll(uint32_t now, SubGhzSink* sink);
const char* subGhzStatus();              // what it is listening to right now, for the screen
