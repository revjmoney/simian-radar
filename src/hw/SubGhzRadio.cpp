#include "SubGhzRadio.h"
#include <Arduino.h>
#include <RadioLib.h>
#include <SPI.h>
#include "Config.h"

// SX1262 and front-end pins (MeshCore variants/heltec_v4, MIT).
static const int kNss = 8, kSck = 9, kMosi = 10, kMiso = 11, kRst = 12, kBusy = 13, kDio1 = 14;
static const int kFemPower = 7, kFemCsd = 2, kGc1109Cps = 46, kKctCtx = 5;
static const float kTcxo = 1.8f;

static SPIClass spi(FSPI);
static SX1262 radio = new Module(kNss, kDio1, kRst, kBusy, spi);
static volatile bool rxFlag = false;
static bool ready = false;
static Mode current = Mode::Cam;
static uint8_t variant = 0;
static uint32_t stepMs = 0;
static uint8_t sweepBin = 0;
static char status[24] = "";

static void IRAM_ATTR onRx() { rxFlag = true; }

// Front end: power the LDO and enable the GC1109 (or KCT8103L) in its receive path. Same
// detection MeshCore uses: GPIO2 reads LOW with the GC1109's pull-down, HIGH with the KCT8103L.
static void femReceive() {
  pinMode(kFemPower, OUTPUT);
  digitalWrite(kFemPower, HIGH);
  delay(1);
  pinMode(kFemCsd, INPUT);
  delay(1);
  bool kct = digitalRead(kFemCsd) == HIGH;
  pinMode(kFemCsd, OUTPUT);
  digitalWrite(kFemCsd, HIGH);
  if (kct) {
    pinMode(kKctCtx, OUTPUT);
    digitalWrite(kKctCtx, LOW);  // LNA on
  } else {
    pinMode(kGc1109Cps, OUTPUT);
    digitalWrite(kGc1109Cps, LOW);  // PA bypass; receive path
  }
}

bool subGhzBegin() {
  femReceive();
  spi.begin(kSck, kMiso, kMosi, kNss);
  int st = radio.begin(915.0, 125.0, 9, 5, RADIOLIB_SX126X_SYNC_WORD_PRIVATE, 0, 16, kTcxo);
  if (st == RADIOLIB_ERR_SPI_CMD_FAILED || st == RADIOLIB_ERR_SPI_CMD_INVALID)
    st = radio.begin(915.0, 125.0, 9, 5, RADIOLIB_SX126X_SYNC_WORD_PRIVATE, 0, 16, 0.0f);  // MeshCore's TCXO fallback
  if (st != RADIOLIB_ERR_NONE) {
    snprintf(status, sizeof status, "SX1262 err %d", st);
    return false;
  }
  radio.setDio2AsRfSwitch(true);
  radio.setRxBoostedGainMode(true);
  radio.setPacketReceivedAction(onRx);
  radio.standby();
  ready = true;
  return true;
}

static void loraRx(float mhz, float bw, uint8_t sf, uint8_t syncWord, uint16_t preamble, const char* label) {
  radio.standby();
  radio.begin(mhz, bw, sf, 5, syncWord, 0, preamble, kTcxo);
  radio.setDio2AsRfSwitch(true);
  radio.setRxBoostedGainMode(true);
  radio.setCRC(true);
  radio.setPacketReceivedAction(onRx);
  rxFlag = false;
  radio.startReceive();
  snprintf(status, sizeof status, "%s", label);
}

static void fskRx(float mhz, float kbps, float devKhz, float rxBw, const uint8_t* sync, size_t syncLen, size_t fixedLen,
                  const char* label) {
  radio.standby();
  radio.beginFSK(mhz, kbps, devKhz, rxBw, 0, 16, kTcxo);
  radio.setDio2AsRfSwitch(true);
  radio.setRxBoostedGainMode(true);
  radio.setSyncWord((uint8_t*)sync, syncLen);
  radio.setCRC(0);
  radio.setDataShaping(RADIOLIB_SHAPING_NONE);
  radio.setWhitening(false);
  radio.fixedPacketLengthMode(fixedLen);
  radio.setPacketReceivedAction(onRx);
  rxFlag = false;
  radio.startReceive();
  snprintf(status, sizeof status, "%s", label);
}

// LoRaWAN US915 sub-band 2 uplinks: 903.9 + 0.2 MHz x channel, SF7-SF10, 125 kHz.
static void lorawanStep() {
  uint8_t ch = variant % 8, sf = 7 + (variant / 8) % 4;
  char label[24];
  snprintf(label, sizeof label, "%.1f SF%u", 903.9 + 0.2 * ch, sf);
  loraRx(903.9 + 0.2 * ch, 125.0, sf, 0x34, 8, label);
}

static void meshStep() {
  if (variant % 2 == 0) loraRx(906.875, 250.0, 11, 0x2B, 16, "Meshtastic LongFast");
  else loraRx(910.525, 62.5, 7, RADIOLIB_SX126X_SYNC_WORD_PRIVATE, 16, "MeshCore US");
}

void subGhzEnter(Mode m, uint32_t now) {
  if (!ready || !isSubGhz(m)) return;
  current = m;
  variant = 0;
  stepMs = now;
  sweepBin = 0;
  switch (m) {
    case Mode::Mesh: meshStep(); break;
    case Mode::LoRaWan: lorawanStep(); break;
    case Mode::ZWave: {
      static const uint8_t sync[] = {0x55, 0xF0};  // Z-Wave R2: preamble 0x55, start of frame 0xF0
      fskRx(908.40, 40.0, 20.0, 117.3, sync, sizeof sync, 64, "Z-Wave 908.4 40k");
      break;
    }
    case Mode::Wx: {
      static const uint8_t sync[] = {0x2D, 0xD4};  // Fine Offset WH24/WH65
      fskRx(915.0, 17.24, 40.0, 117.3, sync, sizeof sync, 17, "WX 915 17.2k");
      break;
    }
    case Mode::Sweep:
      radio.standby();
      radio.beginFSK(902.25, 4.8, 5.0, 156.2, 0, 16, kTcxo);
      radio.setDio2AsRfSwitch(true);
      radio.setRxBoostedGainMode(true);
      snprintf(status, sizeof status, "902-928 sweep");
      break;
    default: break;
  }
}

void subGhzLeave() {
  if (!ready) return;
  radio.standby();
  rxFlag = false;
  status[0] = 0;
}

void subGhzPoll(uint32_t now, SubGhzSink* sink) {
  if (!ready || !isSubGhz(current)) return;
  if (current == Mode::Sweep) {
    for (int i = 0; i < 4; i++) {  // a few bins per loop pass keeps the UI responsive
      float mhz = 902.25f + sweepBin * 0.5f;
      radio.setFrequency(mhz);
      radio.startReceive();
      delayMicroseconds(800);
      float rssi = radio.getRSSI(false);  // instantaneous, not last packet
      if (sink) sink->onSweep(mhz, rssi);
      sweepBin = (sweepBin + 1) % 52;
    }
    return;
  }
  if (rxFlag) {
    rxFlag = false;
    uint8_t buf[256];
    size_t n = radio.getPacketLength();
    if (n > sizeof buf) n = sizeof buf;
    if (radio.readData(buf, n) == RADIOLIB_ERR_NONE && sink)
      sink->onPacket(current, current == Mode::LoRaWan ? variant % 8 : variant % 2, buf, n, radio.getRSSI(),
                     radio.getSNR());
    radio.startReceive();
  }
  // Step through presets/channels: MESH every 30 s, LORAWAN every 250 ms.
  uint32_t dwell = current == Mode::Mesh ? 30000 : current == Mode::LoRaWan ? 250 : 0;
  if (dwell && now - stepMs >= dwell) {
    stepMs = now;
    variant++;
    if (current == Mode::Mesh) meshStep();
    else lorawanStep();
  }
}

const char* subGhzStatus() { return status; }
