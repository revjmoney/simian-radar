#include <unity.h>
#include "ButtonFsm.h"

void setUp() {}
void tearDown() {}

// Feeds a level every 10 ms from t0 to t1 and returns the last non-None event seen.
static BtnEvent feed(ButtonFsm& b, bool level, uint32_t t0, uint32_t t1, int* doneCount = nullptr) {
  BtnEvent last = BtnEvent::None;
  for (uint32_t t = t0; t <= t1; t += 10) {
    BtnEvent e = b.update(level, t);
    if (e == BtnEvent::HoldDone && doneCount) (*doneCount)++;
    if (e != BtnEvent::None) last = e;
  }
  return last;
}

void test_short_press() {
  ButtonFsm b;
  feed(b, true, 0, 200);
  TEST_ASSERT_EQUAL_INT((int)BtnEvent::ShortPress, (int)feed(b, false, 210, 300));
}
void test_bounce_ignored() {
  ButtonFsm b;
  TEST_ASSERT_EQUAL_INT((int)BtnEvent::None, (int)feed(b, true, 0, 10));
  TEST_ASSERT_EQUAL_INT((int)BtnEvent::None, (int)feed(b, false, 20, 300));
}
void test_hold_done_at_5000() {
  ButtonFsm b;
  TEST_ASSERT_EQUAL_INT((int)BtnEvent::HoldProgress, (int)feed(b, true, 0, 4980));
  TEST_ASSERT_EQUAL_INT((int)BtnEvent::HoldDone, (int)b.update(true, 5000));
}
void test_hold_done_once() {
  ButtonFsm b;
  int done = 0;
  feed(b, true, 0, 8000, &done);
  TEST_ASSERT_EQUAL_INT(1, done);
  TEST_ASSERT_EQUAL_INT((int)BtnEvent::None, (int)feed(b, false, 8010, 8200));
}
void test_release_at_3s_cancels() {
  ButtonFsm b;
  feed(b, true, 0, 3000);
  TEST_ASSERT_EQUAL_INT((int)BtnEvent::None, (int)feed(b, false, 3010, 3300));
}
void test_hold_fraction_midway() {
  ButtonFsm b;
  feed(b, true, 0, 3000);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.5f, b.holdFraction());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_short_press);
  RUN_TEST(test_bounce_ignored);
  RUN_TEST(test_hold_done_at_5000);
  RUN_TEST(test_hold_done_once);
  RUN_TEST(test_release_at_3s_cancels);
  RUN_TEST(test_hold_fraction_midway);
  return UNITY_END();
}
