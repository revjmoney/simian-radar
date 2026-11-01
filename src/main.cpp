#include <Arduino.h>
#include <LittleFS.h>
#include <Wire.h>
#include <U8g2lib.h>
#include "AppLogic.h"
#include "ButtonFsm.h"
#include "CamDb.h"
#include "Config.h"
#include "FollowDetector.h"
#include "MarkStore.h"
#include "hw/BleScan.h"
#include "hw/CamDbPartition.h"
#include "hw/Gps.h"
#include "hw/LittleFsMarkIO.h"
#include "hw/WifiScan.h"
#include "ui/Screens.h"

static U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled(U8G2_R1, kPinOledRst);
static CamDb db;
static LittleFsMarkIO markIo;
static MarkStore marks(markIo);
static AppLogic app(db, marks);
static FollowDetector follow;
static ButtonFsm button;

static Fix lastFix = {};
static float vbat = 0;
static uint32_t lastDrawMs = 0, lastLogMs = 0, lastVbatMs = 0, lastFlockCheckMs = 0;

static void ignoreTracker(const uint8_t key[8]) { follow.ignore(key); }

static float readVbat() {
  analogReadResolution(10);
  digitalWrite(kPinAdcCtrl, HIGH);
  delay(2);
  uint32_t raw = 0;
  for (int i = 0; i < 8; i++) raw += analogRead(kPinVbat);
  digitalWrite(kPinAdcCtrl, LOW);
  return 5.42f * (3.3f / 1024.0f) * (raw / 8.0f);  // MeshCore's Heltec V4 divider
}

static void onBle(const BleAdv& a, BleKind k, const uint8_t key[8]) {
  uint32_t now = millis();
  follow.observe(key, k, a.rssi, now, app.hasFix(), lastFix.lat, lastFix.lon);
  if (k == BleKind::GlassesMeta || k == BleKind::GlassesSnap) app.onGlasses(k, a.rssi, key, now);
  if (k == BleKind::Flock) app.onFlockHeard(now);
}

static void bootScreen() {
  oled.clearBuffer();
  oled.setFont(u8g2_font_7x13B_tr);
  oled.drawStr(11, 30, "SIMIAN");
  oled.drawStr(15, 46, "RADAR");
  oled.setFont(u8g2_font_4x6_tr);
  oled.drawStr(4, 100, "(c) OpenStreet");
  oled.drawStr(2, 108, "Map contributors");
  oled.sendBuffer();
}

#ifdef DEMO
// Demo build: the fixture database is compiled in and a fake drive walks past the cameras while
// scripted events show every screen. For checking the layouts on the real OLED.
extern const uint8_t demoDb[] asm("_binary_test_fixtures_small_bin_start");
extern const uint8_t demoDbEnd[] asm("_binary_test_fixtures_small_bin_end");

static bool demoFix(uint32_t now, Fix* f) {
  uint32_t t = now % 60000;
  *f = {true, 33.6830 + t / 60000.0 * 0.0120, -78.8867, 6.0f, 0.0f, 9, 1793500000 + now / 1000};
  return t > 3000;  // first 3 s of each minute: no fix
}

static void demoEvents(uint32_t now) {
  static uint32_t step = 0;
  uint32_t s = now / 4000;
  if (s == step) return;
  step = s;
  static const uint8_t k[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  switch (s % 12) {
    case 3: app.onFlockHeard(now); break;
    case 4: app.onButton(BtnEvent::HoldProgress, 0.6f, now); break;
    case 5: app.onButton(BtnEvent::HoldDone, 1, now); break;
    case 6: app.onGlasses(BleKind::GlassesMeta, -50, k, now); break;
    case 7: { FollowAlert a = {{1, 2, 3, 4, 5, 6, 7, 8}, BleKind::FindMySeparated, -58}; app.onFollow(a, now); break; }
    case 8: app.onButton(BtnEvent::ShortPress, 0, now); break;  // dismiss tracker
    case 9: app.onButton(BtnEvent::ShortPress, 0, now); break;  // to WIFI
    case 10: app.onButton(BtnEvent::ShortPress, 0, now); break; // to BT
    case 11: app.onButton(BtnEvent::ShortPress, 0, now); break; // back to CAM
  }
}
#endif

void setup() {
  Serial.begin(115200);
  pinMode(kPinVext, OUTPUT);
  digitalWrite(kPinVext, HIGH);
  pinMode(kPinLed, OUTPUT);
  pinMode(kPinAdcCtrl, OUTPUT);
  pinMode(kPinButton, INPUT_PULLUP);
  delay(50);
  Wire.begin(kPinSda, kPinScl);
  oled.begin();
  oled.setContrast(255);
  oled.sendF("ca", 0xD9, 0xF1);  // pre-charge period: brighter pixels
  oled.sendF("ca", 0xDB, 0x40);  // VCOMH deselect level: max
  bootScreen();

  if (!LittleFS.begin(true)) Serial.println("LittleFS mount failed");
  marks.begin();
#ifdef DEMO
  db.open(demoDb, demoDbEnd - demoDb);
#else
  size_t len = 0;
  const uint8_t* data = camDbMap(&len);
  if (!data || !db.open(data, len)) Serial.println("no camera database");
#endif
  app.setIgnoreCallback(ignoreTracker);
  gpsBegin();
  wifiScanBegin();
  bleScanBegin();
  vbat = readVbat();
  delay(1500);
}

void loop() {
  uint32_t now = millis();

#ifdef DEMO
  Fix f;
  if (demoFix(now, &f)) { lastFix = f; app.onFix(f, now); }
  demoEvents(now);
#else
  Fix f = {};
  if (gpsPoll(&f)) {
    lastFix = f;
    app.onFix(f, now);
  }
  lastFix.sats = f.sats;
#endif

  BtnEvent e = button.update(digitalRead(kPinButton) == LOW, now);
  if (e != BtnEvent::None) app.onButton(e, button.holdFraction(), now);

  wifiScanHop(now);
  if (wifiFlockHeardSince(lastFlockCheckMs)) app.onFlockHeard(now);
  lastFlockCheckMs = now;
  blePoll(onBle, now);
  FollowAlert alert;
  if (follow.poll(now, &alert)) app.onFollow(alert, now);
  app.tick(now);

  Overlay o = app.overlay();
  bool blink = o == Overlay::CameraAhead || o == Overlay::TrackerFollowing;
  digitalWrite(kPinLed, blink && (now / 250) % 2 ? HIGH : LOW);

  if (now - lastVbatMs > 10000) {
    lastVbatMs = now;
    vbat = readVbat();
  }
  if (now - lastDrawMs >= 100) {
    lastDrawMs = now;
    static ApInfo aps[7];
    static BleSeen ble[7];
    size_t nAps = wifiScanResults(aps, 7, now);
    size_t nBle = bleList(ble, 7, now);
    oled.clearBuffer();
    drawFrame(oled, app, aps, nAps, ble, nBle, lastFix.sats, vbat, db.count(), marks.count());
    oled.sendBuffer();
    if (now - lastLogMs >= 2000) {
      lastLogMs = now;
      Serial.printf("fix=%d sats=%u lat=%.5f lon=%.5f cams=%lu within=%lu aps=%u ble=%u marks=%u overlay=%d vbat=%.2f\n",
                    app.hasFix(), lastFix.sats, lastFix.lat, lastFix.lon, (unsigned long)db.count(),
                    (unsigned long)app.withinCount(), (unsigned)nAps, (unsigned)nBle, (unsigned)marks.count(),
                    (int)o, vbat);
    }
  }
  delay(10);
}
