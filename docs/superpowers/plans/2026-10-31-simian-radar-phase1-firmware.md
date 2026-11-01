# Simian Radar Phase 1 (standalone device firmware) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Standalone firmware for Rev's Heltec V4 that reads GPS, shows nearby ALPR cameras from an offline database, scans WiFi and BLE, raises camera / tracker-following / glasses alerts, and marks new cameras with a 5 s button hold, plus the PC tool that builds the camera database.

**Architecture:** PlatformIO project, Arduino on ESP32-S3. All decision logic lives in small pure-C++ libraries under `lib/` that compile on the host and are unit-tested with `pio test -e native` (Unity). Thin hardware adapters (GPS, WiFi, BLE, OLED, LittleFS, flash partition) live in `src/hw/` and are verified on the board. The camera database is a binary file built by a Python tool and written to its own flash partition, then memory-mapped at runtime.

**Tech Stack:** PlatformIO `platformio/espressif32@6.11.0` (same as MeshCore), Arduino framework, U8g2 (SSD1306 128x64, rotated `U8G2_R1` for portrait), TinyGPSPlus, NimBLE-Arduino, ESP-IDF `esp_partition_mmap`, LittleFS. Python 3.12 + pytest + requests for the database tool. Unity for native tests.

**Spec:** `F:\simian-radar\docs\2026-10-31-simian-radar-design.md` (read it before starting any task).

**Out of scope for this plan:** MeshCore mesh and chat (Phase 2 plan), Android app (Phase 3 plan). This firmware must not transmit on LoRa at all.

## Global Constraints

- Repo root: `F:\simian-radar`. Git repo created in Task 1. Commit messages must NOT contain any Claude / Anthropic / AI attribution or `Co-Authored-By` trailer (Rev's global rule; a global commit-msg hook also strips them, do not disable it).
- Board: plain Heltec V4, ESP32-S3R2, 16 MB flash, 2 MB PSRAM. Device on `COM11` (USB VID 303A PID 1001, native USB, no driver).
- Pins (from MeshCore `variants/heltec_v4`): OLED I2C `SDA=17`, `SCL=18`, `OLED_RST=21`; `PIN_VEXT_EN=36` **active HIGH** (powers OLED/GPS rail); user button `GPIO0` (active LOW, internal pullup); LED `GPIO35`; GPS `Serial1` RX `38`, TX `39`, 9600 baud, `PIN_GPS_EN=34` active LOW, `PIN_GPS_RESET=42` active LOW; battery ADC `GPIO1` with `PIN_ADC_CTRL=37`. Do not use the default `SDA=3/SCL=4` from `pins_arduino.h` (those are TFT pins).
- LoRa radio: leave untouched (never transmit in Phase 1).
- Display: 64 px wide x 128 px tall after rotation. One color.
- Distances: alert radius **1000 ft (304.8 m)**, "N within 5mi" radius **8046.7 m**, new-Flock "not on map" radius **500 ft (152.4 m)**, arrow vs compass speed threshold **2 mph (0.894 m/s)**, glasses toast threshold RSSI **-60 dBm** (~30 ft). Each is one named `constexpr` in `lib/config/Config.h`.
- Follow alert: tracker seen **>= 10 min** AND user moved **>= 0.5 mi (804.67 m)** since first seen AND seen within the last **2 min**.
- Button: short press (< 1 s release) cycles CAM → WIFI → BT → CAM. Hold **5000 ms** = mark. A mark requires a valid GPS fix.
- Listen only: no WiFi association or connect calls, no packet payload capture, read only SSID/BSSID/RSSI/channel and BLE advertisement fields. No transmitting at anything.
- Boot screen must show `(c) OpenStreetMap contributors` (ODbL attribution).
- Partition table (fixed, 16 MB): `nvs 0x9000 0x5000`, `otadata 0xE000 0x2000`, `app0 0x10000 0x300000`, `app1 0x310000 0x300000`, `littlefs 0x610000 0x100000`, `camdb 0x710000 0x8F0000` (data, subtype `0x40`).
- Native tests need a host `g++`. None is installed on this PC today. Task 1 installs WinLibs GCC (`winget install BrechtSanders.WinLibs.POSIX.UCRT`) **after asking Rev**.
- No em dashes in user-facing text (screens, README).

## Review Focus

1. **No GPS fix, or fix lost mid-drive (tunnel, garage):** device shows WAITING FOR GPS instead of stale arrows, never fires camera alerts from a stale position, refuses a mark with "NO GPS FIX". Tests in Task 8 (`test_no_fix_*`).
2. **Empty, missing or corrupt camera database** (fresh unit, failed flash): reader rejects bad magic/version/size, UI shows "NO CAMERA DATA" and the WIFI/BT modes still work. Tests in Task 3 (`test_open_rejects_*`).
3. **Camera near a grid cell edge** (and the 3x3 neighbourhood at high latitude, e.g. Alaska): it must still be found. Test in Task 3 (`test_finds_camera_across_cell_edge`, `test_alaska_latitude`).
4. **Busy BLE environments (traffic jam, parking lot, mall):** dozens of Find My devices must not overflow memory or crash; near-owner AirTags (owner present) must never trigger a follow alert. Tests in Task 5 (`test_table_caps_at_64`, `test_near_owner_never_alerts`).
5. **Marks store full or flash write failure:** 500-mark cap shows "MARKS FULL" instead of silently dropping; survives reboot. Tests in Task 7 (`test_store_full`, `test_persist_roundtrip`).

---

### Task 1: Project scaffold, toolchain, boot screen on the real board

**Files:**
- Create: `platformio.ini`, `partitions.csv`, `boards/heltec_v4.json`, `variants/heltec_v4/pins_arduino.h`, `THIRD_PARTY.md`, `.gitignore`, `lib/config/Config.h`, `src/main.cpp`, `test/test_smoke/test_main.cpp`

**Interfaces:**
- Produces: envs `heltec_v4` (device) and `native` (host tests); `Config.h` constants named `kAlertRadiusM`, `kWithinRadiusM`, `kNewFlockRadiusM`, `kMovingSpeedMps`, `kGlassesRssi`, `kFollowMinMs`, `kFollowMinTravelM`, `kFollowRecentMs`, `kHoldMarkMs`, `kShortPressMaxMs`, `kMaxMarks = 500`, `kMaxBleTracked = 64`, and pin constants `kPinSda=17, kPinScl=18, kPinOledRst=21, kPinVext=36, kPinButton=0, kPinLed=35, kPinGpsRx=38, kPinGpsTx=39, kPinGpsEn=34, kPinGpsReset=42`.

- [ ] **Step 1:** Ask Rev to approve installing WinLibs GCC, then run `winget install BrechtSanders.WinLibs.POSIX.UCRT`. Verify: `g++ --version` prints a version.
- [ ] **Step 2:** `git init` in `F:\simian-radar`. `.gitignore`: `.pio/`, `.vscode/`, `__pycache__/`, `*.bin` except `test/fixtures/*.bin`, `.superpowers/`.
- [ ] **Step 3:** Copy `boards/heltec_v4.json` and `variants/heltec_v4/pins_arduino.h` verbatim from `meshcore-dev/MeshCore` main (MIT). Record source URL, commit hash and MIT notice in `THIRD_PARTY.md`. In the board JSON set `"partitions": "partitions.csv"`.
- [ ] **Step 4:** `platformio.ini`: `[env:heltec_v4]` platform `platformio/espressif32@6.11.0`, `board = heltec_v4`, `board_build.variants_dir = variants`, `board_build.filesystem = littlefs`, `upload_port = COM11`, `monitor_speed = 115200`, `lib_deps = olikraus/U8g2, mikalhart/TinyGPSPlus, h2zero/NimBLE-Arduino@^1.4`. `[env:native]` platform `native`, `test_framework = unity`, `build_flags = -std=gnu++17`, `lib_ldf_mode = deep+`, `test_build_src = no`.
- [ ] **Step 5:** Write `partitions.csv` with exactly the table in Global Constraints.
- [ ] **Step 6:** Smoke test `test/test_smoke/test_main.cpp`: `TEST_ASSERT_EQUAL_FLOAT(304.8f, kAlertRadiusM)`. Run `pio test -e native`. Expected: 1 test PASS.
- [ ] **Step 7:** `src/main.cpp`: drive `kPinVext` HIGH, delay 50 ms, `Wire.begin(kPinSda, kPinScl)`, U8g2 `U8G2_SSD1306_128X64_NONAME_F_HW_I2C(U8G2_R1, kPinOledRst)`, draw "SIMIAN" / "RADAR" / "(c) OpenStreetMap" / "contributors" in portrait. Blink LED once.
- [ ] **Step 8:** `pio run -e heltec_v4 -t upload`. Expected: upload succeeds on COM11; Rev confirms the portrait boot screen is upright in his case (if upside down, switch to `U8G2_R3` and note it in `Config.h`).
- [ ] **Step 9:** Commit `chore: scaffold firmware project, boot screen`.

### Task 2: Geo math library

**Files:** Create `lib/geo/Geo.h`, `lib/geo/Geo.cpp`, `test/test_geo/test_geo.cpp`

**Interfaces:**
- Produces:
  - `double haversineM(double lat1, double lon1, double lat2, double lon2)`
  - `double bearingDeg(double lat1, double lon1, double lat2, double lon2)` (0..360, 0 = north)
  - `int relativeArrowDeg(double bearing, double courseDeg)` (0..359, 0 = straight ahead)
  - `const char* compass8(double bearing)` ("N","NE","E","SE","S","SW","W","NW")
  - `void formatDistance(double meters, char* out, size_t n)` (`"500ft"` under 0.2 mi, rounded to 50 ft; else `"0.4mi"` one decimal; `"12mi"` at 10+ mi)

- [ ] **Step 1:** Tests: Myrtle Beach (33.6891,-78.8867) to Calabash (33.893,-78.465) distance within 1% of 45 km; bearing due east from (0,0) to (0,1) == 90 +/- 0.5; `relativeArrowDeg(10, 350) == 20`; `relativeArrowDeg(350, 10) == 340`; `compass8(22.4)=="N"`, `compass8(22.6)=="NE"`, `compass8(359)=="N"`; `formatDistance(152.4)` -> `"500ft"`; `formatDistance(643.7)` -> `"0.4mi"`; `formatDistance(19312)` -> `"12mi"`.
- [ ] **Step 2:** `pio test -e native -f test_geo`. Expected: FAIL (undefined).
- [ ] **Step 3:** Implement. Spherical earth radius 6371008.8 m.
- [ ] **Step 4:** `pio test -e native -f test_geo`. Expected: PASS.
- [ ] **Step 5:** Commit `feat: geo math`.

### Task 3: Camera database format, Python builder and C++ reader

**Files:**
- Create: `tools/camdb/build_camdb.py`, `tools/camdb/test_build_camdb.py`, `tools/camdb/requirements.txt` (`requests`, `pytest`), `lib/camdb/CamDb.h`, `lib/camdb/CamDb.cpp`, `test/test_camdb/test_camdb.cpp`, `test/fixtures/small.bin` (generated by the tool, committed)

**Format (decided here, both sides must match):** little-endian.
- Header 32 bytes: `char magic[4]="SRDB"`, `u16 version=1`, `u16 cell_q=4` (cells per degree, i.e. 0.25 deg), `i16 lat0` (floor of min lat, degrees), `i16 lon0` (floor of min lon), `u16 rows`, `u16 cols`, `u32 count`, `u32 built_unix`, `u8 reserved[8]`.
- Index: `(rows*cols + 1)` x `u32` record start offsets; cell `(r,c)` covers records `[idx[r*cols+c], idx[r*cols+c+1])`. Row from latitude, col from longitude: `r = floor((lat-lat0)*cell_q)`.
- Records, sorted by cell: 12 bytes each: `i32 lat_e7`, `i32 lon_e7`, `u16 direction` (0..359, `0xFFFF` unknown), `u8 brand`, `u8 flags` (0).
- Brand codes: `0` unknown/other ("ALPR"), `1` Flock ("FLOCK"), `2` Motorola/Vigilant ("VIGILANT"), `3` Genetec ("GENETEC"), `4` Axon ("AXON"), `5` Leonardo/ELSAG ("ELSAG"), `6` Rekor ("REKOR"), `7` Verkada ("VERKADA"), `8` Neology ("NEOLOGY"). Map from the OSM `manufacturer` tag case-insensitively by substring.

**Interfaces:**
- Python produces: `pack(cameras: list[dict]) -> bytes` (dict keys `lat`, `lon`, `direction: int|None`, `manufacturer: str|None`), `brand_code(manufacturer: str|None) -> int`, CLI `python build_camdb.py --out camdb.bin [--from-json overpass.json]` (without `--from-json` it downloads US `surveillance:type=ALPR` nodes via Overpass in 5 x 5 degree tiles with `User-Agent: SimianRadar/0.1`, retrying a tile up to 3 times; one whole-US query timed out on 2026-10-31). Prints the camera count.
- C++ produces:
  - `struct CamRec { int32_t latE7; int32_t lonE7; uint16_t dir; uint8_t brand; uint8_t flags; };`
  - `struct NearCam { CamRec rec; float distM; float bearingDeg; };`
  - `class CamDb { bool open(const uint8_t* data, size_t len); bool isOpen() const; uint32_t count() const; size_t nearest(double lat, double lon, float maxM, NearCam* out, size_t maxOut, uint32_t* withinCount) const; };` (results sorted by distance; `withinCount` = all within `maxM`; searches the 3x3 cells around the position)
  - `const char* brandLabel(uint8_t brand)`

- [ ] **Step 1:** Python tests: `brand_code("Flock Safety")==1`, `brand_code(None)==0`; `pack` of 3 cameras gives `len == 32 + (rows*cols+1)*4 + 36`, magic `b"SRDB"`, records sorted by cell; the index sums to count. Run `pytest tools/camdb -q`. Expected FAIL, then implement, then PASS.
- [ ] **Step 2:** Generate fixture: `python tools/camdb/build_camdb.py --from-json tools/camdb/fixture.json --out test/fixtures/small.bin` where `fixture.json` holds 6 hand-made Overpass-style nodes: two near Myrtle Beach 0.1 mi and 0.6 mi away from (33.6891,-78.8867), one exactly 0.001 deg across a 0.25 deg cell edge from a query point at (33.7499,-78.8867), one at 6 mi, one in Anchorage (61.2181,-149.9003), one Vigilant.
- [ ] **Step 3:** C++ tests (load `test/fixtures/small.bin` with fopen in native): `test_open_ok` (count 6); `test_open_rejects_bad_magic`, `test_open_rejects_short_buffer`, `test_open_rejects_wrong_version`, `test_open_rejects_index_past_end`; `test_nearest_sorted` (first result about 161 m, brand FLOCK); `test_within_count_5mi` (excludes the 6 mi camera); `test_finds_camera_across_cell_edge`; `test_alaska_latitude` (query Anchorage, finds 1); `test_empty_area_returns_zero`.
- [ ] **Step 4:** `pio test -e native -f test_camdb`. Expected FAIL, implement `CamDb` (bounds-check every offset against `len`), then PASS.
- [ ] **Step 5:** Real data run: `python tools/camdb/build_camdb.py --out camdb.bin`. Expected: count printed (record it), file under 9,371,648 bytes (0x8F0000). Note the count in the commit message.
- [ ] **Step 6:** Commit `feat: camera database builder and reader`.

### Task 4: Signature classifier (WiFi + BLE)

**Files:** Create `lib/sig/Signatures.h`, `lib/sig/Signatures.cpp`, `test/test_sig/test_sig.cpp`

**Interfaces:**
- Produces:
  - `enum class BleKind { Other, GlassesMeta, GlassesSnap, FindMySeparated, FindMyNearOwner, Tile, SmartTag, Flock };`
  - `struct BleAdv { uint8_t addr[6]; int8_t rssi; const uint8_t* mfg; size_t mfgLen; const uint16_t* svc16; size_t svc16Count; const char* name; };`
  - `BleKind classifyBle(const BleAdv& a)`
  - `bool trackerKey(const BleAdv& a, BleKind k, uint8_t out[8])` (stable id: for Find My separated, 8 bytes of the public-key fragment from the payload; otherwise the address)
  - `bool isFlockWifi(const char* ssid, const uint8_t bssid[6])`
  - `const char* bleLabel(BleKind k)` ("META GLASS", "SNAP GLASS", "AIRTAG", "AIRTAG", "TILE", "SMARTTAG", "FLOCK", "")
- Rules: glasses by manufacturer company ID (first two mfg bytes, little-endian): `0x01AB`, `0x058E`, `0x0D53` -> GlassesMeta, `0x03C2` -> GlassesSnap. Find My: company `0x004C`, type byte `0x12`; length byte `0x19` -> Separated, `0x02` -> NearOwner. Tile: 16-bit service `0xFEED` or `0xFEEC`. SmartTag: service `0xFD5A`. Flock: from the signature table.

- [ ] **Step 1:** Collect Flock WiFi SSID patterns, OUIs and BLE names from `colonelpanichacks/flock-you` and `simeononsecurity/flock-you-esp32`. Read each repo's licence first; copy only the published signature facts (not code) into a `static const` table at the top of `Signatures.cpp` with a comment citing the source repo and date.
- [ ] **Step 2:** Tests: one per rule above using hand-built adverts (Meta `{0xAB,0x01,...}` -> GlassesMeta, Snap, Find My separated with 0x19 length, near-owner with 0x02 length, Tile by 0xFEED, SmartTag, a phone with company 0x0075 -> Other); `isFlockWifi("Flock-3A9F", any)` true; `isFlockWifi("NETGEAR42", any)` false; at least one test per OUI/name from Step 1; `trackerKey` for the same separated payload on two different addresses returns equal keys.
- [ ] **Step 3:** Run, see FAIL, implement, run `pio test -e native -f test_sig`, PASS.
- [ ] **Step 4:** Commit `feat: wifi and ble signature classifier`.

### Task 5: Follow detector (tracker following you)

**Files:** Create `lib/follow/FollowDetector.h`, `lib/follow/FollowDetector.cpp`, `test/test_follow/test_follow.cpp`

**Interfaces:**
- Consumes: `BleKind`, `haversineM`.
- Produces:
  - `struct FollowAlert { uint8_t key[8]; BleKind kind; int8_t rssi; };`
  - `class FollowDetector { void observe(const uint8_t key[8], BleKind k, int8_t rssi, uint32_t nowMs, bool hasFix, double lat, double lon); bool poll(uint32_t nowMs, FollowAlert* out); void ignore(const uint8_t key[8]); size_t size() const; };`
- Only `FindMySeparated`, `Tile`, `SmartTag` are tracked. Each entry stores first-seen time and first-seen position (first observation with a fix), last-seen time, max distance from first position, alerted flag. `poll` returns each qualifying tracker once (rules in Global Constraints). Entries unseen for 5 min are dropped. Table capped at `kMaxBleTracked` (64): when full, evict the least recently seen. `ignore` list holds up to 16 keys, RAM only (cleared on reboot).

- [ ] **Step 1:** Tests: `test_alerts_after_10min_and_half_mile`; `test_no_alert_if_stationary_30min` (parked next to someone); `test_no_alert_if_moved_but_only_5min`; `test_no_alert_if_last_seen_over_2min_ago`; `test_alerts_once_only`; `test_near_owner_never_alerts`; `test_ignore_suppresses`; `test_table_caps_at_64` (feed 200 keys, size()==64, no crash); `test_no_fix_does_not_start_distance` (observations without fix don't set the start position).
- [ ] **Step 2:** Run, FAIL, implement, `pio test -e native -f test_follow`, PASS.
- [ ] **Step 3:** Commit `feat: tracker follow detector`.

### Task 6: Button state machine

**Files:** Create `lib/button/ButtonFsm.h`, `lib/button/ButtonFsm.cpp`, `test/test_button/test_button.cpp`

**Interfaces:**
- Produces: `enum class BtnEvent { None, ShortPress, HoldProgress, HoldDone };` `class ButtonFsm { BtnEvent update(bool pressed, uint32_t nowMs); float holdFraction() const; };` (debounce 30 ms; release before `kShortPressMaxMs` (1000) = ShortPress; held past 1000 ms emits HoldProgress each update with `holdFraction` 0..1 over 1000..`kHoldMarkMs` measured from press start; reaching 5000 ms emits HoldDone once; release between 1000 and 5000 ms emits None, the hold is cancelled.)

- [ ] **Step 1:** Tests: `test_short_press`, `test_bounce_ignored` (10 ms blip), `test_hold_done_at_5000`, `test_hold_done_once`, `test_release_at_3s_cancels`, `test_hold_fraction_midway` (3000 ms -> 0.5).
- [ ] **Step 2:** Run, FAIL, implement, PASS. Commit `feat: button state machine`.

### Task 7: Marks store

**Files:** Create `lib/marks/MarkStore.h`, `lib/marks/MarkStore.cpp`, `lib/marks/MarkIO.h` (interface), `src/hw/LittleFsMarkIO.cpp`, `test/test_marks/test_marks.cpp`

**Interfaces:**
- Produces:
  - `struct Mark { int32_t latE7; int32_t lonE7; uint32_t unixTime; uint8_t brand; uint8_t flags; };` (flags bit0 = unconfirmed from mesh, reserved for Phase 2)
  - `struct MarkIO { virtual bool load(uint8_t* buf, size_t cap, size_t* len) = 0; virtual bool save(const uint8_t* buf, size_t len) = 0; virtual ~MarkIO() = default; };`
  - `enum class AddResult { Ok, Full, NoFix, IoError };`
  - `class MarkStore { explicit MarkStore(MarkIO& io); bool begin(); AddResult add(bool hasFix, double lat, double lon, uint32_t unixTime, uint8_t brand); size_t count() const; const Mark& at(size_t i) const; };` (file `/marks.bin`: `"SRMK"`, `u16 version=1`, `u16 count`, then records; write whole file to a temp name and rename.)

- [ ] **Step 1:** Tests with an in-memory `MarkIO`: `test_add_and_count`, `test_no_fix_rejected`, `test_store_full` (501st returns Full), `test_persist_roundtrip` (new MarkStore on same IO sees marks), `test_corrupt_file_starts_empty`, `test_io_error_reported`.
- [ ] **Step 2:** Run, FAIL, implement, PASS.
- [ ] **Step 3:** Implement `LittleFsMarkIO` (device only, not in native build). Commit `feat: marks store`.

### Task 8: Camera alert and app state logic

**Files:** Create `lib/app/AppLogic.h`, `lib/app/AppLogic.cpp`, `test/test_app/test_app.cpp`

**Interfaces:**
- Consumes: `CamDb`, `NearCam`, `haversineM`, `bearingDeg`, `relativeArrowDeg`, `compass8`, `BtnEvent`, `FollowAlert`, `MarkStore`, `AddResult`.
- Produces:
  - `enum class Mode { Cam, Wifi, Bt };`
  - `enum class Overlay { None, WaitingGps, NoCamData, CameraAhead, NewFlock, Marking, Marked, MarkRefused, MarksFull, TrackerFollowing, GlassesToast };`
  - `struct Fix { bool valid; double lat, lon; float speedMps; float courseDeg; uint8_t sats; uint32_t unixTime; };`
  - `struct CamRow { char dist[8]; const char* brand; int arrowDeg; const char* compass; bool unconfirmed; };` (`arrowDeg = -1` means show `compass` instead)
  - `class AppLogic { AppLogic(const CamDb& db, MarkStore& marks); void onFix(const Fix& f, uint32_t nowMs); void onButton(BtnEvent e, float holdFraction, uint32_t nowMs); void onFlockHeard(uint32_t nowMs); void onFollow(const FollowAlert& a, uint32_t nowMs); void onGlasses(BleKind k, int8_t rssi, uint32_t nowMs); void tick(uint32_t nowMs); Mode mode() const; Overlay overlay() const; size_t camRows(CamRow* out, size_t max) const; uint32_t withinCount() const; };`
- Rules: CameraAhead fires once per camera when within 1000 ft and re-arms after it is 0.5 mi away; shown 6 s. NewFlock when `onFlockHeard` and no database camera or own mark within 500 ft; shown 8 s; at most once per 2 min. Marking overlay while hold progresses; HoldDone -> MarkStore.add -> Marked (4 s) / MarkRefused "NO GPS FIX" / MarksFull. TrackerFollowing shown until a short press dismisses it; a 5 s hold during it calls `FollowDetector::ignore` (via a callback set by main). GlassesToast 3 s when RSSI >= -60 and that glasses key wasn't seen in the last 10 min. Priority: TrackerFollowing > CameraAhead > Marking/Marked/MarkRefused/MarksFull > NewFlock > GlassesToast > WaitingGps/NoCamData. A short press while an overlay is up dismisses the overlay instead of changing mode. Fix older than 5 s = invalid.

- [ ] **Step 1:** Tests: `test_mode_cycles_cam_wifi_bt`; `test_short_press_dismisses_overlay_not_mode`; `test_camera_ahead_once_then_rearms`; `test_arrow_relative_when_moving`; `test_compass_when_stopped`; `test_no_fix_waiting_gps`; `test_no_fix_stale_fix_no_alert` (last fix 6 s old, drive "into" a camera: no alert); `test_no_fix_mark_refused`; `test_marks_full_overlay`; `test_new_flock_suppressed_near_known_camera`; `test_new_flock_rate_limited`; `test_tracker_overlay_priority_over_camera`; `test_glasses_toast_once_per_10min`; `test_no_cam_data_still_allows_wifi_bt_modes`.
- [ ] **Step 2:** Run, FAIL, implement, `pio test -e native`, all suites PASS.
- [ ] **Step 3:** Commit `feat: app state logic`.

### Task 9: Hardware adapters (GPS, WiFi scan, BLE scan, camdb partition)

**Files:** Create `src/hw/Gps.h/.cpp`, `src/hw/WifiScan.h/.cpp`, `src/hw/BleScan.h/.cpp`, `src/hw/CamDbPartition.h/.cpp`, `tools/flash_camdb.ps1`

**Interfaces:**
- Produces:
  - `bool gpsBegin(); bool gpsPoll(Fix* out);` (power GPS EN low, reset pulse, TinyGPSPlus on Serial1 9600; valid when location valid and age < 2 s)
  - `struct ApInfo { char ssid[33]; uint8_t bssid[6]; int8_t rssi; uint8_t channel; bool flock; };` `void wifiScanBegin(); size_t wifiScanResults(ApInfo* out, size_t max);` (async passive scan via `WiFi.scanNetworks(true, true, true)`, STA mode, never connect; sorted flock first then RSSI)
  - `struct BleSeen { char label[16]; int8_t rssi; BleKind kind; uint8_t key[8]; };` `void bleScanBegin(void (*cb)(const BleAdv&));` `size_t bleList(BleSeen* out, size_t max);` (NimBLE passive scan, duplicates allowed, flagged kinds first then RSSI)
  - `const uint8_t* camDbMap(size_t* len);` (`esp_partition_find_first(ESP_PARTITION_TYPE_DATA, 0x40, "camdb")` + `esp_partition_mmap`)
- `tools/flash_camdb.ps1`: runs `python -m esptool --chip esp32s3 --port COM11 write_flash 0x710000 <camdb.bin>` using PlatformIO's esptool.

- [ ] **Step 1:** Temporary `src/main.cpp` diag mode: print fix, top 5 APs, top 5 BLE devices, camdb count to serial every 2 s. Build and upload.
- [ ] **Step 2:** Flash the real `camdb.bin`: `powershell -File tools/flash_camdb.ps1 camdb.bin`. Then `pio device monitor`. Expected: camdb count equals Task 3 Step 5 count; APs and BLE devices listed; with sky view a fix within ~2 min (Rev puts it by a window or outside).
- [ ] **Step 3:** Confirm no `WiFi.begin` / connect call exists: `grep -rn "WiFi.begin\|esp_wifi_connect" src lib` returns nothing.
- [ ] **Step 4:** Commit `feat: hardware adapters`.

### Task 10: Screens

**Files:** Create `src/ui/Screens.h/.cpp`

**Interfaces:**
- Consumes: `AppLogic` getters, `ApInfo`, `BleSeen`, battery voltage.
- Produces: `void drawFrame(U8G2& d, const AppLogic& app, const ApInfo* aps, size_t nAps, const BleSeen* ble, size_t nBle, uint8_t sats, float vbat, uint32_t camCount);`
- Layouts follow the spec and the chosen mockups (layout C): top strip with inverted mode tag `CAM`/`WIFI`/`BT`, sats, battery; CAM list of 4 rows with the nearest inverted and arrow glyphs rotated by `arrowDeg` (or compass text), unconfirmed rows hollow; footer "N within 5mi"; WIFI and BT lists strongest first with signal bars, flagged items inverted with `!`; full-screen inverted CAMERA AHEAD and TRACKER FOLLOWING YOU; NEW FLOCK prompt; marking progress bar; MARKED with lat/lon and "#N to review"; NO GPS FIX; MARKS FULL; WAITING FOR GPS with camera count; NO CAMERA DATA. LED blinks 2 Hz during CameraAhead and TrackerFollowing.

- [ ] **Step 1:** Add a `DEMO` build flag env `heltec_v4_demo` that feeds fake fixes, APs and BLE entries and cycles every overlay every 4 s.
- [ ] **Step 2:** Upload demo. Rev checks each screen on the board is readable and upright. Fix anything he flags.
- [ ] **Step 3:** Commit `feat: screens`.

### Task 11: Wire it together and field test

**Files:** Modify `src/main.cpp`

- [ ] **Step 1:** `setup()`: Vext on, OLED boot screen 2 s (with OSM credit), LittleFS mount (format on failure), `MarkStore.begin()`, `CamDb.open(camDbMap())`, `gpsBegin()`, `wifiScanBegin()`, `bleScanBegin(cb)`; cb classifies, feeds `FollowDetector.observe`, `AppLogic.onGlasses`, and `onFlockHeard` for Flock BLE. `loop()` at ~20 Hz: poll GPS, button, WiFi results (flock -> `onFlockHeard`), `FollowDetector.poll` -> `onFollow`, `AppLogic.tick`, draw. Wire the ignore callback.
- [ ] **Step 2:** `pio test -e native` all PASS; `pio run -e heltec_v4` builds; record flash and RAM use from the build output.
- [ ] **Step 3:** Upload. Bench test: short press cycles modes; 5 s hold indoors with no fix shows NO GPS FIX; with fix, MARKED and count increments and survives a power cycle.
- [ ] **Step 4:** Drive test with Rev on USB power in the car: arrows track a known camera from DeFlock, CAMERA AHEAD fires around 1000 ft once, WIFI and BT lists populate. Rev reports what felt wrong; adjust constants in `Config.h` only.
- [ ] **Step 5:** Commit `feat: phase 1 firmware complete`. Then update `D:\brain\Projects\Simian Radar.md` with: status, camera count, flash/RAM use, any constant changes, and the next step (Phase 2 mesh plan).
