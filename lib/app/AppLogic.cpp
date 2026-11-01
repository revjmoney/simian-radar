#include "AppLogic.h"
#include <string.h>
#include "Geo.h"

static const uint32_t kFixStaleMs = 5000;
static const uint32_t kAheadShowMs = 6000;
static const float kRearmM = 804.67f;  // 0.5 mi
static const uint32_t kNewFlockShowMs = 8000;
static const uint32_t kNewFlockEveryMs = 2UL * 60UL * 1000UL;
static const uint32_t kProgressGapMs = 250;
static const uint32_t kMarkShowMs = 4000;
static const uint32_t kGlassesShowMs = 3000;
static const uint32_t kGlassesEveryMs = 10UL * 60UL * 1000UL;
static const uint32_t kToastShowMs = 3000;

const char* modeTag(Mode m) {
  static const char* tags[] = {"CAM", "WIFI", "PHONE", "SPYCAM", "CHAN", "ATTACK", "BT", "TAGS", "BODYCAM",
                               "DRONE", "MESH", "LORAWAN", "ZWAVE", "WX", "900", "SKY"};
  return m < Mode::Count ? tags[(int)m] : "";
}

bool isSubGhz(Mode m) { return m >= Mode::Mesh && m <= Mode::Sweep; }

void AppLogic::onToast(const char* text, uint32_t nowMs) {
  now_ = nowMs;
  strncpy(toastText_, text, sizeof toastText_ - 1);
  toastText_[sizeof toastText_ - 1] = 0;
  toastUntil_ = nowMs + kToastShowMs;
}

bool AppLogic::hasFix() const { return fix_.valid && now_ - fixMs_ <= kFixStaleMs; }

void AppLogic::onFix(const Fix& f, uint32_t nowMs) {
  now_ = nowMs;
  if (!f.valid) return;
  fix_ = f;
  fixMs_ = nowMs;
}

void AppLogic::onButton(BtnEvent e, float holdFraction, uint32_t nowMs) {
  now_ = nowMs;
  switch (e) {
    case BtnEvent::ShortPress:
      progressMs_ = 0;
      if (!dismissTop()) mode_ = (Mode)(((int)mode_ + 1) % (int)Mode::Count);
      break;
    case BtnEvent::HoldProgress:
      progressMs_ = nowMs;
      holdFraction_ = holdFraction;
      break;
    case BtnEvent::HoldDone: {
      progressMs_ = 0;
      if (trackerOn_) {  // "that one's mine": stop alerting on this tracker
        if (ignoreCb_) ignoreCb_(tracker_.key);
        trackerOn_ = false;
        break;
      }
      uint8_t brand = active(newFlockUntil_) ? 1 : 0;
      bool fix = hasFix();
      markResult_ = marks_.add(fix, fix_.lat, fix_.lon, fix_.unixTime, brand);
      markUntil_ = nowMs + kMarkShowMs;
      newFlockUntil_ = 0;
      break;
    }
    case BtnEvent::None:
      break;
  }
}

void AppLogic::onFlockHeard(uint32_t nowMs) {
  now_ = nowMs;
  if (!hasFix()) return;  // can't tell "not on the map" without a position
  if (newFlockEver_ && nowMs - lastNewFlockMs_ < kNewFlockEveryMs) return;
  NearCam near[1];
  uint32_t n = 0;
  db_.nearest(fix_.lat, fix_.lon, kNewFlockRadiusM, near, 1, &n);
  if (n) return;
  for (size_t i = 0; i < marks_.count(); i++) {
    const Mark& m = marks_.at(i);
    if (haversineM(fix_.lat, fix_.lon, m.latE7 / 1e7, m.lonE7 / 1e7) <= kNewFlockRadiusM) return;
  }
  newFlockEver_ = true;
  lastNewFlockMs_ = nowMs;
  newFlockUntil_ = nowMs + kNewFlockShowMs;
}

void AppLogic::onFollow(const FollowAlert& a, uint32_t nowMs) {
  now_ = nowMs;
  tracker_ = a;
  trackerOn_ = true;
}

void AppLogic::onGlasses(BleKind k, int8_t rssi, const uint8_t key[8], uint32_t nowMs) {
  now_ = nowMs;
  if (rssi < kGlassesRssi) return;
  Glasses* g = nullptr;
  for (size_t i = 0; i < nGlasses_; i++)
    if (memcmp(glasses_[i].key, key, 8) == 0) g = &glasses_[i];
  if (g && nowMs - g->toastedMs < kGlassesEveryMs) return;
  if (!g) {
    g = &glasses_[nGlasses_ < kGlasses ? nGlasses_++ : (nowMs / 1000) % kGlasses];
    memcpy(g->key, key, 8);
  }
  g->toastedMs = nowMs;
  glassesKind_ = k;
  glassesUntil_ = nowMs + kGlassesShowMs;
}

void AppLogic::tick(uint32_t nowMs) {
  now_ = nowMs;
  if (!hasFix()) {
    nRows_ = 0;
    within_ = 0;
    rowsFixMs_ = UINT32_MAX;
    return;
  }
  if (fixMs_ != rowsFixMs_) refreshRows();
}

void AppLogic::refreshRows() {
  rowsFixMs_ = fixMs_;
  NearCam near[kRows];
  size_t n = db_.nearest(fix_.lat, fix_.lon, kWithinRadiusM, near, kRows, &within_);

  // Re-arm cameras we've left behind, then alert on the first new one inside the radius.
  for (size_t i = 0; i < nAlerted_;) {
    if (haversineM(fix_.lat, fix_.lon, alerted_[i].latE7 / 1e7, alerted_[i].lonE7 / 1e7) > kRearmM)
      alerted_[i] = alerted_[--nAlerted_];
    else
      i++;
  }
  for (size_t i = 0; i < n; i++) {
    if (near[i].distM > kAlertRadiusM) break;
    bool seen = false;
    for (size_t j = 0; j < nAlerted_; j++)
      seen |= alerted_[j].latE7 == near[i].rec.latE7 && alerted_[j].lonE7 == near[i].rec.lonE7;
    if (seen) continue;
    if (nAlerted_ == kAlerted) memmove(alerted_, alerted_ + 1, --nAlerted_ * sizeof alerted_[0]);
    alerted_[nAlerted_++] = {near[i].rec.latE7, near[i].rec.lonE7};
    ahead_ = {near[i].rec.latE7, near[i].rec.lonE7, near[i].rec.brand, near[i].distM, near[i].bearingDeg, false};
    aheadUntil_ = now_ + kAheadShowMs;
    break;
  }

  // Merge database cameras with the user's own (unconfirmed) marks, nearest first.
  nRows_ = 0;
  auto insert = [&](const Row& r) {
    size_t pos = nRows_;
    while (pos > 0 && rows_[pos - 1].distM > r.distM) pos--;
    if (pos >= kRows) return;
    size_t last = nRows_ < kRows ? nRows_ : kRows - 1;
    for (size_t k = last; k > pos; k--) rows_[k] = rows_[k - 1];
    rows_[pos] = r;
    if (nRows_ < kRows) nRows_++;
  };
  for (size_t i = 0; i < n; i++)
    insert({near[i].rec.latE7, near[i].rec.lonE7, near[i].rec.brand, near[i].distM, near[i].bearingDeg, false});
  for (size_t i = 0; i < marks_.count(); i++) {
    const Mark& m = marks_.at(i);
    double lat = m.latE7 / 1e7, lon = m.lonE7 / 1e7;
    float d = (float)haversineM(fix_.lat, fix_.lon, lat, lon);
    if (d <= kWithinRadiusM)
      insert({m.latE7, m.lonE7, m.brand, d, (float)bearingDeg(fix_.lat, fix_.lon, lat, lon), true});
  }
}

CamRow AppLogic::toCamRow(const Row& r) const {
  CamRow c;
  formatDistance(r.distM, c.dist, sizeof c.dist);
  c.brand = r.unconfirmed ? "UNCONF." : brandLabel(r.brand);
  c.compass = compass8(r.bearingDeg);
  c.arrowDeg = fix_.speedMps >= kMovingSpeedMps ? relativeArrowDeg(r.bearingDeg, fix_.courseDeg) : -1;
  c.unconfirmed = r.unconfirmed;
  return c;
}

size_t AppLogic::camRows(CamRow* out, size_t max) const {
  size_t n = nRows_ < max ? nRows_ : max;
  for (size_t i = 0; i < n; i++) out[i] = toCamRow(rows_[i]);
  return n;
}

CamRow AppLogic::aheadRow() const {
  Row r = ahead_;
  if (hasFix()) {  // keep the alert's distance and arrow live as we approach
    double lat = r.latE7 / 1e7, lon = r.lonE7 / 1e7;
    r.distM = (float)haversineM(fix_.lat, fix_.lon, lat, lon);
    r.bearingDeg = (float)bearingDeg(fix_.lat, fix_.lon, lat, lon);
  }
  return toCamRow(r);
}

bool AppLogic::dismissTop() {
  if (trackerOn_) { trackerOn_ = false; return true; }
  if (active(aheadUntil_)) { aheadUntil_ = 0; return true; }
  if (active(markUntil_)) { markUntil_ = 0; return true; }
  if (active(newFlockUntil_)) { newFlockUntil_ = 0; return true; }
  if (active(toastUntil_)) { toastUntil_ = 0; return true; }
  if (active(glassesUntil_)) { glassesUntil_ = 0; return true; }
  return false;
}

Overlay AppLogic::overlay() const {
  if (trackerOn_) return Overlay::TrackerFollowing;
  if (active(aheadUntil_)) return Overlay::CameraAhead;
  if (progressMs_ && now_ - progressMs_ < kProgressGapMs) return Overlay::Marking;
  if (active(markUntil_)) {
    switch (markResult_) {
      case AddResult::Ok: return Overlay::Marked;
      case AddResult::Full: return Overlay::MarksFull;
      default: return Overlay::MarkRefused;
    }
  }
  if (active(newFlockUntil_)) return Overlay::NewFlock;
  if (active(toastUntil_)) return Overlay::Toast;
  if (active(glassesUntil_)) return Overlay::GlassesToast;
  if (mode_ == Mode::Cam) {
    if (!hasFix()) return Overlay::WaitingGps;
    if (!db_.isOpen()) return Overlay::NoCamData;
  }
  return Overlay::None;
}
