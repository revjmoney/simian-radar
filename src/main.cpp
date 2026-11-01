#include <Arduino.h>
#include <LittleFS.h>
#include <Wire.h>
#include "AppLogic.h"
#include "AttackDetector.h"
#include "ButtonFsm.h"
#include "CamDb.h"
#include "Collectors.h"
#include "Config.h"
#include "DroneTable.h"
#include "FollowDetector.h"
#include "Geo.h"
#include "MarkStore.h"
#include "Sky.h"
#include "SubGhz.h"
#include "hw/BleScan.h"
#include "hw/CamDbPartition.h"
#include "hw/Gps.h"
#include "hw/LittleFsMarkIO.h"
#include "hw/SubGhzRadio.h"
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
static Collectors collectors;
static AttackDetector attacks;
static DroneTable drones;
static SkyGuard sky;
static SweepStats sweep;
static SeenTable<24> meshNodes, lorawanDevs, zwaveNodes, wxStations;

static Fix lastFix = {};
static float vbat = 0;
static Mode lastMode = Mode::Cam;
static bool subGhzOk = false;
static uint32_t lastDrawMs = 0, lastLogMs = 0, lastVbatMs = 0, lastFlockCheckMs = 0, lastSkyMs = 0;

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

static void setText(Seen* s, const char* t) {
  strncpy(s->text, t, sizeof s->text - 1);
  s->text[sizeof s->text - 1] = 0;
}

static void key4(uint8_t k[6], uint32_t v, uint8_t tag) {
  k[0] = tag; k[1] = 0;
  k[2] = v >> 24; k[3] = v >> 16; k[4] = v >> 8; k[5] = v;
}

static void onBle(const BleAdv& a, BleKind k, const uint8_t key[8]) {
  uint32_t now = millis();
  follow.observe(key, k, a.rssi, now, app.hasFix(), lastFix.lat, lastFix.lon);
  if (k == BleKind::GlassesMeta || k == BleKind::GlassesSnap) app.onGlasses(k, a.rssi, key, now);
  if (k == BleKind::Flock) app.onFlockHeard(now);
  collectors.onBle(a, k, key, now);
  attacks.onBle(a, now);
  if (a.sdUuid == 0xFFFA && a.sd) drones.onBleServiceData(a.addr, a.sd, a.sdLen, a.rssi, now);
}

static void onWifiFrame(const WifiFrame& f, int8_t rssi, uint8_t ch, uint32_t now) {
  collectors.onWifi(f, rssi, ch, now);
  attacks.onWifi(f, now);
  if (f.ridIe && f.hasA2) drones.onWifiIe(f.a2, f.ridIe, f.ridLen, rssi, now);
}

static void onWifiDwell(uint8_t ch, uint32_t ms, uint32_t frames) {
  collectors.onFrameCount(ch, frames);
  collectors.onDwell(ch, ms);
}

// What the SX1262 hears in the 900 MHz modes.
struct RadioSink : SubGhzSink {
  void onPacket(Mode m, uint8_t variant, const uint8_t* p, size_t n, float rssi, float snr) override {
    (void)snr;
    uint32_t now = millis();
    uint8_t k[6];
    char t[24];
    int8_t r = (int8_t)rssi;
    if (m == Mode::Mesh) {
      uint32_t from, to;
      uint8_t hops;
      char name[32];
      double lat, lon;
      bool hasPos;
      if (variant == 0 && parseMeshtastic(p, n, &from, &to, &hops)) {
        key4(k, from, 'T');
        snprintf(t, sizeof t, "MT !%08lx h%u", (unsigned long)from, hops);
      } else if (variant == 1 && parseMeshCoreAdvert(p, n, name, &lat, &lon, &hasPos)) {
        uint32_t h = 2166136261u;
        for (const char* c = name; *c; c++) h = (h ^ (uint8_t)*c) * 16777619u;
        key4(k, h, 'C');
        snprintf(t, sizeof t, "MC %s", name[0] ? name : "(no name)");
      } else if (variant == 1) {
        key4(k, 0, 'c');
        snprintf(t, sizeof t, "MeshCore traffic");
      } else {
        return;
      }
      setText(meshNodes.upsert(k, r, now), t);
    } else if (m == Mode::LoRaWan) {
      uint8_t mtype;
      uint32_t addr;
      if (!parseLoRaWan(p, n, &mtype, &addr)) return;
      key4(k, addr, 'L');
      if (mtype == 0) snprintf(t, sizeof t, "JOIN REQUEST");
      else snprintf(t, sizeof t, "%s %08lX", netName(addr), (unsigned long)addr);
      setText(lorawanDevs.upsert(k, r, now), t);
    } else if (m == Mode::ZWave) {
      uint32_t home;
      uint8_t src, type;
      if (!parseZWave(p, n, &home, &src, &type)) return;
      key4(k, home ^ src, 'Z');
      snprintf(t, sizeof t, "%08lX n%u", (unsigned long)home, src);
      setText(zwaveNodes.upsert(k, r, now), t);
    } else if (m == Mode::Wx) {
      WxReading w;
      if (!parseWeather(p, n, &w)) return;
      key4(k, w.id, 'W');
      char temp[8] = "--", hum[6] = "--", wind[8] = "--";
      if (w.hasTemp) snprintf(temp, sizeof temp, "%.0fF", w.tempC * 1.8f + 32);
      if (w.hasHum) snprintf(hum, sizeof hum, "%u%%", w.hum);
      if (w.hasWind) snprintf(wind, sizeof wind, "%.0fmph", w.windMs * 2.23694f);
      snprintf(t, sizeof t, "%s %s %s", temp, hum, wind);
      setText(wxStations.upsert(k, r, now), t);
    }
  }
  void onSweep(float mhz, float rssi) override { sweep.add(mhz, rssi, millis()); }
};
static RadioSink radioSink;

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

// Rows for the list modes, formatted for the screen.
static size_t buildRows(Mode m, Seen* rows, size_t max, uint32_t now, ScreenData& sd, char* foot, size_t footLen) {
  static Seen tmp[7];
  size_t n = 0;
  char t[40];
  switch (m) {
    case Mode::Phone:
      n = collectors.phones(tmp, max, now);
      for (size_t i = 0; i < n; i++) {
        rows[i] = tmp[i];
        snprintf(t, sizeof t, "%s %s", makerName((Maker)tmp[i].maker), tmp[i].text);
        setText(&rows[i], t);
      }
      snprintf(foot, footLen, "%u in last min", (unsigned)collectors.phoneCount(now));
      break;
    case Mode::SpyCam:
      n = collectors.spycams(rows, max, now);
      snprintf(foot, footLen, n ? "%u cameras" : "no cameras heard", (unsigned)n);
      break;
    case Mode::Attack: {
      AttackEvent ev[7];
      n = attacks.recent(ev, max);
      for (size_t i = 0; i < n; i++) {
        rows[i] = Seen{};
        snprintf(t, sizeof t, "%s %lum", attackName(ev[i].kind), (unsigned long)((now - ev[i].ms) / 60000));
        setText(&rows[i], t);
      }
      sd.rowBars = false;
      sd.rowFlagAll = true;
      snprintf(foot, footLen, n ? "since power on" : "nothing heard");
      break;
    }
    case Mode::Tags:
      n = collectors.tags(tmp, max, now);
      for (size_t i = 0; i < n; i++) {
        rows[i] = tmp[i];
        snprintf(t, sizeof t, "%s %lum", tmp[i].text, (unsigned long)((now - tmp[i].firstMs) / 60000));
        setText(&rows[i], t);
      }
      snprintf(foot, footLen, "trackers + glasses");
      break;
    case Mode::Bodycam:
      n = collectors.bodycams(tmp, max, now);
      for (size_t i = 0; i < n; i++) {
        rows[i] = tmp[i];
        snprintf(t, sizeof t, "AXON %02X%02X%02X", tmp[i].key[3], tmp[i].key[4], tmp[i].key[5]);
        setText(&rows[i], t);
      }
      snprintf(foot, footLen, n ? "%u Axon devices" : "none heard", (unsigned)n);
      break;
    case Mode::Drone: {
      Drone d[7];
      n = drones.list(d, max, now);
      for (size_t i = 0; i < n; i++) {
        rows[i] = Seen{};
        rows[i].rssi = d[i].rssi;
        const char* id = d[i].hasId ? d[i].id : "?";
        size_t L = strlen(id);
        const char* tail = L > 8 ? id + L - 8 : id;
        if (d[i].hasPos && app.hasFix()) {
          char dist[8];
          formatDistance(haversineM(app.fix().lat, app.fix().lon, d[i].lat, d[i].lon), dist, sizeof dist);
          snprintf(t, sizeof t, "%s %s", tail, dist);
        } else if (d[i].hasPos) {
          snprintf(t, sizeof t, "%s %.0fm", tail, d[i].altM);
        } else {
          snprintf(t, sizeof t, "%s", tail);
        }
        setText(&rows[i], t);
      }
      if (n && d[0].hasPilot && app.hasFix()) {
        char dist[8];
        formatDistance(haversineM(app.fix().lat, app.fix().lon, d[0].pilotLat, d[0].pilotLon), dist, sizeof dist);
        snprintf(foot, footLen, "pilot %s away", dist);
      } else {
        snprintf(foot, footLen, n ? "%u drones" : "no Remote ID", (unsigned)n);
      }
      break;
    }
    case Mode::Mesh: n = meshNodes.list(rows, max, now, 300000); break;
    case Mode::LoRaWan: n = lorawanDevs.list(rows, max, now, 300000); break;
    case Mode::ZWave: n = zwaveNodes.list(rows, max, now, 300000); break;
    case Mode::Wx: n = wxStations.list(rows, max, now, 600000); break;
    default: break;
  }
  if (isSubGhz(m)) snprintf(foot, footLen, "%s", subGhzOk ? subGhzStatus() : "SX1262 not found");
  return n;
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
  subGhzOk = subGhzBegin();
  if (!subGhzOk) Serial.printf("SX1262: %s\n", subGhzStatus());
  vbat = readVbat();
  delay(1500);
}

void loop() {
  uint32_t now = millis();

  Fix f = {};
  if (gpsPoll(&f)) {
    lastFix = f;
    app.onFix(f, now);
    sky.onFix(f, now);
  }
  lastFix.sats = f.sats;

  BtnEvent e = button.update(digitalRead(kPinButton) == LOW, now);
  if (e != BtnEvent::None) app.onButton(e, button.holdFraction(), now);

  wifiScanHop(now, onWifiDwell);
  wifiPoll(now, onWifiFrame);
  if (wifiFlockHeardSince(lastFlockCheckMs)) app.onFlockHeard(now);
  lastFlockCheckMs = now;
  blePoll(onBle, now);
  subGhzPoll(now, &radioSink);

  FollowAlert alert;
  if (follow.poll(now, &alert)) app.onFollow(alert, now);
  AttackEvent attack;
  if (attacks.poll(now, &attack)) app.onToast(attackName(attack.kind), now);
  if (now - lastSkyMs >= 1000) {
    lastSkyMs = now;
    SatInfo s[40];
    sky.onSats(s, gpsSats(s, 40, now), now);
    sky.tick(now, app.hasFix());
  }
  const char* guard;
  if (sky.poll(now, &guard)) app.onToast(guard, now);
  app.tick(now);

  // The SX1262 belongs to whichever 900 MHz mode is on screen, and to nothing else.
  if (app.mode() != lastMode) {
    if (isSubGhz(lastMode)) subGhzLeave();
    if (isSubGhz(app.mode()) && subGhzOk) subGhzEnter(app.mode(), now);
    lastMode = app.mode();
  }

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
    static Seen rows[7];
    static SatInfo sats[16];
    static float chan[11];
    static char foot[32];
    ScreenData sd;
    foot[0] = 0;
    sd.satCount = lastFix.sats;
    sd.vbat = vbat;
    sd.camCount = db.count();
    sd.markCount = marks.count();
    switch (app.mode()) {
      case Mode::Wifi: sd.aps = aps; sd.nAps = wifiScanResults(aps, 7, now); break;
      case Mode::Bt: sd.ble = ble; sd.nBle = bleList(ble, 7, now); break;
      case Mode::Chan: collectors.channelRates(chan); sd.chan = chan; break;
      case Mode::Sweep: sd.sweep = &sweep; sd.rfid = sweep.rfidLikely(now); break;
      case Mode::Sky:
        sd.sats = sats;
        sd.nSats = gpsSats(sats, 16, now);
        sd.hdop = gpsHdop();
        sd.altM = gpsAltM();
        break;
      default:
        sd.rows = rows;
        sd.nRows = buildRows(app.mode(), rows, 7, now, sd, foot, sizeof foot);
        break;
    }
    sd.footer = foot;
    canvas.clear();
    drawFrame(canvas, app, sd);
    canvas.send();
    if (now - lastLogMs >= 2000) {
      lastLogMs = now;
      Serial.printf("mode=%s gpschars=%lu fix=%d sats=%u cams=%lu within=%lu phones=%u marks=%u overlay=%d radio=%s vbat=%.2f\n",
                    modeTag(app.mode()), (unsigned long)gpsChars(), app.hasFix(), lastFix.sats,
                    (unsigned long)db.count(), (unsigned long)app.withinCount(),
                    (unsigned)collectors.phoneCount(now), (unsigned)marks.count(), (int)o,
                    subGhzOk ? subGhzStatus() : "none", vbat);
    }
  }
  delay(5);
}
