#pragma once
#include <stddef.h>
#include <stdint.h>

struct Drone {
  bool used;
  uint8_t mac[6];
  char id[21];
  double lat, lon;
  float altM, speedMps;
  double pilotLat, pilotLon;
  bool hasId, hasPos, hasPilot;
  int8_t rssi;
  uint32_t lastMs;
};

// FAA Remote ID (ASTM F3411) drones heard over WiFi beacons and Bluetooth. Drones are keyed by the
// sender address, because Bluetooth sends each message type separately and only the address ties a
// position to an ID.
class DroneTable {
 public:
  static const uint32_t kExpireMs = 60000;
  // WiFi: the Remote ID vendor element body after its type byte (counter byte + message pack).
  void onWifiIe(const uint8_t mac[6], const uint8_t* p, size_t len, int8_t rssi, uint32_t now);
  // Bluetooth: service data for UUID 0xFFFA after the UUID (app code 0x0D, counter, one message).
  void onBleServiceData(const uint8_t mac[6], const uint8_t* p, size_t len, int8_t rssi, uint32_t now);
  size_t list(Drone* out, size_t max, uint32_t now);  // strongest first

 private:
  Drone drones_[8] = {};
  Drone* slot(const uint8_t mac[6], int8_t rssi, uint32_t now);
  void message(Drone* d, const uint8_t* msg, size_t len);
};
