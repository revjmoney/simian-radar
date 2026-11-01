#pragma once
#include <stdint.h>

enum class Maker : uint8_t {
  Unknown, Random,
  Axon, Flock,
  Ring, Blink, Wyze, Arlo, Eufy, Hikvision, Dahua, Reolink, Nest, Tuya,  // cameras
  Apple, Samsung, Google, Amazon, Microsoft, Intel, Motorola, Huawei, Xiaomi, Oneplus, Lg, Sony,
  Espressif,
};

Maker makerOf(const uint8_t mac[6]);          // Random when the locally-administered bit is set
bool isRandomMac(const uint8_t mac[6]);
bool isCameraMaker(Maker m);
Maker cameraFromSsid(const char* ssid);       // setup/default SSIDs of camera makers, else Unknown
const char* makerName(Maker m);
