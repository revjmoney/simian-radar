import struct

import build_camdb as b


def cams():
    return [
        {"lat": 33.70, "lon": -78.88, "direction": 90, "manufacturer": "Flock Safety"},
        {"lat": 34.10, "lon": -78.20, "direction": None, "manufacturer": None},
        {"lat": 33.60, "lon": -78.95, "direction": 270, "manufacturer": "Motorola Solutions"},
    ]


def test_brand_code_flock():
    assert b.brand_code("Flock Safety") == 1


def test_brand_code_none():
    assert b.brand_code(None) == 0


def test_brand_code_vigilant():
    assert b.brand_code("Vigilant Solutions") == 2


def test_pack_layout():
    data = b.pack(cams())
    magic, version, q, lat0, lon0, rows, cols, count = struct.unpack_from("<4sHHhhHHI", data, 0)
    assert magic == b"SRDB" and version == 1 and q == 4 and count == 3
    assert len(data) == 32 + (rows * cols + 1) * 4 + 3 * 12


def test_pack_index_matches_records():
    data = b.pack(cams())
    _, _, q, lat0, lon0, rows, cols, count = struct.unpack_from("<4sHHhhHHI", data, 0)
    idx = struct.unpack_from("<%dI" % (rows * cols + 1), data, 32)
    assert idx[0] == 0 and idx[-1] == count
    assert list(idx) == sorted(idx)
    base = 32 + len(idx) * 4
    for cell in range(rows * cols):
        for i in range(idx[cell], idx[cell + 1]):
            lat_e7, lon_e7 = struct.unpack_from("<ii", data, base + i * 12)
            r = int((lat_e7 / 1e7 - lat0) * q)
            c = int((lon_e7 / 1e7 - lon0) * q)
            assert r * cols + c == cell


def test_pack_unknown_direction():
    data = b.pack([cams()[1]])
    rows, cols = struct.unpack_from("<HH", data, 12)
    base = 32 + (rows * cols + 1) * 4
    _, _, d, brand, _ = struct.unpack_from("<iiHBB", data, base)
    assert d == 0xFFFF and brand == 0


def test_pack_empty():
    data = b.pack([])
    assert struct.unpack_from("<I", data, 16)[0] == 0


def test_parse_overpass_direction():
    js = {"elements": [{"type": "node", "lat": 1.0, "lon": 2.0,
                        "tags": {"camera:direction": "45;90", "manufacturer": "Flock Safety"}}]}
    out = b.parse_overpass(js)
    assert out == [{"lat": 1.0, "lon": 2.0, "direction": 45, "manufacturer": "Flock Safety"}]


def test_download_rotates_mirrors_and_caches(tmp_path):
    calls = []

    def fetch(url, query):
        calls.append(url)
        if url == b.MIRRORS[0]:
            raise IOError("504")
        return {"elements": [{"type": "node", "id": len(calls), "lat": 1.0, "lon": 2.0, "tags": {}}]}

    tiles = [(1, 2, 3, 4), (5, 6, 7, 8)]
    js = b.download(fetch=fetch, cache_dir=str(tmp_path), tiles=tiles, pause=0)
    assert len(js["elements"]) == 2
    assert b.MIRRORS[1] in calls
    n = len(calls)
    b.download(fetch=fetch, cache_dir=str(tmp_path), tiles=tiles, pause=0)
    assert len(calls) == n  # second run served from the cache


def test_brand_from_other_tags():
    js = {"elements": [{"type": "node", "lat": 1.0, "lon": 2.0, "tags": {"brand": "Flock Safety"}},
                       {"type": "node", "lat": 1.0, "lon": 2.0, "tags": {"surveillance:manufacturer": "Motorola"}}]}
    out = b.parse_overpass(js)
    assert [b.brand_code(c["manufacturer"]) for c in out] == [1, 2]


def test_download_deflock_keeps_us_only():
    index = {"regions": ["40/-80", "40/0"], "tile_url": "https://cdn/regions/{lat}/{lon}.json?v=1"}
    tiles = {
        "https://cdn/regions/40/-80.json?v=1": [
            {"id": 1, "lat": 33.7, "lon": -78.9, "tags": {"manufacturer": "Flock Safety"}},
            {"id": 2, "lat": 19.4, "lon": -99.1, "tags": {}},  # Mexico City: outside the US boxes
        ],
    }
    fetched = []

    def get(url):
        fetched.append(url)
        return index if url.endswith("index.json") else tiles[url]

    js = b.download_deflock(get=get)
    assert [e["id"] for e in js["elements"]] == [1]
    assert js["elements"][0]["type"] == "node"
    assert not any("/40/0.json" in u for u in fetched)  # Europe tile never fetched


def test_main_refuses_tiny_database(tmp_path):
    src = tmp_path / "few.json"
    src.write_text('{"elements": [{"type": "node", "lat": 1.0, "lon": 2.0, "tags": {}}]}')
    out = tmp_path / "db.bin"
    import pytest
    with pytest.raises(SystemExit):
        b.main(["--out", str(out), "--from-json", str(src)])
    assert not out.exists()
    b.main(["--out", str(out), "--from-json", str(src), "--allow-small"])
    assert out.exists()
