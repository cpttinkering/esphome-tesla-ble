// Behavioural tests for ArrivalRefreshPolicy: arming on stale data, the
// success check (all groups arrived), retry back-off, the attempt cap, pausing
// while asleep, resuming on the next trigger and millis() wraparound.
//
// No ESPHome dependency.
//   make test-cpp  (or just  make test)

#include "test_helper.h"

#include "arrival_refresh_policy.h"

using namespace esphome::tesla_ble_vehicle;
using Type = ArrivalRefreshPolicy::ActionType;
using Trigger = ArrivalRefreshPolicy::TriggerResult;

static constexpr uint32_t S = 1000;

static ArrivalRefreshPolicy make_policy() {
  ArrivalRefreshPolicy p;
  p.set_max_age_ms(600 * S);
  p.set_first_retry_ms(20 * S);
  p.set_retry_max_interval_ms(120 * S);
  p.set_max_attempts(10);
  return p;
}

static void receive_all(ArrivalRefreshPolicy &p, uint32_t now) { p.on_data(REFRESH_ALL_GROUPS, now); }

static void test_fresh_data_does_not_arm() {
  auto p = make_policy();
  receive_all(p, 1000 * S);
  CHECK(p.on_trigger(1500 * S) == Trigger::FRESH);  // 500 s old <= 600 s
  CHECK(!p.armed());
  CHECK(p.poll_due(1500 * S, true).type == Type::NONE);
}

static void test_never_received_arms_and_polls_immediately() {
  auto p = make_policy();
  CHECK(p.data_age_ms(5 * S) == UINT32_MAX);
  CHECK(p.on_trigger(5 * S) == Trigger::ARMED);
  auto a = p.poll_due(5 * S, true);
  CHECK(a.type == Type::POLL);
  CHECK(a.groups == REFRESH_ALL_GROUPS);
  CHECK(a.attempt == 1);
}

static void test_stale_data_arms_and_completes_on_all_groups() {
  auto p = make_policy();
  receive_all(p, 0);
  CHECK(p.on_trigger(3600 * S) == Trigger::ARMED);  // 1 h old
  CHECK(p.poll_due(3600 * S, true).type == Type::POLL);
  CHECK(!p.on_data(REFRESH_CHARGE | REFRESH_CLIMATE | REFRESH_DRIVE, 3601 * S));
  CHECK(p.missing() == (REFRESH_CLOSURES | REFRESH_TIRES));
  // Retry re-requests only what is missing.
  auto retry = p.poll_due(3620 * S, true);
  CHECK(retry.type == Type::POLL);
  CHECK(retry.groups == (REFRESH_CLOSURES | REFRESH_TIRES));
  CHECK(p.on_data(REFRESH_CLOSURES | REFRESH_TIRES, 3621 * S));  // completes
  CHECK(!p.armed());
  CHECK(p.poll_due(3700 * S, true).type == Type::NONE);
}

static void test_one_old_group_makes_data_stale() {
  auto p = make_policy();
  receive_all(p, 0);
  p.on_data(REFRESH_ALL_GROUPS & ~REFRESH_TIRES, 900 * S);  // tires from t=0
  CHECK(p.data_age_ms(1000 * S) == 1000 * S);
  CHECK(p.on_trigger(1000 * S) == Trigger::ARMED);
}

static void test_backoff_timeline_and_give_up() {
  auto p = make_policy();
  p.on_trigger(0);
  // Expected attempt times (s): 0, 20, 60, 140, then every 120.
  const uint32_t expected[10] = {0, 20, 60, 140, 260, 380, 500, 620, 740, 860};
  uint8_t attempts = 0;
  for (uint32_t t = 0; t <= 1000; ++t) {
    auto a = p.poll_due(t * S, true);
    if (a.type == Type::POLL) {
      CHECK(attempts < 10);
      if (attempts < 10)
        CHECK(t == expected[attempts]);
      ++attempts;
      CHECK(a.attempt == attempts);
    } else if (a.type == Type::GAVE_UP) {
      CHECK(t == 980);  // one full interval after the 10th attempt
      CHECK(a.groups == REFRESH_ALL_GROUPS);
      CHECK(a.attempt == 10);
      break;
    }
  }
  CHECK(attempts == 10);
  CHECK(!p.armed());
  CHECK(p.poll_due(2000 * S, true).type == Type::NONE);
  // A new trigger starts over.
  CHECK(p.on_trigger(3000 * S) == Trigger::ARMED);
  CHECK(p.poll_due(3000 * S, true).attempt == 1);
}

static void test_asleep_pauses_and_trigger_resumes() {
  auto p = make_policy();
  p.on_trigger(0);
  CHECK(p.poll_due(0, false).type == Type::NONE);  // asleep at connect: wait
  CHECK(p.poll_due(10 * S, true).attempt == 1);    // first awake tick
  CHECK(p.poll_due(15 * S, false).type == Type::NONE);
  CHECK(p.poll_due(500 * S, false).type == Type::NONE);  // asleep for long: no attempts
  CHECK(p.armed());
  // Wake / reconnect resumes with an immediate attempt, keeping the count.
  CHECK(p.on_trigger(600 * S) == Trigger::RESUMED);
  auto a = p.poll_due(600 * S, true);
  CHECK(a.type == Type::POLL);
  CHECK(a.attempt == 2);
}

static void test_regular_poll_counts_or_defers() {
  auto p = make_policy();
  p.on_trigger(0);
  p.on_regular_poll(0);  // upstream probe poll in the same tick
  CHECK(p.poll_due(0, true).type == Type::NONE);
  CHECK(p.poll_due(19 * S, true).type == Type::NONE);
  auto a = p.poll_due(20 * S, true);
  CHECK(a.type == Type::POLL);
  CHECK(a.attempt == 2);  // the regular poll counted as attempt 1
  // A regular poll mid-backoff restarts the wait without counting.
  p.on_regular_poll(50 * S);
  CHECK(p.poll_due(60 * S, true).type == Type::NONE);  // would have been due at 60
  CHECK(p.poll_due(90 * S, true).attempt == 3);       // 50 + 40
}

static void test_regular_data_completes_without_own_attempt() {
  auto p = make_policy();
  p.on_trigger(0);
  receive_all(p, 1 * S);  // answer to a regular poll
  CHECK(!p.armed());
  CHECK(p.poll_due(1 * S, true).type == Type::NONE);
}

static void test_zero_max_age_disables() {
  auto p = make_policy();
  p.set_max_age_ms(0);
  CHECK(p.on_trigger(0) == Trigger::FRESH);
  CHECK(!p.armed());
}

static void test_wraparound() {
  auto p = make_policy();
  const uint32_t near_wrap = UINT32_MAX - 30 * S;
  receive_all(p, near_wrap);
  CHECK(p.data_age_ms(near_wrap + 100 * S) == 100 * S);  // wrapped
  CHECK(p.on_trigger(near_wrap + 100 * S) == Trigger::FRESH);
  CHECK(p.on_trigger(near_wrap + 700 * S) == Trigger::ARMED);
  CHECK(p.poll_due(near_wrap + 700 * S, true).attempt == 1);
  CHECK(p.poll_due(near_wrap + 719 * S, true).type == Type::NONE);
  CHECK(p.poll_due(near_wrap + 720 * S, true).attempt == 2);
}

int main() {
  test_fresh_data_does_not_arm();
  test_never_received_arms_and_polls_immediately();
  test_stale_data_arms_and_completes_on_all_groups();
  test_one_old_group_makes_data_stale();
  test_backoff_timeline_and_give_up();
  test_asleep_pauses_and_trigger_resumes();
  test_regular_poll_counts_or_defers();
  test_regular_data_completes_without_own_attempt();
  test_zero_max_age_disables();
  test_wraparound();
  return test_summary();
}
