#include <unity.h>
#include <stdio.h>
#include <string.h>
#include <vector>
#include "AppLogic.h"

struct MemIO : MarkIO {
  std::vector<uint8_t> file;
  bool load(uint8_t* buf, size_t cap, size_t* len) override {
    if (file.empty() || file.size() > cap) return false;
    memcpy(buf, file.data(), file.size());
    *len = file.size();
    return true;
  }
  bool save(const uint8_t* buf, size_t len) override {
    file.assign(buf, buf + len);
    return true;
  }
};

static std::vector<uint8_t> img;
static CamDb db;
static MemIO io;
static MarkStore* marks;
static AppLogic* app;
static uint8_t ignoredKey[8];
static int ignoredCalls;

// Fixture cameras (test/fixtures/small.bin): a Flock 161 m north of P, another 0.6 mi north,
// a Vigilant 1.2 km west. Far is a spot with no cameras.
static const double kLat = 33.6891, kLon = -78.8867;
static const double kCamALat = 33.690547;
static const double kFarLat = 35.0, kFarLon = -80.0;

static void onIgnore(const uint8_t key[8]) {
  memcpy(ignoredKey, key, 8);
  ignoredCalls++;
}

void setUp() {
  FILE* f = fopen("test/fixtures/small.bin", "rb");
  TEST_ASSERT_NOT_NULL(f);
  img.clear();
  uint8_t b[4096];
  size_t n;
  while ((n = fread(b, 1, sizeof b, f)) > 0) img.insert(img.end(), b, b + n);
  fclose(f);
  db = CamDb();
  db.open(img.data(), img.size());
  io.file.clear();
  delete marks;
  marks = new MarkStore(io);
  marks->begin();
  delete app;
  app = new AppLogic(db, *marks);
  app->setIgnoreCallback(onIgnore);
  ignoredCalls = 0;
}
void tearDown() {}

static void at(double lat, double lon, uint32_t now, float speed = 0, float course = 0) {
  Fix f = {true, lat, lon, speed, course, 9, 1793500000};
  app->onFix(f, now);
  app->tick(now);
}
static void press(uint32_t now) { app->onButton(BtnEvent::ShortPress, 0, now); app->tick(now); }
static void hold(uint32_t now) { app->onButton(BtnEvent::HoldDone, 1, now); app->tick(now); }
#define OVERLAY(o) TEST_ASSERT_EQUAL_INT((int)Overlay::o, (int)app->overlay())

void test_mode_cycles_cam_wifi_bt() {
  at(kFarLat, kFarLon, 1000);
  press(1100);
  TEST_ASSERT_EQUAL_INT((int)Mode::Wifi, (int)app->mode());
  press(1200);
  TEST_ASSERT_EQUAL_INT((int)Mode::Phone, (int)app->mode());
}
void test_short_press_dismisses_overlay_not_mode() {
  at(kLat, kLon, 1000);
  OVERLAY(CameraAhead);
  press(1500);
  OVERLAY(None);
  TEST_ASSERT_EQUAL_INT((int)Mode::Cam, (int)app->mode());
}
void test_camera_ahead_times_out() {
  at(kLat, kLon, 1000);
  app->tick(6999);
  OVERLAY(CameraAhead);
  at(kLat, kLon, 7001);
  OVERLAY(None);
}
void test_camera_ahead_once_then_rearms() {
  at(kLat, kLon, 1000);
  OVERLAY(CameraAhead);
  press(1100);
  at(kLat, kLon, 2000);
  OVERLAY(None);
  at(kLat - 0.0145, kLon, 60000);  // a mile south: re-arms
  OVERLAY(None);
  at(kLat, kLon, 120000);
  OVERLAY(CameraAhead);
}
void test_arrow_relative_when_moving() {
  CamRow r[4];
  at(kLat, kLon, 1000, 10, 0);
  TEST_ASSERT_TRUE(app->camRows(r, 4) >= 1);
  TEST_ASSERT_INT_WITHIN(1, 0, r[0].arrowDeg);
  at(kLat, kLon, 2000, 10, 90);
  app->camRows(r, 4);
  TEST_ASSERT_INT_WITHIN(1, 270, r[0].arrowDeg);
  TEST_ASSERT_EQUAL_STRING("FLOCK", r[0].brand);
  TEST_ASSERT_EQUAL_STRING("550ft", r[0].dist);
}
void test_compass_when_stopped() {
  CamRow r[4];
  at(kLat, kLon, 1000, 0, 0);
  app->camRows(r, 4);
  TEST_ASSERT_EQUAL_INT(-1, r[0].arrowDeg);
  TEST_ASSERT_EQUAL_STRING("N", r[0].compass);
}
void test_within_count() {
  at(kLat, kLon, 1000);
  TEST_ASSERT_EQUAL_UINT32(4, app->withinCount());
}
void test_no_fix_waiting_gps() {
  app->tick(1000);
  OVERLAY(WaitingGps);
  CamRow r[4];
  TEST_ASSERT_EQUAL_UINT(0, app->camRows(r, 4));
}
void test_no_fix_stale_fix_no_alert() {
  Fix f = {true, kCamALat, kLon, 10, 0, 9, 1793500000};
  app->onFix(f, 0);
  app->tick(6000);  // 6 s with no new fix: position is stale
  OVERLAY(WaitingGps);
}
void test_no_fix_mark_refused() {
  app->tick(1000);
  hold(1000);
  OVERLAY(MarkRefused);
  TEST_ASSERT_EQUAL_INT((int)AddResult::NoFix, (int)app->markResult());
  TEST_ASSERT_EQUAL_UINT(0, marks->count());
}
void test_mark_saved() {
  at(kFarLat, kFarLon, 1000);
  hold(1100);
  OVERLAY(Marked);
  TEST_ASSERT_EQUAL_UINT(1, marks->count());
  at(kFarLat, kFarLon, 5200);
  OVERLAY(None);
}
void test_marking_overlay_while_holding() {
  at(kFarLat, kFarLon, 1000);
  app->onButton(BtnEvent::HoldProgress, 0.4f, 1100);
  app->tick(1100);
  OVERLAY(Marking);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.4f, app->holdFraction());
  app->tick(1500);  // released: no more progress events
  OVERLAY(None);
}
void test_marks_full_overlay() {
  for (int i = 0; i < 500; i++) marks->add(true, 40.0 + i * 0.001, -100.0, 1, 0);
  at(kFarLat, kFarLon, 1000);
  hold(1100);
  OVERLAY(MarksFull);
}
void test_new_flock_suppressed_near_known_camera() {
  at(kCamALat, kLon, 1000);
  press(1100);  // dismiss the camera alert
  app->onFlockHeard(1200);
  app->tick(1200);
  OVERLAY(None);
}
void test_new_flock_shown_off_map() {
  at(kFarLat, kFarLon, 1000);
  app->onFlockHeard(1100);
  app->tick(1100);
  OVERLAY(NewFlock);
}
void test_new_flock_suppressed_near_own_mark() {
  at(kFarLat, kFarLon, 1000);
  hold(1100);
  at(kFarLat, kFarLon, 6000);
  app->onFlockHeard(6100);
  app->tick(6100);
  OVERLAY(None);
}
void test_new_flock_rate_limited() {
  at(kFarLat, kFarLon, 1000);
  app->onFlockHeard(1000);
  press(1100);
  at(kFarLat, kFarLon, 61000);
  app->onFlockHeard(61000);
  app->tick(61000);
  OVERLAY(None);
  at(kFarLat, kFarLon, 122000);
  app->onFlockHeard(122000);
  app->tick(122000);
  OVERLAY(NewFlock);
}
void test_new_flock_needs_fix() {
  app->onFlockHeard(1000);
  app->tick(1000);
  OVERLAY(WaitingGps);
}
void test_tracker_overlay_priority_over_camera() {
  at(kLat, kLon, 1000);
  FollowAlert a = {{1, 2, 3, 4, 5, 6, 7, 8}, BleKind::FindMySeparated, -60};
  app->onFollow(a, 1100);
  app->tick(1100);
  OVERLAY(TrackerFollowing);
}
void test_tracker_stays_until_dismissed() {
  FollowAlert a = {{1, 2, 3, 4, 5, 6, 7, 8}, BleKind::Tile, -60};
  at(kFarLat, kFarLon, 1000);
  app->onFollow(a, 1000);
  at(kFarLat, kFarLon, 600000);
  OVERLAY(TrackerFollowing);
  press(600100);
  OVERLAY(None);
}
void test_hold_during_tracker_ignores_it() {
  FollowAlert a = {{9, 9, 9, 9, 9, 9, 9, 9}, BleKind::FindMySeparated, -60};
  at(kFarLat, kFarLon, 1000);
  app->onFollow(a, 1000);
  hold(2000);
  TEST_ASSERT_EQUAL_INT(1, ignoredCalls);
  TEST_ASSERT_EQUAL_MEMORY(a.key, ignoredKey, 8);
  TEST_ASSERT_EQUAL_UINT(0, marks->count());
  OVERLAY(None);
}
void test_glasses_toast_once_per_10min() {
  const uint8_t k[8] = {7, 7, 7, 7, 7, 7, 7, 7};
  at(kFarLat, kFarLon, 1000);
  app->onGlasses(BleKind::GlassesMeta, -50, k, 1000);
  app->tick(1000);
  OVERLAY(GlassesToast);
  at(kFarLat, kFarLon, 4500);
  OVERLAY(None);
  at(kFarLat, kFarLon, 60000);
  app->onGlasses(BleKind::GlassesMeta, -50, k, 60000);
  app->tick(60000);
  OVERLAY(None);
  at(kFarLat, kFarLon, 661000);
  app->onGlasses(BleKind::GlassesMeta, -50, k, 661000);
  app->tick(661000);
  OVERLAY(GlassesToast);
}
void test_glasses_weak_signal_no_toast() {
  const uint8_t k[8] = {6, 6, 6, 6, 6, 6, 6, 6};
  at(kFarLat, kFarLon, 1000);
  app->onGlasses(BleKind::GlassesMeta, -80, k, 1000);
  app->tick(1000);
  OVERLAY(None);
}
void test_no_cam_data_still_allows_wifi_bt_modes() {
  CamDb empty;
  AppLogic a2(empty, *marks);
  Fix f = {true, kLat, kLon, 0, 0, 9, 1};
  a2.onFix(f, 1000);
  a2.tick(1000);
  TEST_ASSERT_EQUAL_INT((int)Overlay::NoCamData, (int)a2.overlay());
  a2.onButton(BtnEvent::ShortPress, 0, 1100);
  a2.tick(1100);
  TEST_ASSERT_EQUAL_INT((int)Mode::Wifi, (int)a2.mode());
  TEST_ASSERT_EQUAL_INT((int)Overlay::None, (int)a2.overlay());
}
void test_waiting_gps_only_in_cam_mode() {
  app->tick(1000);
  press(1100);
  OVERLAY(None);
}
void test_own_mark_listed_unconfirmed_without_alert() {
  at(kFarLat, kFarLon, 1000);
  hold(1100);
  at(kFarLat + 0.0009, kFarLon, 6000);  // 100 m from the mark
  OVERLAY(None);
  CamRow r[4];
  TEST_ASSERT_EQUAL_UINT(1, app->camRows(r, 4));
  TEST_ASSERT_TRUE(r[0].unconfirmed);
  TEST_ASSERT_EQUAL_STRING("UNCONF.", r[0].brand);
  TEST_ASSERT_EQUAL_UINT32(0, app->withinCount());
}

void test_mode_cycles_all_16_and_wraps() {
  at(kFarLat, kFarLon, 1000);
  const char* expect[] = {"WIFI", "PHONE", "SPYCAM", "CHAN", "ATTACK", "BT", "TAGS", "BODYCAM",
                          "DRONE", "MESH", "LORAWAN", "ZWAVE", "WX", "900", "SKY", "CAM"};
  for (int i = 0; i < 16; i++) {
    press(2000 + i * 10);
    TEST_ASSERT_EQUAL_STRING(expect[i], modeTag(app->mode()));
  }
}
void test_sub_ghz_modes_flagged() {
  TEST_ASSERT_TRUE(isSubGhz(Mode::Mesh));
  TEST_ASSERT_TRUE(isSubGhz(Mode::Sweep));
  TEST_ASSERT_TRUE(isSubGhz(Mode::ZWave));
  TEST_ASSERT_FALSE(isSubGhz(Mode::Drone));
  TEST_ASSERT_FALSE(isSubGhz(Mode::Sky));
  TEST_ASSERT_FALSE(isSubGhz(Mode::Cam));
}
void test_toast_shows_3s() {
  at(kFarLat, kFarLon, 1000);
  app->onToast("DEAUTH FLOOD", 1000);
  app->tick(1000);
  OVERLAY(Toast);
  TEST_ASSERT_EQUAL_STRING("DEAUTH FLOOD", app->toastText());
  at(kFarLat, kFarLon, 4100);
  OVERLAY(None);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_mode_cycles_cam_wifi_bt);
  RUN_TEST(test_short_press_dismisses_overlay_not_mode);
  RUN_TEST(test_camera_ahead_times_out);
  RUN_TEST(test_camera_ahead_once_then_rearms);
  RUN_TEST(test_arrow_relative_when_moving);
  RUN_TEST(test_compass_when_stopped);
  RUN_TEST(test_within_count);
  RUN_TEST(test_no_fix_waiting_gps);
  RUN_TEST(test_no_fix_stale_fix_no_alert);
  RUN_TEST(test_no_fix_mark_refused);
  RUN_TEST(test_mark_saved);
  RUN_TEST(test_marking_overlay_while_holding);
  RUN_TEST(test_marks_full_overlay);
  RUN_TEST(test_new_flock_suppressed_near_known_camera);
  RUN_TEST(test_new_flock_shown_off_map);
  RUN_TEST(test_new_flock_suppressed_near_own_mark);
  RUN_TEST(test_new_flock_rate_limited);
  RUN_TEST(test_new_flock_needs_fix);
  RUN_TEST(test_tracker_overlay_priority_over_camera);
  RUN_TEST(test_tracker_stays_until_dismissed);
  RUN_TEST(test_hold_during_tracker_ignores_it);
  RUN_TEST(test_glasses_toast_once_per_10min);
  RUN_TEST(test_glasses_weak_signal_no_toast);
  RUN_TEST(test_no_cam_data_still_allows_wifi_bt_modes);
  RUN_TEST(test_waiting_gps_only_in_cam_mode);
  RUN_TEST(test_own_mark_listed_unconfirmed_without_alert);
  RUN_TEST(test_mode_cycles_all_16_and_wraps);
  RUN_TEST(test_sub_ghz_modes_flagged);
  RUN_TEST(test_toast_shows_3s);
  return UNITY_END();
}
