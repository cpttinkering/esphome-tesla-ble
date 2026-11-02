#pragma once

// Text / JSON formatting for charge and departure (preconditioning) schedules.
//
// Header-only and free of ESPHome dependencies so it can be unit-tested on the
// host (tests/test_schedule_format.cpp). The schedule structs come from the
// shared tesla-ble library (schedule_state.h).
//
// Home Assistant limits entity states to 255 characters, so every string built
// here is bounded by MAX_STATE_LENGTH.

#include <cstdint>
#include <cstdio>
#include <string>

#include <schedule_state.h>

namespace esphome {
namespace tesla_ble_vehicle {
namespace schedule_format {

static constexpr size_t MAX_STATE_LENGTH = 255;
static constexpr const char *NONE = "none";

// Day bitmask: bit 0 = Sunday ... bit 6 = Saturday (vehicle-command encoding).
static constexpr int32_t DAYS_ALL = 0x7F;
static constexpr int32_t DAYS_WEEKDAYS = 0x3E;
static constexpr int32_t DAYS_WEEKEND = 0x41;

// Minutes after midnight -> "HH:MM". Out-of-range values yield NONE.
inline std::string hhmm(int32_t minutes) {
  if (minutes < 0 || minutes >= 24 * 60)
    return NONE;
  char buffer[6];
  std::snprintf(buffer, sizeof(buffer), "%02d:%02d", static_cast<int>(minutes / 60), static_cast<int>(minutes % 60));
  return buffer;
}

// Bitmask -> "Mon,Tue,Wed" in Monday-first order (machine friendly, used in JSON).
inline std::string day_list(int32_t days) {
  static const char *const NAMES[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  static const int ORDER[7] = {1, 2, 3, 4, 5, 6, 0};
  std::string out;
  for (int bit : ORDER) {
    if (days & (1 << bit)) {
      if (!out.empty())
        out += ',';
      out += NAMES[bit];
    }
  }
  return out;
}

// Bitmask -> human text: "Every day", "Weekdays", "Weekend", or the list.
inline std::string day_text(int32_t days, bool one_time) {
  if (one_time)
    return "Once";
  switch (days & DAYS_ALL) {
    case 0:
      return NONE;
    case DAYS_ALL:
      return "Every day";
    case DAYS_WEEKDAYS:
      return "Weekdays";
    case DAYS_WEEKEND:
      return "Weekend";
    default:
      return day_list(days);
  }
}

// Minimal JSON string escaping (quotes, backslash, control characters).
inline std::string json_escape(const std::string &in) {
  std::string out;
  out.reserve(in.size() + 2);
  for (unsigned char c : in) {
    switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      default:
        if (c < 0x20) {
          char buffer[7];
          std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
          out += buffer;
        } else {
          out += static_cast<char>(c);
        }
    }
  }
  return out;
}

inline const char *json_bool(bool value) { return value ? "true" : "false"; }

inline std::string json_location(float latitude, float longitude) {
  char buffer[48];
  std::snprintf(buffer, sizeof(buffer), ",\"lat\":%.5f,\"lon\":%.5f", static_cast<double>(latitude),
                static_cast<double>(longitude));
  return buffer;
}

// Builds a JSON object, shortening the name if the result would exceed
// MAX_STATE_LENGTH. Returns an empty string if even an empty name does not fit.
// The raw name is shortened (on a UTF-8 boundary) and re-escaped each round,
// so an escape sequence is never cut in half.
template<typename Builder> std::string bounded_json(std::string name, Builder build) {
  for (;;) {
    std::string json = build(json_escape(name));
    if (json.size() <= MAX_STATE_LENGTH)
      return json;
    if (name.empty())
      return "";
    name.pop_back();
    while (!name.empty() && (static_cast<unsigned char>(name.back()) & 0x80) &&
           (static_cast<unsigned char>(name.back()) & 0xC0) != 0xC0)
      name.pop_back();  // drop continuation bytes of a partially removed character
    if (!name.empty() && (static_cast<unsigned char>(name.back()) & 0xC0) == 0xC0)
      name.pop_back();  // and its lead byte
  }
}

// One charge schedule as compact JSON for a slot text sensor.
inline std::string charge_json(const TeslaBLE::ChargeScheduleEntry &s, bool include_location) {
  return bounded_json(s.name, [&](const std::string &name) {
    std::string json = "{\"id\":" + std::to_string(s.id) + ",\"name\":\"" + name + "\"";
    json += ",\"days\":\"" + day_list(s.days_of_week) + "\"";
    json += ",\"start\":\"" + (s.start_enabled ? hhmm(s.start_time) : std::string(NONE)) + "\"";
    json += ",\"end\":\"" + (s.end_enabled ? hhmm(s.end_time) : std::string(NONE)) + "\"";
    json += std::string(",\"one_time\":") + json_bool(s.one_time);
    json += std::string(",\"enabled\":") + json_bool(s.enabled);
    if (include_location)
      json += json_location(s.latitude, s.longitude);
    return json + "}";
  });
}

// One departure (preconditioning) schedule as compact JSON for a slot text sensor.
inline std::string departure_json(const TeslaBLE::PreconditionScheduleEntry &s, bool include_location) {
  return bounded_json(s.name, [&](const std::string &name) {
    std::string json = "{\"id\":" + std::to_string(s.id) + ",\"name\":\"" + name + "\"";
    json += ",\"days\":\"" + day_list(s.days_of_week) + "\"";
    json += ",\"departure\":\"" + hhmm(s.precondition_time) + "\"";
    json += std::string(",\"one_time\":") + json_bool(s.one_time);
    json += std::string(",\"enabled\":") + json_bool(s.enabled);
    if (include_location)
      json += json_location(s.latitude, s.longitude);
    return json + "}";
  });
}

inline std::string display_name(const std::string &name) { return name.empty() ? "Unnamed" : name; }

inline std::string bounded(std::string text) {
  if (text.size() <= MAX_STATE_LENGTH)
    return text;
  size_t length = MAX_STATE_LENGTH;
  while (length > 0 && (static_cast<unsigned char>(text[length]) & 0xC0) == 0x80)
    --length;
  text.resize(length);
  return text;
}

template<typename List> size_t enabled_count(const List &list) {
  size_t count = 0;
  for (const auto &s : list.schedules)
    count += s.enabled ? 1 : 0;
  return count;
}

inline std::string charge_window_text(const TeslaBLE::ChargeScheduleEntry &s) {
  std::string start = s.start_enabled ? hhmm(s.start_time) : "";
  std::string end = s.end_enabled ? hhmm(s.end_time) : "";
  std::string range = start.empty() ? "until " + end : end.empty() ? "from " + start : start + "-" + end;
  return display_name(s.name) + " " + range + " " + day_text(s.days_of_week, s.one_time);
}

// e.g. "2 of 3 enabled; next: Home night 23:00-07:00 Weekdays"
inline std::string charge_summary(const TeslaBLE::ChargeScheduleList &list) {
  if (list.schedules.empty() && !list.has_window)
    return "No schedules";
  std::string text = std::to_string(enabled_count(list)) + " of " + std::to_string(list.schedules.size()) +
                     " enabled";
  if (list.has_window)
    text += "; next: " + charge_window_text(list.window);
  return bounded(text);
}

// e.g. "1 of 2 enabled; next: Work 07:45 Weekdays"
inline std::string departure_summary(const TeslaBLE::PreconditionScheduleList &list) {
  if (list.schedules.empty() && !list.has_window)
    return "No schedules";
  std::string text = std::to_string(enabled_count(list)) + " of " + std::to_string(list.schedules.size()) +
                     " enabled";
  if (list.has_window) {
    const auto &w = list.window;
    text += "; next: " + display_name(w.name) + " " + hhmm(w.precondition_time) + " " +
            day_text(w.days_of_week, w.one_time);
  }
  return bounded(text);
}

}  // namespace schedule_format
}  // namespace tesla_ble_vehicle
}  // namespace esphome
