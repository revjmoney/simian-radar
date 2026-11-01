#include "Collectors.h"

static const uint32_t kRateWindowMs = 5000;

static void setText(Seen* s, const char* t) {
  strncpy(s->text, t, sizeof s->text - 1);
  s->text[sizeof s->text - 1] = 0;
}

void Collectors::onWifi(const WifiFrame& f, int8_t rssi, uint8_t channel, uint32_t now) {
  if (channel >= 1 && channel <= 11) frames_[channel - 1]++;
  if (!f.hasA2) return;
  Maker m = makerOf(f.a2);

  if (f.type == 0 && f.subtype == kWifiProbeReq) {
    Seen* s = phones_.upsert(f.a2, rssi, now);
    s->maker = (uint8_t)m;
    if (f.hasSsid && f.ssid[0]) setText(s, f.ssid);
  }

  Maker cam = isCameraMaker(m) ? m : Maker::Unknown;
  if (cam == Maker::Unknown && f.type == 0 && (f.subtype == kWifiBeacon || f.subtype == kWifiProbeResp) && f.hasSsid)
    cam = cameraFromSsid(f.ssid);
  if (cam != Maker::Unknown) {
    Seen* s = cams_.upsert(f.a2, rssi, now);
    s->maker = (uint8_t)cam;
    setText(s, makerName(cam));
  }

  if (m == Maker::Axon) {
    Seen* s = bodycams_.upsert(f.a2, rssi, now);
    s->maker = (uint8_t)m;
    setText(s, makerName(m));
  }
}

void Collectors::onDwell(uint8_t channel, uint32_t ms) {
  if (channel < 1 || channel > 11) return;
  uint8_t i = channel - 1;
  dwellMs_[i] += ms;
  if (dwellMs_[i] > kRateWindowMs) {  // age old traffic out so the bars follow what is happening now
    dwellMs_[i] /= 2;
    frames_[i] /= 2;
  }
}

void Collectors::channelRates(float out[11]) const {
  for (int i = 0; i < 11; i++) out[i] = dwellMs_[i] ? frames_[i] * 1000.0f / dwellMs_[i] : 0;
}

void Collectors::onBle(const BleAdv& a, BleKind k, const uint8_t key[8], uint32_t now) {
  switch (k) {
    case BleKind::FindMySeparated:
    case BleKind::FindMyNearOwner:
    case BleKind::Tile:
    case BleKind::SmartTag:
    case BleKind::GoogleFmdn:
    case BleKind::GlassesMeta:
    case BleKind::GlassesSnap: {
      Seen* s = tags_.upsert(key, a.rssi, now);
      s->maker = (uint8_t)k;
      setText(s, bleLabel(k));
      break;
    }
    default:
      break;
  }
  if (makerOf(a.addr) == Maker::Axon) {
    Seen* s = bodycams_.upsert(a.addr, a.rssi, now);
    s->maker = (uint8_t)Maker::Axon;
    setText(s, makerName(Maker::Axon));
  }
}
