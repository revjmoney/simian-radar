#include "WifiFrame.h"
#include <string.h>

static const uint8_t kRidOui[3] = {0xFA, 0x0B, 0xBC};
static const uint8_t kRidOuiType = 0x0D;

bool parseWifiFrame(const uint8_t* f, size_t len, WifiFrame* out) {
  memset(out, 0, sizeof *out);
  if (!f || len < 10) return false;
  out->type = (f[0] >> 2) & 3;
  out->subtype = (f[0] >> 4) & 0xF;
  memcpy(out->a1, f + 4, 6);

  if (out->type == 1) {  // control: CTS and ACK carry only the receiver address
    bool twoAddr = out->subtype != 12 && out->subtype != 13;
    if (twoAddr && len >= 16) {
      memcpy(out->a2, f + 10, 6);
      out->hasA2 = true;
    }
    return true;
  }
  if (len < 24) return false;
  memcpy(out->a2, f + 10, 6);
  memcpy(out->a3, f + 16, 6);
  out->hasA2 = out->hasA3 = true;
  if (out->type != 0) return true;

  size_t ies;
  switch (out->subtype) {
    case kWifiBeacon:
    case kWifiProbeResp:
      if (len < 36) return true;
      out->open = !((f[34] | (f[35] << 8)) & 0x0010);  // capability: privacy bit
      ies = 36;
      break;
    case kWifiProbeReq:
      ies = 24;
      break;
    default:
      return true;
  }
  for (size_t off = ies; off + 2 <= len;) {
    uint8_t id = f[off], n = f[off + 1];
    if (off + 2 + n > len) break;  // element claims more than the frame has
    const uint8_t* body = f + off + 2;
    if (id == 0 && !out->hasSsid) {
      size_t m = n > 32 ? 32 : n;
      memcpy(out->ssid, body, m);
      out->ssid[m] = 0;
      out->hasSsid = true;
    } else if (id == 221 && n >= 4 && memcmp(body, kRidOui, 3) == 0 && body[3] == kRidOuiType) {
      out->ridIe = body + 4;
      out->ridLen = n - 4;
    }
    off += 2 + n;
  }
  return true;
}
