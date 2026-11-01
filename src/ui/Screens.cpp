#include "Screens.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static const int W = 64, H = 128;

static void center(Canvas& d, int y, const char* s) { d.drawStr((W - d.getStrWidth(s)) / 2, y, s); }

// Filled arrowhead pointing deg degrees clockwise from straight up, centred on (cx, cy).
static void arrow(Canvas& d, int cx, int cy, int r, int deg) {
  float a = deg * (float)M_PI / 180.0f, s = sinf(a), c = cosf(a);
  auto px = [&](float x, float y) { return (int)lroundf(cx + x * c - y * s); };
  auto py = [&](float x, float y) { return (int)lroundf(cy + x * s + y * c); };
  float tip = -r, back = r * 0.8f, half = r * 0.7f, notch = back * 0.4f;
  d.drawTriangle(px(0, tip), py(0, tip), px(-half, back), py(-half, back), px(0, notch), py(0, notch));
  d.drawTriangle(px(0, tip), py(0, tip), px(half, back), py(half, back), px(0, notch), py(0, notch));
}

static void topStrip(Canvas& d, const char* tag, uint8_t sats, float vbat) {
  d.setFont(u8g2_font_5x7_tr);
  int tagW = d.getStrWidth(tag) + 4;
  d.drawBox(0, 0, tagW, 9);
  d.setDrawColor(0);
  d.drawStr(2, 7, tag);
  d.setDrawColor(1);
  char s[8];
  snprintf(s, sizeof s, "*%u", sats);
  if (tagW + 4 + d.getStrWidth(s) < 50) d.drawStr(tagW + 4, 7, s);
  d.drawFrame(51, 1, 11, 6);
  d.drawBox(62, 3, 1, 2);
  float frac = (vbat - 3.3f) / (4.2f - 3.3f);
  if (frac < 0) frac = 0;
  if (frac > 1) frac = 1;
  d.drawBox(53, 3, (int)lroundf(7 * frac), 2);
  d.drawHLine(0, 10, W);
}

static void signalBar(Canvas& d, int y, int8_t rssi) {
  int w = (rssi + 100) * 50 / 70;  // -100 dBm empty, -30 dBm full
  if (w < 1) w = 1;
  if (w > 50) w = 50;
  d.drawBox(1, y, w, 2);
}

static void camList(Canvas& d, const AppLogic& app) {
  CamRow rows[4];
  size_t n = app.camRows(rows, 4);
  for (size_t i = 0; i < n; i++) {
    int y = 12 + (int)i * 25;
    bool hi = i == 0;
    if (hi) d.drawBox(0, y, W, 24);
    d.setDrawColor(hi ? 0 : 1);
    if (rows[i].unconfirmed) d.drawCircle(10, y + 12, 9);  // hollow ring = unconfirmed
    if (rows[i].arrowDeg >= 0) {
      arrow(d, 10, y + 12, 7, rows[i].arrowDeg);
    } else {
      d.setFont(u8g2_font_6x10_tr);
      d.drawStr(rows[i].compass[1] ? 4 : 7, y + 16, rows[i].compass);
    }
    d.setFont(u8g2_font_7x13B_tr);
    d.drawStr(22, y + 12, rows[i].dist);
    d.setFont(u8g2_font_4x6_tr);
    d.drawStr(22, y + 20, rows[i].brand);
    d.setDrawColor(1);
  }
  d.drawHLine(0, 115, W);
  d.setFont(u8g2_font_4x6_tr);
  char s[20];
  snprintf(s, sizeof s, "%lu within 5mi", (unsigned long)app.withinCount());
  center(d, 124, s);
}

static void listRow(Canvas& d, size_t i, bool flag, const char* label, int8_t rssi, int channel) {
  int y = 13 + (int)i * 14;
  if (flag) {
    d.drawBox(0, y - 1, W, 13);
    d.setDrawColor(0);
  }
  char name[14];
  snprintf(name, sizeof name, "%s%s", flag ? "!" : "", label);
  d.setFont(u8g2_font_5x7_tr);
  d.drawStr(1, y + 6, name);
  signalBar(d, y + 8, rssi);
  if (channel > 0) {
    char ch[4];
    snprintf(ch, sizeof ch, "%d", channel);
    d.setFont(u8g2_font_4x6_tr);
    d.drawStr(54, y + 11, ch);
  }
  d.setDrawColor(1);
}

static void wifiList(Canvas& d, const ApInfo* aps, size_t n) {
  d.setFont(u8g2_font_5x7_tr);
  if (!n) center(d, 60, "listening...");
  for (size_t i = 0; i < n && i < 7; i++)
    listRow(d, i, aps[i].flock, aps[i].ssid[0] ? aps[i].ssid : "(hidden)", aps[i].rssi, aps[i].channel);
}

static void bleList(Canvas& d, const BleSeen* ble, size_t n) {
  d.setFont(u8g2_font_5x7_tr);
  if (!n) center(d, 60, "listening...");
  for (size_t i = 0; i < n && i < 7; i++) {
    bool flag = ble[i].kind != BleKind::Other && ble[i].kind != BleKind::FindMyNearOwner;
    listRow(d, i, flag, ble[i].label, ble[i].rssi, 0);
  }
}

static void drawOverlay(Canvas& d, const AppLogic& app, Overlay o, uint32_t camCount, size_t markCount) {
  char s[24];
  switch (o) {
    case Overlay::WaitingGps:
      d.drawCircle(32, 45, 14);
      d.drawDisc(32, 45, 3);
      d.setFont(u8g2_font_6x10_tr);
      center(d, 78, "WAITING");
      center(d, 89, "FOR GPS");
      d.setFont(u8g2_font_4x6_tr);
      center(d, 104, "go outside");
      snprintf(s, sizeof s, "%lu cams", (unsigned long)camCount);
      center(d, 122, s);
      break;
    case Overlay::NoCamData:
      d.setFont(u8g2_font_6x10_tr);
      center(d, 55, "NO CAMERA");
      center(d, 66, "DATA");
      d.setFont(u8g2_font_4x6_tr);
      center(d, 84, "flash camdb.bin");
      center(d, 92, "tap: WIFI / BT");
      break;
    case Overlay::CameraAhead: {
      CamRow r = app.aheadRow();
      d.drawBox(0, 0, W, H);
      d.setDrawColor(0);
      d.setFont(u8g2_font_7x13B_tr);
      center(d, 18, "CAMERA");
      center(d, 30, "AHEAD");
      if (r.arrowDeg >= 0) arrow(d, 32, 54, 16, r.arrowDeg);
      else center(d, 58, r.compass);
      center(d, 90, r.dist);
      d.setFont(u8g2_font_5x7_tr);
      center(d, 102, r.brand);
      break;
    }
    case Overlay::TrackerFollowing:
      d.drawBox(0, 0, W, H);
      d.setDrawColor(0);
      d.setFont(u8g2_font_7x13B_tr);
      center(d, 18, "TRACKER");
      center(d, 31, "FOLLOWS");
      center(d, 44, "YOU");
      d.setFont(u8g2_font_6x10_tr);
      center(d, 66, bleLabel(app.trackerAlert().kind));
      snprintf(s, sizeof s, "%d dBm", app.trackerAlert().rssi);
      d.setFont(u8g2_font_5x7_tr);
      center(d, 80, s);
      d.setFont(u8g2_font_4x6_tr);
      center(d, 104, "tap: dismiss");
      center(d, 112, "hold 5s: mine");
      break;
    case Overlay::NewFlock:
      for (int r = 12; r <= 24; r += 6) d.drawUpperArc(32, 40, r);
      d.drawDisc(32, 40, 3);
      d.setFont(u8g2_font_6x10_tr);
      center(d, 60, "HEARD A");
      center(d, 71, "NEW FLOCK");
      d.setFont(u8g2_font_4x6_tr);
      center(d, 82, "not on map");
      d.drawHLine(0, 92, W);
      center(d, 106, "hold button");
      center(d, 114, "5 sec to");
      center(d, 122, "mark it");
      break;
    case Overlay::Marking:
      d.setFont(u8g2_font_6x10_tr);
      center(d, 50, "MARKING");
      d.drawFrame(6, 60, 52, 8);
      d.drawBox(8, 62, (int)lroundf(48 * app.holdFraction()), 4);
      d.setFont(u8g2_font_4x6_tr);
      center(d, 84, "keep holding");
      break;
    case Overlay::Marked: {
      const Fix& f = app.fix();
      for (int k = 0; k < 2; k++) {
        d.drawLine(20, 48 + k, 28, 56 + k);
        d.drawLine(28, 56 + k, 44, 38 + k);
      }
      d.setFont(u8g2_font_7x13B_tr);
      center(d, 76, "MARKED");
      d.setFont(u8g2_font_4x6_tr);
      snprintf(s, sizeof s, "%.4f %c", fabs(f.lat), f.lat >= 0 ? 'N' : 'S');
      center(d, 88, s);
      snprintf(s, sizeof s, "%.4f %c", fabs(f.lon), f.lon >= 0 ? 'E' : 'W');
      center(d, 96, s);
      snprintf(s, sizeof s, "#%u to review", (unsigned)markCount);
      center(d, 116, s);
      break;
    }
    case Overlay::MarkRefused:
      d.setFont(u8g2_font_6x10_tr);
      if (app.markResult() == AddResult::NoFix) {
        center(d, 58, "NO GPS FIX");
        d.setFont(u8g2_font_4x6_tr);
        center(d, 74, "not marked");
      } else {
        center(d, 58, "SAVE FAILED");
      }
      break;
    case Overlay::MarksFull:
      d.setFont(u8g2_font_6x10_tr);
      center(d, 58, "MARKS FULL");
      d.setFont(u8g2_font_4x6_tr);
      center(d, 74, "review in app");
      break;
    case Overlay::GlassesToast:
      d.setDrawColor(0);
      d.drawBox(0, 40, W, 40);  // clear a band over the list
      d.setDrawColor(1);
      d.drawBox(2, 42, W - 4, 36);
      d.setDrawColor(0);
      d.setFont(u8g2_font_6x10_tr);
      center(d, 56, app.glassesKind() == BleKind::GlassesSnap ? "SNAP" : "META");
      center(d, 67, "GLASSES");
      d.setFont(u8g2_font_4x6_tr);
      center(d, 75, "nearby");
      break;
    case Overlay::Toast:
      d.setDrawColor(0);
      d.drawBox(0, 44, W, 30);
      d.setDrawColor(1);
      d.drawBox(2, 46, W - 4, 26);
      d.setDrawColor(0);
      d.setFont(u8g2_font_5x7_tr);
      center(d, 62, app.toastText());
      break;
    case Overlay::None:
      break;
  }
  d.setDrawColor(1);
}

void drawFrame(Canvas& d, const AppLogic& app, const ApInfo* aps, size_t nAps, const BleSeen* ble,
               size_t nBle, uint8_t sats, float vbat, uint32_t camCount, size_t markCount) {
  Overlay o = app.overlay();
  bool fullScreen = o == Overlay::CameraAhead || o == Overlay::TrackerFollowing;
  const char* tag = modeTag(app.mode());
  if (!fullScreen) topStrip(d, tag, sats, vbat);
  if (o == Overlay::None || o == Overlay::GlassesToast || o == Overlay::Toast) {
    if (app.mode() == Mode::Cam) camList(d, app);
    else if (app.mode() == Mode::Wifi) wifiList(d, aps, nAps);
    else bleList(d, ble, nBle);
  }
  if (o != Overlay::None) drawOverlay(d, app, o, camCount, markCount);
}
