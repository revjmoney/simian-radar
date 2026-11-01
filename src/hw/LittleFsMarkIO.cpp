#include "LittleFsMarkIO.h"
#include <LittleFS.h>

static const char* kPath = "/marks.bin";
static const char* kTmp = "/marks.tmp";

bool LittleFsMarkIO::load(uint8_t* buf, size_t cap, size_t* len) {
  File f = LittleFS.open(kPath, "r");
  if (!f) f = LittleFS.open(kTmp, "r");  // power cut between remove and rename in save()
  if (!f) return false;
  size_t n = f.size();
  bool ok = n <= cap && f.read(buf, n) == n;
  f.close();
  if (ok) *len = n;
  return ok;
}

bool LittleFsMarkIO::save(const uint8_t* buf, size_t len) {
  File f = LittleFS.open(kTmp, "w");
  if (!f) return false;
  bool ok = f.write(buf, len) == len;
  f.close();
  if (!ok) return false;
  LittleFS.remove(kPath);
  return LittleFS.rename(kTmp, kPath);
}
