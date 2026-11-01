#pragma once
#include <stddef.h>
#include <stdint.h>
#include "Signatures.h"
#include "WifiFrame.h"

enum class AttackKind : uint8_t { DeauthFlood, EvilTwin, Karma, Pwnagotchi, Flipper, BleSpam, Count };
const char* attackName(AttackKind k);

struct AttackEvent { AttackKind kind; uint8_t mac[6]; char ssid[20]; uint32_t ms; };

// Spots WiFi and Bluetooth attack tools from what the radios hear. Listen only.
class AttackDetector {
 public:
  static const uint32_t kDeauthPerSec = 10;   // more than this per second from one BSSID = flood
  static const size_t kKarmaSsids = 5;        // one BSSID answering for this many SSIDs in 60 s
  static const size_t kSpamAddresses = 20;    // popup-advert addresses each heard only once, within 10 s

  void onWifi(const WifiFrame& f, uint32_t now);
  void onBle(const BleAdv& a, uint32_t now);
  bool poll(uint32_t now, AttackEvent* out);  // new detections, once per kind per 2 minutes
  size_t recent(AttackEvent* out, size_t max) const;  // newest first

 private:
  struct Deauth { bool used; uint8_t mac[6]; uint32_t windowMs; uint32_t count; };
  struct Twin { bool used; uint32_t ssidHash; char ssid[20]; uint8_t oui[4][3]; bool open[4]; uint32_t ouiMs[4];
               uint8_t n; uint32_t lastMs; };
  struct Karma { bool used; uint8_t mac[6]; uint32_t windowMs; uint32_t ssids[kKarmaSsids]; uint8_t n; };
  struct Spam { uint32_t addrHash; uint32_t firstMs; uint16_t count; };

  Deauth deauth_[8] = {};
  Twin twins_[24] = {};
  Karma karma_[8] = {};
  Spam spam_[64] = {};

  AttackEvent pending_[8] = {};
  size_t nPending_ = 0;
  AttackEvent recent_[8] = {};
  size_t nRecent_ = 0;
  uint32_t lastReported_[(int)AttackKind::Count] = {};
  bool reportedEver_[(int)AttackKind::Count] = {};

  void raise(AttackKind k, const uint8_t mac[6], const char* ssid, uint32_t now);
  void checkSpam(uint32_t now);
};
