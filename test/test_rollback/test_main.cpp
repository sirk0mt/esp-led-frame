#include <unity.h>

#include "app/RollbackGuard.h"

using ledframe::RollbackGuard;
using Action = ledframe::RollbackGuard::Action;

#define ASSERT_ACTION(expected, actual) \
  TEST_ASSERT_EQUAL_INT(static_cast<int>(expected), static_cast<int>(actual))

void setUp() {}
void tearDown() {}

void test_keeps_firmware_after_the_wait_with_progress() {
  RollbackGuard guard;
  uint32_t beats = 0;
  for (uint32_t ms = 0; ms < RollbackGuard::kKeepAfterMs; ms += 1000) {
    ASSERT_ACTION(Action::kWait, guard.update(ms, beats += 50));
  }
  TEST_ASSERT_FALSE(guard.done());
  ASSERT_ACTION(Action::kKeep, guard.update(RollbackGuard::kKeepAfterMs, beats += 50));
  TEST_ASSERT_TRUE(guard.done());
}

void test_reports_only_once() {
  RollbackGuard guard;
  guard.update(0, 1);
  ASSERT_ACTION(Action::kKeep, guard.update(RollbackGuard::kKeepAfterMs, 2));
  ASSERT_ACTION(Action::kWait, guard.update(RollbackGuard::kKeepAfterMs + 1000, 3));
  // Kept means kept: no roll back later, whatever the render task does.
  ASSERT_ACTION(Action::kWait, guard.update(RollbackGuard::kGiveUpMs + 1000, 3));
  TEST_ASSERT_TRUE(guard.done());
}

void test_waits_while_the_render_task_stands_still() {
  RollbackGuard guard;
  guard.update(0, 7);
  // A stopped render task: the counter stays, so the firmware is not kept.
  for (uint32_t ms = 1000; ms < RollbackGuard::kGiveUpMs; ms += 1000) {
    ASSERT_ACTION(Action::kWait, guard.update(ms, 7));
  }
  TEST_ASSERT_FALSE(guard.done());
}

void test_rolls_back_when_never_kept() {
  RollbackGuard guard;
  guard.update(0, 7);
  for (uint32_t ms = 1000; ms < RollbackGuard::kGiveUpMs; ms += 1000) {
    guard.update(ms, 7);
  }
  ASSERT_ACTION(Action::kRollBack, guard.update(RollbackGuard::kGiveUpMs, 7));
  TEST_ASSERT_TRUE(guard.done());
  ASSERT_ACTION(Action::kWait, guard.update(RollbackGuard::kGiveUpMs + 1000, 8));
}

void test_late_progress_before_giving_up_keeps() {
  RollbackGuard guard;
  guard.update(0, 7);
  for (uint32_t ms = 1000; ms < RollbackGuard::kGiveUpMs - 1000; ms += 1000) {
    guard.update(ms, 7);
  }
  // The render task runs again just before the limit: kept.
  ASSERT_ACTION(Action::kKeep, guard.update(RollbackGuard::kGiveUpMs - 1000, 8));
}

void test_progress_at_the_limit_keeps() {
  // Progress wins over the limit: a check that comes late still keeps a working firmware.
  RollbackGuard guard;
  guard.update(0, 1);
  ASSERT_ACTION(Action::kKeep, guard.update(RollbackGuard::kGiveUpMs + 5000, 2));
}

void test_first_call_after_the_wait_only_records() {
  // Without an earlier value there is no progress to compare with.
  RollbackGuard guard;
  ASSERT_ACTION(Action::kWait, guard.update(RollbackGuard::kKeepAfterMs + 5000, 100));
  ASSERT_ACTION(Action::kKeep, guard.update(RollbackGuard::kKeepAfterMs + 6000, 101));
}

void test_counter_wrap_counts_as_progress() {
  RollbackGuard guard;
  guard.update(RollbackGuard::kKeepAfterMs, 0xFFFFFFFFu);
  ASSERT_ACTION(Action::kKeep, guard.update(RollbackGuard::kKeepAfterMs + 1000, 0));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_keeps_firmware_after_the_wait_with_progress);
  RUN_TEST(test_reports_only_once);
  RUN_TEST(test_waits_while_the_render_task_stands_still);
  RUN_TEST(test_rolls_back_when_never_kept);
  RUN_TEST(test_late_progress_before_giving_up_keeps);
  RUN_TEST(test_progress_at_the_limit_keeps);
  RUN_TEST(test_first_call_after_the_wait_only_records);
  RUN_TEST(test_counter_wrap_counts_as_progress);
  return UNITY_END();
}
