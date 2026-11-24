#pragma once

#include <cstdint>

namespace esphome {
namespace tesla_ble_vehicle {

// Data groups of a full infotainment read. Bit flags, so "what is still
// missing" is a single mask.
enum ArrivalRefreshGroup : uint8_t {
  REFRESH_CHARGE = 1 << 0,
  REFRESH_CLIMATE = 1 << 1,
  REFRESH_DRIVE = 1 << 2,
  REFRESH_CLOSURES = 1 << 3,
  REFRESH_TIRES = 1 << 4,
};
static constexpr uint8_t REFRESH_ALL_GROUPS =
    REFRESH_CHARGE | REFRESH_CLIMATE | REFRESH_DRIVE | REFRESH_CLOSURES | REFRESH_TIRES;
static constexpr uint8_t REFRESH_GROUP_COUNT = 5;

// Arrival refresh: one full, success-checked data read when the car comes
// into range (BLE connect) or wakes up while the data is stale.
//
// Pure logic with no ESPHome or tesla-ble dependency so it can be unit-tested
// in isolation (see tests/test_arrival_refresh_policy.cpp).
//
// Behaviour:
//  - a trigger (connect / wake) arms a refresh when the oldest data group is
//    older than max_age (or was never received); fresh data needs nothing
//  - "success" means every group has *arrived* after the trigger; data from
//    regular polls counts too, so the refresh never duplicates a good read
//  - attempts only run while the car is reported awake (never wakes it), and
//    re-request only the groups that are still missing
//  - retries back off: first_retry, x2 each time, capped at retry_max_interval
//  - after max_attempts attempts without success it gives up (bounded, so a
//    group that never answers cannot keep the car awake indefinitely); the
//    next trigger arms a fresh refresh
//  - sleep or BLE loss pauses an armed refresh; the next trigger resumes it
//
// All times are millis() values; elapsed-time arithmetic is unsigned, so it
// stays correct across the ~49 day wraparound.
class ArrivalRefreshPolicy {
 public:
  enum class ActionType : uint8_t { NONE, POLL, GAVE_UP };
  struct Action {
    ActionType type{ActionType::NONE};
    uint8_t groups{0};    // POLL: groups to request; GAVE_UP: groups still missing
    uint8_t attempt{0};   // POLL: attempt number (1-based); GAVE_UP: attempts made
  };
  enum class TriggerResult : uint8_t { FRESH, ARMED, RESUMED };

  void set_max_age_ms(uint32_t ms) { max_age_ms_ = ms; }
  void set_first_retry_ms(uint32_t ms) { first_retry_ms_ = ms; }
  void set_retry_max_interval_ms(uint32_t ms) { retry_max_interval_ms_ = ms; }
  void set_max_attempts(uint8_t attempts) { max_attempts_ = attempts; }

  uint32_t max_age_ms() const { return max_age_ms_; }
  uint8_t max_attempts() const { return max_attempts_; }
  bool armed() const { return armed_; }
  uint8_t missing() const { return armed_ ? missing_ : 0; }

  // A data group arrived (from any poll). Returns true when this completes an
  // armed refresh.
  bool on_data(uint8_t group, uint32_t now_ms) {
    for (uint8_t i = 0; i < REFRESH_GROUP_COUNT; ++i) {
      if (group & (1 << i)) {
        last_arrival_ms_[i] = now_ms;
        received_ |= static_cast<uint8_t>(1 << i);
      }
    }
    if (!armed_)
      return false;
    missing_ &= static_cast<uint8_t>(~group);
    if (missing_ != 0)
      return false;
    armed_ = false;
    return true;
  }

  // Age of the oldest group in ms, or UINT32_MAX if any group was never received.
  uint32_t data_age_ms(uint32_t now_ms) const {
    if (received_ != REFRESH_ALL_GROUPS)
      return UINT32_MAX;
    uint32_t oldest = 0;
    for (uint8_t i = 0; i < REFRESH_GROUP_COUNT; ++i) {
      const uint32_t age = now_ms - last_arrival_ms_[i];
      if (age > oldest)
        oldest = age;
    }
    return oldest;
  }

  // BLE connect or wake-up. A refresh already in progress resumes with an
  // immediate attempt; otherwise one is armed if the data is stale.
  TriggerResult on_trigger(uint32_t now_ms) {
    if (armed_) {
      next_due_immediately_ = true;
      return TriggerResult::RESUMED;
    }
    if (max_age_ms_ == 0 || data_age_ms(now_ms) <= max_age_ms_)
      return TriggerResult::FRESH;
    armed_ = true;
    missing_ = REFRESH_ALL_GROUPS;
    attempts_ = 0;
    retry_delay_ms_ = first_retry_ms_;
    next_due_immediately_ = true;
    return TriggerResult::ARMED;
  }

  // Called every update tick. Returns what to do now.
  Action poll_due(uint32_t now_ms, bool awake) {
    Action action;
    if (!armed_ || !awake)
      return action;
    if (!next_due_immediately_ && now_ms - last_attempt_ms_ < retry_delay_ms_)
      return action;
    if (attempts_ >= max_attempts_) {
      armed_ = false;
      action.type = ActionType::GAVE_UP;
      action.groups = missing_;
      action.attempt = attempts_;
      return action;
    }
    if (!next_due_immediately_) {
      // Back off after the delay that just elapsed.
      const uint32_t doubled = retry_delay_ms_ > UINT32_MAX / 2 ? UINT32_MAX : retry_delay_ms_ * 2;
      retry_delay_ms_ = doubled < retry_max_interval_ms_ ? doubled : retry_max_interval_ms_;
    }
    next_due_immediately_ = false;
    last_attempt_ms_ = now_ms;
    ++attempts_;
    action.type = ActionType::POLL;
    action.groups = missing_;
    action.attempt = attempts_;
    return action;
  }

  // A regular poll requested every group this tick, so the refresh must not
  // send a duplicate right after it. If an attempt was due right now, the
  // regular poll counts as that attempt; otherwise it restarts the retry wait
  // (the answer to that poll may still complete the refresh).
  void on_regular_poll(uint32_t now_ms) {
    if (!armed_)
      return;
    last_attempt_ms_ = now_ms;
    if (next_due_immediately_) {
      next_due_immediately_ = false;
      ++attempts_;
    }
  }

 private:
  uint32_t max_age_ms_{600000};
  uint32_t first_retry_ms_{20000};
  uint32_t retry_max_interval_ms_{120000};
  uint8_t max_attempts_{10};

  uint32_t last_arrival_ms_[REFRESH_GROUP_COUNT]{};
  uint8_t received_{0};

  bool armed_{false};
  uint8_t missing_{0};
  uint8_t attempts_{0};
  uint32_t last_attempt_ms_{0};
  uint32_t retry_delay_ms_{20000};
  bool next_due_immediately_{false};
};

}  // namespace tesla_ble_vehicle
}  // namespace esphome
