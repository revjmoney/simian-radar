#pragma once
#include <stddef.h>
#include <stdint.h>

// Distances and timing (spec: Global Constraints). Tune here after drive tests.
constexpr float kAlertRadiusM = 304.8f;      // 1000 ft
constexpr float kWithinRadiusM = 8046.7f;    // 5 mi
constexpr float kNewFlockRadiusM = 152.4f;   // 500 ft
constexpr float kMovingSpeedMps = 0.894f;    // 2 mph
constexpr int8_t kGlassesRssi = -60;         // ~30 ft
constexpr uint32_t kFollowMinMs = 10UL * 60UL * 1000UL;
constexpr float kFollowMinTravelM = 804.67f; // 0.5 mi
constexpr uint32_t kFollowRecentMs = 2UL * 60UL * 1000UL;
constexpr uint32_t kHoldMarkMs = 5000;
constexpr uint32_t kShortPressMaxMs = 1000;
constexpr size_t kMaxMarks = 500;
constexpr size_t kMaxBleTracked = 64;

// Heltec V4 pins (MeshCore variants/heltec_v4). Vext is active HIGH on the V4.
constexpr int kPinSda = 17;
constexpr int kPinScl = 18;
constexpr int kPinOledRst = 21;
constexpr int kPinVext = 36;
constexpr int kPinButton = 0;
constexpr int kPinLed = 35;
constexpr int kPinGpsRx = 38;
constexpr int kPinGpsTx = 39;
constexpr int kPinGpsEn = 34;
constexpr int kPinGpsReset = 42;
constexpr int kPinVbat = 1;
constexpr int kPinAdcCtrl = 37;
