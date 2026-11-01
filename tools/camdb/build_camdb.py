"""Build the Simian Radar camera database (camdb.bin) from OpenStreetMap ALPR nodes.

Format (little-endian), shared with lib/camdb/CamDb.cpp:
  header 32 bytes: magic "SRDB", u16 version, u16 cells per degree, i16 lat0, i16 lon0,
                   u16 rows, u16 cols, u32 count, u32 built_unix, 8 reserved bytes
  index: (rows*cols + 1) x u32 record start offsets
  records, sorted by cell: i32 lat_e7, i32 lon_e7, u16 direction (0xFFFF unknown), u8 brand, u8 flags
"""
import argparse
import json
import math
import re
import struct
import sys
import time

MAGIC = b"SRDB"
VERSION = 1
CELLS_PER_DEG = 4
NO_DIRECTION = 0xFFFF
MIN_CAMERAS = 10000
HEADER = "<4sHHhhHHII8x"

BRANDS = [
    (1, ("flock",)),
    (2, ("motorola", "vigilant")),
    (3, ("genetec",)),
    (4, ("axon",)),
    (5, ("leonardo", "elsag")),
    (6, ("rekor",)),
    (7, ("verkada",)),
    (8, ("neology",)),
]

# Lower 48, Alaska, Hawaii as (south, west, north, east); split into 5 x 5 degree tiles.
US_BOXES = [(24, -125, 50, -66), (51, -170, 72, -129), (18, -161, 23, -154)]
MIRRORS = [
    "https://overpass-api.de/api/interpreter",
    "https://overpass.kumi.systems/api/interpreter",
    "https://overpass.private.coffee/api/interpreter",
    "https://maps.mail.ru/osm/tools/overpass/api/interpreter",
]
USER_AGENT = "SimianRadar/0.1"
DEFLOCK_INDEX = "https://cdn.deflock.me/regions/index.json"
BRAND_TAGS = ("manufacturer", "brand", "surveillance:manufacturer", "surveillance:brand")


def brand_code(manufacturer):
    if not manufacturer:
        return 0
    m = manufacturer.lower()
    for code, needles in BRANDS:
        if any(n in m for n in needles):
            return code
    return 0


def _direction(tags):
    raw = tags.get("camera:direction") or tags.get("direction")
    if not raw:
        return None
    m = re.match(r"\s*(\d+(?:\.\d+)?)", raw)
    return int(float(m.group(1))) % 360 if m else None


def parse_overpass(js):
    out = []
    for el in js.get("elements", []):
        if el.get("type") != "node":
            continue
        tags = el.get("tags", {})
        maker = next((tags[k] for k in BRAND_TAGS if tags.get(k)), None)
        out.append({"lat": el["lat"], "lon": el["lon"], "direction": _direction(tags),
                    "manufacturer": maker})
    return out


def _in_us(lat, lon):
    return any(s <= lat <= n and w <= lon <= e for s, w, n, e in US_BOXES)


def _http_get_json(url):
    import requests

    r = requests.get(url, headers={"User-Agent": USER_AGENT}, timeout=120)
    r.raise_for_status()
    return r.json()


def download_deflock(get=_http_get_json):
    """DeFlock republishes every OSM ALPR node daily as 20 x 20 degree tiles on its CDN; fetch the
    tiles that overlap the US and keep the nodes inside the US boxes."""
    index = get(DEFLOCK_INDEX)
    elements = []
    for region in index["regions"]:
        lat, lon = (int(x) for x in region.split("/"))
        if not any(lat <= n and lat + 20 >= s and lon <= e and lon + 20 >= w for s, w, n, e in US_BOXES):
            continue
        for node in get(index["tile_url"].format(lat=lat, lon=lon)):
            if _in_us(node["lat"], node["lon"]):
                elements.append(dict(node, type="node"))
    return {"elements": elements}


def pack(cameras, built_unix=None):
    q = CELLS_PER_DEG
    if cameras:
        lat0 = math.floor(min(c["lat"] for c in cameras))
        lon0 = math.floor(min(c["lon"] for c in cameras))
        rows = int((max(c["lat"] for c in cameras) - lat0) * q) + 1
        cols = int((max(c["lon"] for c in cameras) - lon0) * q) + 1
    else:
        lat0 = lon0 = rows = cols = 0

    def cell(c):
        lat_e7, lon_e7 = round(c["lat"] * 1e7), round(c["lon"] * 1e7)
        # Bucket from the stored integer coordinates so the reader computes the same cell.
        r = int((lat_e7 / 1e7 - lat0) * q)
        col = int((lon_e7 / 1e7 - lon0) * q)
        return r * cols + col

    ordered = sorted(cameras, key=cell)
    index = [0] * (rows * cols + 1)
    for c in ordered:
        index[cell(c) + 1] += 1
    for i in range(1, len(index)):
        index[i] += index[i - 1]

    out = bytearray(struct.pack(HEADER, MAGIC, VERSION, q, lat0, lon0, rows, cols, len(ordered),
                                int(built_unix if built_unix is not None else time.time())))
    out += struct.pack("<%dI" % len(index), *index)
    for c in ordered:
        d = c["direction"]
        out += struct.pack("<iiHBB", round(c["lat"] * 1e7), round(c["lon"] * 1e7),
                           NO_DIRECTION if d is None else d, brand_code(c["manufacturer"]), 0)
    return bytes(out)


def _tiles():
    for s, w, n, e in US_BOXES:
        for lat in range(s, n, 5):
            for lon in range(w, e, 5):
                yield lat, lon, min(lat + 5, n), min(lon + 5, e)


def _http_fetch(url, query):
    import requests

    r = requests.post(url, data={"data": query}, headers={"User-Agent": USER_AGENT}, timeout=180)
    r.raise_for_status()
    return r.json()


def download(fetch=_http_fetch, cache_dir="build/tiles", tiles=None, pause=10, rounds=4):
    """Fetches every tile, trying each mirror in turn. Finished tiles are cached as JSON so a
    rerun after a failure only fetches what is missing."""
    import os

    os.makedirs(cache_dir, exist_ok=True)
    seen = {}
    for s, w, n, e in tiles if tiles is not None else _tiles():
        path = os.path.join(cache_dir, "%s_%s_%s_%s.json" % (s, w, n, e))
        if os.path.exists(path):
            with open(path, encoding="utf-8") as f:
                js = json.load(f)
        else:
            query = '[out:json][timeout:120];node["surveillance:type"="ALPR"](%s,%s,%s,%s);out;' % (s, w, n, e)
            js = None
            for attempt in range(rounds * len(MIRRORS)):
                url = MIRRORS[attempt % len(MIRRORS)]
                try:
                    js = fetch(url, query)
                    break
                except Exception as ex:  # overloaded server: move to the next mirror
                    print("tile %s,%s via %s failed: %s" % (s, w, url.split("/")[2], ex), file=sys.stderr)
                    time.sleep(pause)
            if js is None:
                raise SystemExit("tile %s,%s failed on every mirror; rerun to resume" % (s, w))
            with open(path, "w", encoding="utf-8") as f:
                json.dump(js, f)
        for el in js.get("elements", []):
            seen[el.get("id")] = el
    return {"elements": list(seen.values())}


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--out", required=True)
    ap.add_argument("--from-json", help="Overpass JSON file instead of downloading")
    ap.add_argument("--source", choices=["deflock", "overpass"], default="deflock",
                    help="deflock: DeFlock's daily CDN copy of the OSM data (fast); overpass: query OSM")
    ap.add_argument("--save-json", help="also save the downloaded Overpass JSON here")
    ap.add_argument("--allow-small", action="store_true",
                    help="write the database even if it has fewer than %d cameras" % MIN_CAMERAS)
    args = ap.parse_args(argv)
    if args.from_json:
        with open(args.from_json, encoding="utf-8") as f:
            js = json.load(f)
    else:
        js = download_deflock() if args.source == "deflock" else download()
        if args.save_json:
            with open(args.save_json, "w", encoding="utf-8") as f:
                json.dump(js, f)
    cameras = parse_overpass(js)
    if len(cameras) < MIN_CAMERAS and not args.allow_small:
        # An empty or tiny download would flash a database that looks like "no cameras around".
        raise SystemExit("only %d cameras; refusing to write (use --allow-small for test data)" % len(cameras))
    data = pack(cameras)
    with open(args.out, "wb") as f:
        f.write(data)
    print("%d cameras, %d bytes -> %s" % (len(cameras), len(data), args.out))


if __name__ == "__main__":
    main()
