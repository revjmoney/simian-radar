#pragma once
#include <stddef.h>
#include <stdint.h>
#include "ButtonFsm.h"
#include "CamDb.h"
#include "Fix.h"
#include "FollowDetector.h"
#include "MarkStore.h"
#include "Signatures.h"

enum class Mode { Cam, Wifi, Phone, SpyCam, Chan, Attack, Bt, Tags, Bodycam, Drone, Mesh, LoRaWan, ZWave,
                  Wx, Sweep, Sky, Count };
const char* modeTag(Mode m);
bool isSubGhz(Mode m);  // modes that own the SX1262 while on screen
enum class Overlay { None, WaitingGps, NoCamData, CameraAhead, NewFlock, Marking, Marked, MarkRefused,
                     MarksFull, TrackerFollowing, GlassesToast, Toast };

struct CamRow { char dist[8]; const char* brand; int arrowDeg; const char* compass; bool unconfirmed; };

// Everything the device decides, with no hardware: modes, alerts, marks and which screen wins.
class AppLogic {
 public:
  AppLogic(const CamDb& db, MarkStore& marks) : db_(db), marks_(marks) {}
  void setIgnoreCallback(void (*cb)(const uint8_t key[8])) { ignoreCb_ = cb; }

  void onFix(const Fix& f, uint32_t nowMs);
  void onButton(BtnEvent e, float holdFraction, uint32_t nowMs);
  void onFlockHeard(uint32_t nowMs);
  void onFollow(const FollowAlert& a, uint32_t nowMs);
  void onGlasses(BleKind k, int8_t rssi, const uint8_t key[8], uint32_t nowMs);
  void onToast(const char* text, uint32_t nowMs);  // short notice: attacks, GPS guard
  void tick(uint32_t nowMs);

  Mode mode() const { return mode_; }
  Overlay overlay() const;
  size_t camRows(CamRow* out, size_t max) const;
  uint32_t withinCount() const { return within_; }
  bool hasFix() const;
  const Fix& fix() const { return fix_; }

  // Details for the overlay screens.
  CamRow aheadRow() const;
  const FollowAlert& trackerAlert() const { return tracker_; }
  BleKind glassesKind() const { return glassesKind_; }
  float holdFraction() const { return holdFraction_; }
  AddResult markResult() const { return markResult_; }
  const char* toastText() const { return toastText_; }

 private:
  struct Row { int32_t latE7, lonE7; uint8_t brand; float distM, bearingDeg; bool unconfirmed; };
  static const size_t kRows = 4, kAlerted = 32, kGlasses = 16;

  const CamDb& db_;
  MarkStore& marks_;
  void (*ignoreCb_)(const uint8_t key[8]) = nullptr;
  Mode mode_ = Mode::Cam;
  uint32_t now_ = 0;

  Fix fix_ = {};
  uint32_t fixMs_ = 0, rowsFixMs_ = UINT32_MAX;
  Row rows_[kRows] = {};
  size_t nRows_ = 0;
  uint32_t within_ = 0;

  struct Alerted { int32_t latE7, lonE7; };
  Alerted alerted_[kAlerted] = {};
  size_t nAlerted_ = 0;
  Row ahead_ = {};
  uint32_t aheadUntil_ = 0;

  uint32_t newFlockUntil_ = 0, lastNewFlockMs_ = 0;
  bool newFlockEver_ = false;

  uint32_t progressMs_ = 0;
  float holdFraction_ = 0;
  uint32_t markUntil_ = 0;
  AddResult markResult_ = AddResult::Ok;

  bool trackerOn_ = false;
  uint32_t trackerSinceMs_ = 0, holdStartMs_ = 0;
  FollowAlert tracker_ = {};

  struct Glasses { uint8_t key[8]; uint32_t toastedMs; };
  Glasses glasses_[kGlasses] = {};
  size_t nGlasses_ = 0;
  uint32_t glassesUntil_ = 0;
  BleKind glassesKind_ = BleKind::Other;

  char toastText_[24] = "";
  uint32_t toastUntil_ = 0;

  bool active(uint32_t until) const { return until && (int32_t)(until - now_) > 0; }  // wrap-safe
  bool dismissTop();
  void refreshRows();
  CamRow toCamRow(const Row& r) const;
};
