// Behavioural tests for schedule_format.h: time/day text, slot JSON (incl. the
// location privacy switch and the 255-character Home Assistant state limit)
// and the summary sensors.
//
// No ESPHome dependency; tests/stubs provides the tesla-ble schedule structs.
//   make test-cpp  (or just  make test)

#include "test_helper.h"

#include "schedule_format.h"

#include <string>

using namespace esphome::tesla_ble_vehicle;
namespace fmt = esphome::tesla_ble_vehicle::schedule_format;

static bool contains(const std::string &haystack, const std::string &needle) {
  return haystack.find(needle) != std::string::npos;
}

static TeslaBLE::ChargeScheduleEntry charge_entry() {
  TeslaBLE::ChargeScheduleEntry s;
  s.id = 1700000000;
  s.name = "Home night";
  s.days_of_week = fmt::DAYS_WEEKDAYS;
  s.start_enabled = true;
  s.start_time = 23 * 60;
  s.end_enabled = true;
  s.end_time = 7 * 60;
  s.enabled = true;
  s.latitude = 53.16123f;
  s.longitude = 6.79456f;
  return s;
}

static void test_time_and_days() {
  CHECK(fmt::hhmm(0) == "00:00");
  CHECK(fmt::hhmm(7 * 60 + 5) == "07:05");
  CHECK(fmt::hhmm(23 * 60 + 59) == "23:59");
  CHECK(fmt::hhmm(24 * 60) == "none");
  CHECK(fmt::hhmm(-1) == "none");

  CHECK(fmt::day_list(fmt::DAYS_ALL) == "Mon,Tue,Wed,Thu,Fri,Sat,Sun");
  CHECK(fmt::day_list(1) == "Sun");          // bit 0 = Sunday
  CHECK(fmt::day_list(2 | 64) == "Mon,Sat");  // Monday-first order
  CHECK(fmt::day_text(fmt::DAYS_ALL, false) == "Every day");
  CHECK(fmt::day_text(fmt::DAYS_WEEKDAYS, false) == "Weekdays");
  CHECK(fmt::day_text(fmt::DAYS_WEEKEND, false) == "Weekend");
  CHECK(fmt::day_text(8, false) == "Wed");
  CHECK(fmt::day_text(fmt::DAYS_ALL, true) == "Once");
  CHECK(fmt::day_text(0, false) == "none");
}

static void test_charge_json_and_privacy() {
  const auto s = charge_entry();
  const std::string hidden = fmt::charge_json(s, false);
  CHECK(hidden == "{\"id\":1700000000,\"name\":\"Home night\",\"days\":\"Mon,Tue,Wed,Thu,Fri\","
                  "\"start\":\"23:00\",\"end\":\"07:00\",\"one_time\":false,\"enabled\":true}");
  CHECK(!contains(hidden, "lat"));

  const std::string shown = fmt::charge_json(s, true);
  CHECK(contains(shown, "\"lat\":53.16123"));
  CHECK(contains(shown, "\"lon\":6.79456"));

  auto open_end = s;
  open_end.end_enabled = false;
  CHECK(contains(fmt::charge_json(open_end, false), "\"end\":\"none\""));
}

static void test_departure_json() {
  TeslaBLE::PreconditionScheduleEntry s;
  s.id = 42;
  s.name = "Work";
  s.days_of_week = 2;
  s.precondition_time = 7 * 60 + 45;
  s.one_time = true;
  s.enabled = true;
  CHECK(fmt::departure_json(s, false) ==
        "{\"id\":42,\"name\":\"Work\",\"days\":\"Mon\",\"departure\":\"07:45\",\"one_time\":true,\"enabled\":true}");
}

static void test_json_escaping_and_length_limit() {
  auto s = charge_entry();
  s.name = "Say \"hi\"\\\n";
  CHECK(contains(fmt::charge_json(s, false), "\"name\":\"Say \\\"hi\\\"\\\\\\u000a\""));

  // Control characters escape to 6 bytes each: the name is shortened until
  // the JSON fits HA's 255-character state, never splitting an escape.
  s.name = std::string(64, '\x01');
  const std::string json = fmt::charge_json(s, true);
  CHECK(!json.empty());
  CHECK(json.size() <= fmt::MAX_STATE_LENGTH);
  CHECK(json.back() == '}');
  CHECK(contains(json, "\\u0001\""));  // last kept escape is complete

  // Multi-byte UTF-8 is cut on a character boundary.
  s.name.clear();
  for (int i = 0; i < 120; ++i)
    s.name += "\xC3\xA9";  // é
  const std::string utf8 = fmt::charge_json(s, true);
  CHECK(utf8.size() <= fmt::MAX_STATE_LENGTH);
  const size_t name_end = utf8.find("\",\"days\"");
  CHECK(name_end != std::string::npos);
  CHECK((static_cast<unsigned char>(utf8[name_end - 1]) & 0xC0) == 0x80);  // ends on é's 2nd byte
  CHECK((static_cast<unsigned char>(utf8[name_end - 2]) & 0xC0) == 0xC0);
}

static void test_summaries() {
  TeslaBLE::ChargeScheduleList charge;
  CHECK(fmt::charge_summary(charge) == "No schedules");

  charge.schedules.push_back(charge_entry());
  auto disabled = charge_entry();
  disabled.enabled = false;
  charge.schedules.push_back(disabled);
  CHECK(fmt::charge_summary(charge) == "1 of 2 enabled");

  charge.has_window = true;
  charge.window = charge_entry();
  CHECK(fmt::charge_summary(charge) == "1 of 2 enabled; next: Home night 23:00-07:00 Weekdays");

  charge.window.start_enabled = false;
  CHECK(fmt::charge_summary(charge) == "1 of 2 enabled; next: Home night until 07:00 Weekdays");

  TeslaBLE::PreconditionScheduleList departure;
  CHECK(fmt::departure_summary(departure) == "No schedules");
  TeslaBLE::PreconditionScheduleEntry d;
  d.name = "";
  d.days_of_week = fmt::DAYS_ALL;
  d.precondition_time = 8 * 60;
  d.enabled = true;
  departure.schedules.push_back(d);
  departure.has_window = true;
  departure.window = d;
  CHECK(fmt::departure_summary(departure) == "1 of 1 enabled; next: Unnamed 08:00 Every day");
}

int main() {
  test_time_and_days();
  test_charge_json_and_privacy();
  test_departure_json();
  test_json_escaping_and_length_limit();
  test_summaries();
  return test_summary();
}
