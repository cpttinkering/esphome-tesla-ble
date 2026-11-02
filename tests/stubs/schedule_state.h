#pragma once

// Host-test stand-in for the tesla-ble library's schedule_state.h: the same
// structs, without the nanopb-dependent extractor functions. Keep in sync with
// tesla-ble include/schedule_state.h when the structs change.


#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace TeslaBLE {

// Upper bounds keep memory use predictable on small targets.
static constexpr size_t MAX_SCHEDULES = 16;
static constexpr size_t MAX_SCHEDULE_NAME_LENGTH = 32;

// days_of_week is a bitmask: bit 0 = Sunday ... bit 6 = Saturday
// (same encoding as vehicle-command's charge/precondition schedules).
struct ChargeScheduleEntry {
  uint64_t id = 0;  // creation time, epoch seconds; stable identifier
  std::string name;
  int32_t days_of_week = 0;
  bool start_enabled = false;
  int32_t start_time = 0;  // minutes after midnight, local car time
  bool end_enabled = false;
  int32_t end_time = 0;  // minutes after midnight, local car time
  bool one_time = false;
  bool enabled = false;
  float latitude = 0.0f;
  float longitude = 0.0f;
};

struct PreconditionScheduleEntry {
  uint64_t id = 0;
  std::string name;
  int32_t days_of_week = 0;
  int32_t precondition_time = 0;  // departure time, minutes after midnight
  bool one_time = false;
  bool enabled = false;
  float latitude = 0.0f;
  float longitude = 0.0f;
};

struct ChargeScheduleList {
  std::vector<ChargeScheduleEntry> schedules;
  bool truncated = false;  // more than MAX_SCHEDULES entries were present
  bool has_window = false;
  ChargeScheduleEntry window;  // the active / next charge window
  bool has_charge_buffer = false;
  int32_t charge_buffer = 0;
  bool has_max_num_schedules = false;
  uint32_t max_num_schedules = 0;
  bool has_next_schedule = false;
  bool next_schedule = false;
  bool has_show_schedule_complete_state = false;
  bool show_schedule_complete_state = false;
};

struct PreconditionScheduleList {
  std::vector<PreconditionScheduleEntry> schedules;
  bool truncated = false;
  bool has_window = false;
  PreconditionScheduleEntry window;  // the active / next departure
  bool has_max_num_schedules = false;
  uint32_t max_num_schedules = 0;
  bool has_next_schedule = false;
  bool next_schedule = false;
};

}  // namespace TeslaBLE
