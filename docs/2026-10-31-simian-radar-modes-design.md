# Simian Radar: all-signals scanner modes (design addendum)

Date: 2026-10-31. Owner: Rev J Money. Extends `2026-10-31-simian-radar-design.md` (read that first;
everything there still holds unless this file changes it).

## Intent

Rev: "add modes for everything... we want to scan and switch for any device or signal this thing
can pick up." The device becomes a listen-only scanner for every kind of device its three
receivers can hear: the ESP32-S3 2.4 GHz radio (WiFi + Bluetooth LE), the SX1262 900 MHz radio
(LoRa and GFSK), and the L76K GPS.

## Rules (Rev's, unchanged from the base design)

- **One mode on screen at a time. Only the button changes it.** No auto-switching, no demo or fake
  data anywhere in the firmware.
- Short tap = next mode. Hold 5 s = mark a camera (needs a GPS fix), works in every mode.
- **Listen only.** Nothing transmits: no WiFi association, no BLE scan requests (passive scan), the
  SX1262 is only ever put in receive/CAD/RSSI states and never in TX. Only metadata is read
  (addresses, names, IDs, headers, broadcast payloads that are public by design such as Remote ID,
  weather station readings, MeshCore adverts). No decryption of anything.
- Background alerts from the base design stay: CAMERA AHEAD, TRACKER FOLLOWS YOU, glasses toast.

## Mode order (tap cycles through, then wraps)

| # | Tag | Radio | Shows |
|---|-----|-------|-------|
| 1 | CAM | GPS + DB | nearest ALPR cameras (existing) |
| 2 | WIFI | 2.4 | networks (existing) |
| 3 | PHONE | 2.4 | devices sending WiFi probe requests |
| 4 | SPYCAM | 2.4 | WiFi cameras by maker |
| 5 | CHAN | 2.4 | traffic per WiFi channel 1 to 11 |
| 6 | ATTACK | 2.4 | WiFi and BLE attack tools |
| 7 | BT | 2.4 | all BLE devices (existing) |
| 8 | TAGS | 2.4 | trackers and smart glasses only |
| 9 | BODYCAM | 2.4 | Axon body cams, Tasers, in-car units |
| 10 | DRONE | 2.4 | FAA Remote ID drones |
| 11 | MESH | 900 | Meshtastic and MeshCore nodes |
| 12 | LORAWAN | 900 | LoRaWAN devices and networks |
| 13 | ZWAVE | 900 | Z-Wave smart-home devices |
| 14 | WX | 900 | 915 MHz weather stations |
| 15 | 900 | 900 | 902 to 928 MHz sweep, RFID/toll reader flag |
| 16 | SKY | GPS | satellites, fix, jamming/spoofing guard |

Tags must fit the 22 to 30 px inverted box in the top strip; builder may shorten them.

## Radio sharing

- 2.4 GHz: the WiFi sniffer (channel hopping 1 to 11) and passive BLE scan run all the time, as now.
  To see PHONE/SPYCAM/CHAN data the sniffer reads **data and control frame headers too** (addresses
  and frame type only, never the body).
- 900 MHz: the SX1262 does one thing at a time, so modes 11 to 15 run **only while that mode is on
  screen**; leaving the mode puts the SX1262 in standby. Lists for a 900 mode start empty each time
  you enter it.
- GPS: always on.

## Mode details

**PHONE.** Transmitters seen sending probe requests in the last 60 s, strongest first: maker (from a
built-in OUI table) or "random" when the address has the locally-administered bit set, plus the
network name being probed if any. Footer: count.

**SPYCAM.** Any WiFi transmitter (AP or client, from any frame header) whose OUI or SSID matches a
camera maker table: Ring, Blink, Wyze, Arlo, Eufy, Hikvision, Dahua, Reolink, Nest/Google cams,
Tuya/Smart Life generic cams, and the common cheap "IPC"/"HDcam" SSID patterns. Shows maker, signal,
seconds since last heard. Builder compiles the table from public OUI registries and existing
open-source hidden-camera detectors; cite sources in the file.

**CHAN.** Frames per second on each channel 1 to 11 as a bar graph (counts from the sniffer, which
dwells on each channel in turn; normalise by dwell time).

**ATTACK.** Rolling list of detections, newest first, each with age:
- Deauth/disassoc flood: more than 10 per second for one BSSID.
- Evil twin: one SSID advertised by BSSIDs from two different makers (OUIs), with at least one
  open/unencrypted.
- Karma/Pineapple: one BSSID answering probe responses for 5 or more distinct SSIDs in 60 s.
- Pwnagotchi: beacons from `de:ad:be:ef:de:ad` (its JSON beacon).
- Flipper Zero: BLE name starting "Flipper" or its published service UUIDs.
- BLE popup spam: Apple Continuity proximity-pairing / nearby-action adverts from 10+ different
  addresses within 10 s (and the Samsung/Google/Microsoft equivalents where signatures are public).
A new detection shows a short toast in any mode (same style as the glasses toast), once per
detection type per 2 minutes.

**TAGS.** Only trackers and glasses, with how long each has been near and whether it is moving with
you. Adds Google Find My Device network tags (Fast Pair FMDN, service 0xFEAA EID frames), Chipolo
and Pebblebee to the existing AirTag/Tile/SmartTag/Meta/Snap set. The follow detector covers the new
tracker kinds too.

**BODYCAM.** BLE devices from Axon (body cameras, Tasers, Signal units). Builder verifies the
identifiers (OUI and/or advert format) from published research before relying on them; cite
sources.

**DRONE.** ASTM F3411 / FAA Remote ID over WiFi beacons (vendor IE, OUI FA:0B:BC) and BLE legacy
adverts (service data UUID 0xFFFA). Decode Basic ID (serial), Location (lat/lon/alt/speed/heading)
and System (operator location). List: ID, distance and arrow to the drone, altitude, distance to
the pilot. Use the Apache-2.0 opendroneid-core-c decoder (record in THIRD_PARTY.md).

**MESH.** SX1262 in LoRa receive. Alternates every 30 s between the US Meshtastic default
(LongFast) and the US MeshCore preset (910.525 MHz, SF7, BW 62.5 kHz, CR5); builder takes exact
frequency, sync word and preamble for each from their source code. Lists nodes heard: Meshtastic
node ID (from the unencrypted header) and hop info; MeshCore node name and position from public
adverts. RSSI and SNR.

**LORAWAN.** US915 uplinks. The SX1262 listens on one channel and spreading factor at a time, so it
steps through sub-band 2 (the common one: 903.9 to 905.3 MHz, 8 channels) using channel activity
detection per SF7 to SF10. Decodes MHDR and DevAddr only; maps the DevAddr NetID prefix to the
network name where known (Helium, The Things Network). List of devices with network and signal.

**ZWAVE.** SX1262 in GFSK receive on the US Z-Wave channels (908.42 MHz at 9.6 and 40 kbps,
916 MHz at 100 kbps), alternating. Decodes the frame header: HomeID, source node, frame type. Lists
networks (HomeID) with node count and signal.

**WX.** SX1262 in GFSK receive for 915 MHz weather stations (Fine Offset / Ambient Weather /
Ecowitt families). Builder takes modulation and frame formats from rtl_433 protocol definitions.
Shows station ID, temperature, humidity, wind where present.

**900.** RSSI sweep 902 to 928 MHz in 0.5 MHz steps as a bar graph with peak hold. Reuse the sweep
approach from Rev's working `D:\blink\heltec-probe` firmware on this same board. Flag "RFID/TOLL
READER?" when strong carriers keep hopping across the band for 5 s or more.

**SKY.** From NMEA GSV/GSA/RMC: satellites in view per constellation with signal bars, fix type,
HDOP, altitude, speed, UTC time. GPS guard toasts: JAMMING? when the fix is lost while satellites
that were strong all drop by 10 dB or more; SPOOFING? when consecutive fixes jump more than 500 m
faster than 150 mph would allow, or GPS time jumps against the device clock.

## Not possible on this hardware (do not attempt)

Zigbee/Thread (no 802.15.4 radio on the ESP32-S3), 5 GHz WiFi, 315/433 MHz devices (front end and
antenna are 915 MHz), utility meters (OOK, which the SX1262 cannot demodulate), cellular.

## Open item for Phase 2

Phase 2 puts MeshCore mesh chat on the SX1262. That conflicts with modes 11 to 15 owning the radio.
Phase 2's design must decide how the radio is shared (for example: mesh receive whenever no 900
mode is on screen). Not decided here.

## Left to the builder

Signature tables and their sources, frame parsers, list layouts within the existing screen style,
dwell times, thresholds not given above, memory caps per list (keep total RAM use sane).
