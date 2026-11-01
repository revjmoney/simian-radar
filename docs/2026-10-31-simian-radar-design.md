# Simian Radar: design

Date: 2026-10-31. Owner: Rev J Money. Status: reviewed, awaiting Rev's approval.

## Intent

A pocket privacy gadget built on Rev's Heltec WiFi LoRa 32 V4 (with the L76K GPS fitted) in his
walkie-talkie-style 3D printed case (antenna on top, screen portrait). It tells you what is
watching or tracking you nearby:

- Bluetooth trackers (AirTag, Tile) and smart glasses (Meta, Snap) close to you.
- Known license-plate cameras (Flock and other ALPR) with distance and an arrow toward each.
- Flock cameras heard over the air that are not on any map yet, which you can mark with one long
  button press and later submit to DeFlock.

Units talk to each other over a LoRa mesh: one shared group chat, plus newly marked cameras.

**Positioning (Rev's decision):** sell it as a personal privacy and anti-stalking device with the
camera map included, not as a "dodge the cops" box. Rev likes that cameras catch real crime; the
problem is misuse (officers searching for "LMAO", ex-officer use, stalking, wrongful arrests).

**Why now:** Flock/ALPR was the single most repeated theme on Slashdot Sep to Oct 2026 (25+ front
page stories), smart-glasses privacy was second (ZuckOff iOS app hit ~5k users in a month).
Competitors are free single-purpose tools (flock-you ESP32 forks, OUI-SPY, Nearby Glasses for
Android, ZuckOff for iOS, the DeFlock map). Our edge is one polished, ready-made device that does
all of it offline, plus mesh.

## Goals

1. Zero-settings device. No menus on the device, no radio settings in the phone app.
2. One button (the board's PRG/user button):
   - **short press:** cycle mode CAM → WIFI → BT → CAM.
   - **hold 5 s:** mark a new camera at the current GPS position (works in every mode).
3. Portrait OLED UI (64 x 128 after rotation), screens below.
4. Offline camera database for at least the whole US stored on the board.
5. Mesh chat and camera reports on MeshCore, every unit on the same baked-in settings.
6. Android companion app: chat + review/submit marks to DeFlock (OpenStreetMap).

## Non-goals (v1)

- Direct messages. One group channel only.
- iPhone app.
- Non-US regions or radio bands.
- A radar view in the phone app (the device is the radar).
- Updating the camera database over Bluetooth (PC over USB for v1; phone later).

## Anti-goals (would make it a failure even if it works)

- **Never broadcast anyone's own position over the mesh.** Rev's decision: users don't want to be
  tracked. The mesh carries only chat text and camera reports. Camera reports carry no sender name.
- **Never transmit at, jam, or interfere with cameras.**
- **Never associate with or join any camera's (or anyone's) WiFi network**, open or not.
- **Never capture packet payload contents.** Read only broadcast metadata: SSID, BSSID/MAC, RSSI,
  channel, BLE advertisement data (company ID, name, service UUIDs).
- **Never auto-submit to OpenStreetMap/DeFlock.** OSM organised-editing rules need a human to place
  and tag each node. Submission happens only from the phone app, by the user, under the user's own
  OSM account.
- No settings maze like the Meshtastic app.
- Don't advertise Null Island: if there's no GPS fix, a hold does not create a mark.

## Power and use case (Rev's decision)

Mainly a **car device on USB power**. Scan hard: WiFi, BLE, GPS and LoRa receive run continuously
in every mode. Battery life is not a v1 target; on battery it just runs until it's flat.

## Hardware facts (from `D:\brain\Reference\Heltec V4 MeshCore Node.md`)

- Board: Heltec WiFi LoRa 32 V4, plain V4 (ESP32-S3R2, 2 MB PSRAM, **16 MB flash**), GC1109 FEM.
  Rev's unit reports `Heltec V4 OLED`. Not the R8 variant. MeshCore env family: `heltec_v4` OLED.
- LoRa SX1262, 902-928 MHz. TX power: leave at board default `LORA_TX_POWER=10` (= ~22 dBm at
  antenna through the PA). Never key without an antenna.
- GPS L76K on `Serial1`: `PIN_GPS_RX=38`, `PIN_GPS_TX=39`, `PIN_GPS_EN=34` (active LOW),
  `PIN_GPS_RESET=42` (active LOW), 9600 baud. MeshCore's boot detect is a 1 s one-shot; use
  `ENV_SKIP_GPS_DETECT` or a longer detect so a slow module isn't marked absent.
- USB is the S3's native USB (VID 303A PID 1001), no driver.
- OLED: 0.96 in SSD1306 128 x 64, rotated 90° for portrait. **Builder: take OLED I2C pins, Vext
  power pin, button GPIO and LED GPIO from MeshCore's `variants/heltec_v4` files, not from V3
  docs.**
- One 2.4 GHz radio shared by WiFi scan, BLE scan and the BLE link to the phone (time-sliced).
  Scanner lists refresh every few seconds, that's expected.

## Device screens (mockups in the brainstorm session; layout "C" chosen)

Top strip on every screen: mode tag (inverted: `CAM` / `WIFI` / `BT`), GPS satellites, mesh
status, battery.

1. **Waiting for GPS** (boot until fix): spinner, "WAITING FOR GPS / go outside", camera count
   loaded from the database.
2. **CAM mode (default):** list of the nearest four cameras. Each row: small arrow, distance
   (mi, or ft under 0.2 mi), brand (FLOCK, VIGILANT, ... or UNCONF. for mesh/own unconfirmed).
   Nearest row highlighted (inverted). Footer: "N within 5mi".
3. **WIFI mode:** nearby access points, strongest first: SSID (or "(hidden)"), signal bar, channel.
   Anything matching a Flock signature floats to the top.
4. **BT mode:** nearby BLE devices, strongest first: name/type, signal bar. Smart glasses and
   tracker tags are flagged (inverted, "!") and pinned to the top with a rough distance.
5. **Close-camera alert** (pops over any mode): full-screen inverted "CAMERA AHEAD", big arrow,
   distance, brand; board LED blinks. Fires once per camera when within **1000 ft** (single
   constant, Rev's to tune after a drive test), then returns to the previous mode.
6. **Heard a new Flock:** when a Flock WiFi/BLE signature is detected and no known camera is within
   a radius (builder's call, suggest ~500 ft), show "HEARD A NEW FLOCK / not on map / hold button
   5 sec to mark it". Nudge only; nothing is marked automatically.
7. **Marking:** progress bar fills during the 5 s hold, then check mark, "MARKED", lat/lon,
   "sent to mesh", "#N to review".

### Arrow direction

The board has no compass. Arrows are relative to **direction of travel** (GPS course over ground)
when moving faster than a threshold (builder's call, ~2 mph). When stationary, rows show a compass
bearing (N, NE, ...) instead of an arrow. Distances use haversine.

## Detection

- **Smart glasses (BLE):** Bluetooth SIG company IDs in manufacturer data: `0x01AB` Meta Platforms,
  `0x058E` Meta Platforms Technologies, `0x0D53` Luxottica, `0x03C2` Snap. Known false positives
  (e.g. Meta Quest headsets); label as "possible".
- **Trackers (BLE):** Apple Find My / AirTag and Tile advertisement signatures (builder researches
  current signatures, including separated-from-owner mode).
- **"Following you" alert (Rev's decision, headline feature):** warn when a tracker you don't own
  stays with you over time **and** distance, the way Android's unknown-tracker alerts work. Default
  trigger: still seen after 10+ minutes **and** 0.5+ mile of travel (constants, tunable). Must
  follow a device across its address rotations (match on payload/key fragments where the
  signature allows). Gets its own full-screen inverted alert, like "CAMERA AHEAD": "TRACKER
  FOLLOWING YOU", type, rough distance, LED blinks. Runs in every mode. A way to dismiss "that's
  mine" for a tracker is the builder's call (e.g. hold-5 s while the alert is showing ignores it).
- **Smart glasses** keep a lighter treatment: flagged and pinned in the BT list, plus a short
  non-blocking toast when a new pair first appears within ~30 ft.
- **Flock (WiFi + BLE):** signatures as used by the open flock-you projects and DeFlock research
  (e.g. SSID patterns like `Flock-XXXXXX`, known OUIs, BLE names). Builder collects the current
  list from those projects and keeps it in one table. Check each project's licence before copying
  code; reimplement from the published signatures if the licence doesn't allow reuse in a product
  Rev sells.

## Camera database

- Source: OpenStreetMap nodes tagged `surveillance:type=ALPR` in the US (same data DeFlock and
  MapQuest's Surveillance Layer show), with `manufacturer` and `direction`/`camera:direction`.
- A PC tool (Python is fine) downloads them (Overpass API or a Geofabrik US extract; the public
  Overpass servers timed out on a whole-US query on 2026-10-31, so prefer the extract or tiled
  queries), packs each camera into ~12 bytes (int32 lat, int32 lon, uint16 direction, uint8
  brand code, padding/flags), sorts it into a spatial grid so nearby lookups are fast, and writes
  a binary file that gets flashed to a data partition over USB.
- **Partition table (my call, fixed now):** two ~3 MB app slots so over-the-air updates can be
  added later without re-partitioning, a small LittleFS for marks/settings, the rest (~9 MB) for
  the camera database (~750k cameras). v1 updates firmware and database over USB only; Rev flashes
  kits himself, so the PC tool can stay a dev script for v1.
- **Licence:** OpenStreetMap data is ODbL. A sold device must credit "© OpenStreetMap
  contributors" (boot screen line + product page) and say where the derived database comes from.
- Budget: ~9 MB for the database after the partition split above. Expected US count is tens of
  thousands to low hundreds of thousands.
- Marks and mesh-received unconfirmed reports are stored separately in a small writable store
  (LittleFS or NVS), survive reboots.

## Mesh (MeshCore underneath)

- Base: `meshcore-dev/MeshCore` (MIT; commercial use allowed). Build on the companion-radio
  example for the `heltec_v4` OLED target, with both BLE and USB companion interfaces compiled in.
- **Baked-in radio settings:** 910.525 MHz, SF7, BW 62.5 kHz, CR5, TX power board default. Matches
  102 of 127 nearby MeshCore nodes (2026-09-22 survey).
- **One baked-in group channel** "Simian Radar" with a fixed key compiled into the firmware. Every
  unit joins automatically. The key is effectively public (anyone with the firmware has it); treat
  it like a CB channel and say so in user docs.
- Identity: generated on first boot, default name like `Ape-XXXX` (from the public key). The phone
  app can rename once; that is the only user-facing setting.
- Location sharing OFF and not exposed: `adv_loc_policy` must not share location, no GPS in
  adverts, no telemetry location. (Rev's own existing node has `adv_loc_policy=1`; this firmware
  must force it off.)
- **Camera report message:** a compact, machine-readable message on the group channel (lat, lon,
  brand guess, timestamp, no sender name). Receiving units add it to their unconfirmed list (hollow
  marker / UNCONF.) and the phone app hides these from the chat view. Builder designs the format.
  **Sent right away** at the moment of marking (Rev's decision). Accepted trade-off: receivers can
  infer someone was at that spot at that time. No delay or time blurring.
- **The unit itself must not be trackable over the air (my call, from the anti-goal):**
  - No periodic LoRa adverts. Group-channel messages don't need them. Send no advert unless a
    feature truly needs one; if MeshCore requires one, send it without location and rarely.
  - BLE to the phone uses bonding plus a rotating resolvable private address, and a generic
    advertised name (not `Ape-XXXX`), so a passive scanner can't follow one unit. The bonded phone
    still finds it.
  - Our own Flock/tracker detector logic must not flag Simian Radar units.
- **Trust of mesh camera reports (my call):** the channel key is public, so anyone can inject fake
  reports. Mesh-received reports show only as UNCONF. rows and hollow markers. They never trigger
  the full-screen alert and don't count in "N within 5mi". Rate-limit per sender, drop duplicates
  within ~100 ft. Only the user's **own** marks can be sent to DeFlock from the app (OSM rules:
  you saw it yourself). Submitting or deleting a mark in the app changes nothing on the mesh in v1.
- **To verify early:** that stock MeshCore repeaters forward group-channel flood packets for a
  channel they don't hold the key to, so our traffic can ride existing repeaters (e.g. Calabash).

## Phone app (Android)

- Built with Rev's no-Gradle Android toolchain (see `D:\brain` [[Build Toolchain]]). Package under
  `com.simian.*` (builder proposes the name).
- Connects to the device over BLE using MeshCore's companion protocol; first run picks your unit,
  nothing else to configure.
- **Chat tab:** the one shared channel. Camera-report messages are hidden from chat.
- **Marks tab:** your marks on a map. For each: set the direction the camera faces, pick the brand,
  then **Send to DeFlock**, which signs into OpenStreetMap via OAuth with the user's own account and
  creates the node with the standard ALPR tags (`man_made=surveillance`, `surveillance:type=ALPR`,
  `manufacturer=...`, direction). Delete a mark that was nothing.
- Suggest in-app (once): use an OSM account without your real name if you don't want your edits
  tied to you; OSM edit history is public.
- Paid Play release needs a new 12-tester, 14-day closed test (see [[Google Play Account]]).

## Build order

1. **Device firmware standalone:** GPS, database lookup, the three modes, alerts, 5 s mark. Tested
   on Rev's board. Useful on its own.
2. **Mesh:** MeshCore integration, baked settings, shared channel, camera reports.
3. **Phone app.**
4. **PC database tool** is needed by step 1 (a small test file is enough to start).

Each step is tested on the real board before the next starts.

## Selling (later, not v1 scope)

Pre-flashed kits on jmscnc.com (Rotatocam-style direct sale). Lead the pitch with tracker and
smart-glasses detection (anti-stalking), camera map as a feature.

## Left to the builder

- Firmware framework details (PlatformIO, Arduino-on-ESP32 as MeshCore uses).
- Exact radii and thresholds other than the 1000 ft alert.
- Database file format details and grid size.
- Camera report wire format.
- Fonts and pixel layout within the chosen screen designs.
- App package name and UI details within the two tabs.
