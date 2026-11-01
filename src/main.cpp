#include <Arduino.h>
#include <LittleFS.h>
#include <Wire.h>
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

// Same driver and init as MeshCore uses for this board's OLED.
static Adafruit_SSD1306 display(128, 64, &Wire, kPinOledRst);
static Canvas canvas(display);
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
  canvas.clear();
  canvas.setFont(u8g2_font_7x13B_tr);
  canvas.drawStr(11, 30, "SIMIAN");
  canvas.drawStr(15, 46, "RADAR");
  canvas.setFont(u8g2_font_4x6_tr);
  canvas.drawStr(4, 100, "(c) OpenStreet");
  canvas.drawStr(2, 108, "Map contributors");
  canvas.send();
}


void setup() {
  Serial.begin(115200);
  pinMode(kPinVext, OUTPUT);
  digitalWrite(kPinVext, kVextOn);
  pinMode(kPinLed, OUTPUT);
  pinMode(kPinAdcCtrl, OUTPUT);
  pinMode(kPinButton, INPUT_PULLUP);
  delay(50);
  Wire.begin(kPinSda, kPinScl);
  display.setRotation(1);  // portrait, 64 x 128
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C, true, false)) Serial.println("OLED init failed");
  canvas.begin();
  bootScreen();

  if (!LittleFS.begin(true, "/littlefs", 10, "littlefs")) Serial.println("LittleFS mount failed");
  marks.begin();
  size_t len = 0;
  const uint8_t* data = camDbMap(&len);
  if (!data || !db.open(data, len)) Serial.println("no camera database");
  app.setIgnoreCallback(ignoreTracker);
  gpsBegin();
  wifiScanBegin();
  bleScanBegin();
  vbat = readVbat();
  delay(1500);
}

void loop() {
  uint32_t now = millis();

  Fix f = {};
  if (gpsPoll(&f)) {
    lastFix = f;
    app.onFix(f, now);
  }
  lastFix.sats = f.sats;

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
    canvas.clear();
    drawFrame(canvas, app, aps, nAps, ble, nBle, lastFix.sats, vbat, db.count(), marks.count());
    canvas.send();
    if (now - lastLogMs >= 2000) {
      lastLogMs = now;
      Serial.printf("gpschars=%lu fix=%d sats=%u lat=%.5f lon=%.5f cams=%lu within=%lu aps=%u ble=%u marks=%u overlay=%d vbat=%.2f\n",
                    (unsigned long)gpsChars(), app.hasFix(), lastFix.sats, lastFix.lat, lastFix.lon, (unsigned long)db.count(),
                    (unsigned long)app.withinCount(), (unsigned)nAps, (unsigned)nBle, (unsigned)marks.count(),
                    (int)o, vbat);
    }
  }
  delay(10);
}
