// Arrival refresh, end to end: the production component and the real TeslaBLE
// library on host stubs (same harness as test_command_results.cpp). Checks the
// glue in tesla_ble_vehicle.cpp: connect / wake triggers, data arrival from the
// library callbacks, that it never wakes the car, never duplicates upstream's
// polls in the same tick, requests only missing groups, and stays bounded.
//
//   make test-native  (or just  make test)

#include "test_helper.h"
#include "tesla_ble_vehicle.h"

#include <map>
#include <string>

using namespace esphome;
using namespace esphome::tesla_ble_vehicle;
using TeslaBLE::WakePolicy;

namespace {

constexpr uint32_t S = 1000;

struct Fixture {
  TeslaBLEVehicle component;
  binary_sensor::BinarySensor asleep;
  std::map<std::string, int> sent;  // poll name -> count
  int wake_capable = 0;

  explicit Fixture(uint32_t refresh_max_age_ms) {
    component.set_vin("5YJ30123456789ABC");
    component.set_binary_sensor("asleep", &asleep);
    component.set_refresh_max_age(refresh_max_age_ms);
    component.set_refresh_retry_max_interval(120 * S);
    component.set_refresh_max_attempts(10);
    component.setup();
    connect();
    drain();
    sent.clear();
  }
  auto &queue() { return component.vehicle_->get_command_queue_for_testing(); }
  void connect() {
    esp_ble_gattc_cb_param_t event{};
    component.gattc_event_handler(ESP_GATTC_REG_FOR_NOTIFY_EVT, 0, &event);
  }
  void disconnect() {
    esp_ble_gattc_cb_param_t event{};
    component.gattc_event_handler(ESP_GATTC_DISCONNECT_EVT, 0, &event);
    component.gattc_event_handler(ESP_GATTC_CLOSE_EVT, 0, &event);
  }
  void drain() {
    while (!queue().empty()) {
      const auto &command = queue().front();
      ++sent[command->name];
      if (command->wake_policy == WakePolicy::WAKE_IF_NEEDED)
        ++wake_capable;
      queue().pop();
    }
  }
  void status(bool sleeping = false) {
    VCSEC_VehicleStatus s = VCSEC_VehicleStatus_init_default;
    s.vehicleSleepStatus = sleeping ? VCSEC_VehicleSleepStatus_E_VEHICLE_SLEEP_STATUS_ASLEEP
                                    : VCSEC_VehicleSleepStatus_E_VEHICLE_SLEEP_STATUS_AWAKE;
    component.vehicle_->vehicle_status_callback_(s);
  }
  // One 10 s tick: VCSEC status arrives, then the component's update().
  void tick(bool sleeping = false) {
    host_millis += 10 * S;
    status(sleeping);
    component.update();
    drain();
  }
  void deliver(bool charge, bool climate, bool drive, bool closures, bool tires) {
    auto *v = component.vehicle_.get();
    if (charge) {
      CarServer_ChargeState s = CarServer_ChargeState_init_default;
      v->charge_state_callback_(s);
    }
    if (climate) {
      CarServer_ClimateState s = CarServer_ClimateState_init_default;
      v->climate_state_callback_(s);
    }
    if (drive) {
      CarServer_DriveState s = CarServer_DriveState_init_default;
      v->drive_state_callback_(s);
    }
    if (closures) {
      CarServer_ClosuresState s = CarServer_ClosuresState_init_default;
      v->closures_state_callback_(s);
    }
    if (tires) {
      CarServer_TirePressureState s = CarServer_TirePressureState_init_default;
      v->tire_pressure_callback_(s);
    }
  }
  void deliver_all() { deliver(true, true, true, true, true); }
  int charge_polls() { return sent["Charge State Poll"]; }
};

// Runs `ticks` 10 s ticks with the car awake and no data answers, returning
// the number of Charge State Polls sent.
int count_unanswered(Fixture &f, int ticks) {
  const int before = f.charge_polls();
  for (int i = 0; i < ticks; ++i)
    f.tick();
  return f.charge_polls() - before;
}

}  // namespace

static void first_awake_tick_sends_one_read() {
  host_millis = 1000 * S;
  Fixture f(600 * S);
  f.tick();  // first fresh awake status: upstream probe + armed refresh
  CHECK(f.charge_polls() == 1);  // the probe poll counts as attempt 1, no duplicate
  CHECK(f.sent["Tire Pressure Poll"] == 1);
  CHECK(f.wake_capable == 0);
}

static void completes_and_then_matches_upstream() {
  host_millis = 1000 * S;
  Fixture on(600 * S), off(0);
  on.tick();
  off.tick();
  on.deliver_all();  // first read answered: refresh complete
  int on_polls = 0, off_polls = 0;
  for (int i = 0; i < 180; ++i) {  // 30 minutes
    on.tick();
    off.tick();
  }
  on_polls = on.charge_polls();
  off_polls = off.charge_polls();
  CHECK(on_polls == off_polls);  // no refresh polls once complete
}

static void retries_only_missing_groups() {
  host_millis = 1000 * S;
  Fixture f(600 * S);
  f.tick();
  f.deliver(true, true, true, false, false);  // closures and tires never answered
  for (int i = 0; i < 30; ++i)  // let upstream's 3 min probe window end
    f.tick();
  f.sent.clear();
  for (int i = 0; i < 12; ++i)  // 2 minutes: at least one refresh retry
    f.tick();
  CHECK(f.sent["Tire Pressure Poll"] >= 1);
  CHECK(f.sent["Closures State Poll"] == f.sent["Tire Pressure Poll"]);
  CHECK(f.sent["Charge State Poll"] == 0);  // charge already arrived, not re-requested
  CHECK(f.sent["Climate State Poll"] == 0);
}

static void bounded_without_answers() {
  host_millis = 1000 * S;
  Fixture on(600 * S), off(0);
  const int with_refresh = count_unanswered(on, 360);  // 1 hour, never answered
  const int without = count_unanswered(off, 360);
  CHECK(with_refresh > without);
  CHECK(with_refresh - without <= 10);  // at most max_attempts extra reads
  // After giving up, a new connect re-arms.
  on.disconnect();
  on.connect();
  on.drain();
  on.sent.clear();
  on.tick();
  CHECK(on.charge_polls() >= 1);
  CHECK(on.wake_capable == 0);
}

static void never_polls_a_sleeping_car() {
  host_millis = 1000 * S;
  Fixture f(600 * S);
  for (int i = 0; i < 180; ++i)
    f.tick(true);
  CHECK(f.charge_polls() == 0);
  CHECK(f.wake_capable == 0);
  // Waking up resumes immediately.
  f.tick(false);
  CHECK(f.charge_polls() == 1);
}

static void fresh_data_on_reconnect_does_nothing_extra() {
  host_millis = 1000 * S;
  Fixture on(600 * S), off(0);
  on.tick();
  off.tick();
  on.deliver_all();
  for (int i = 0; i < 6; ++i) {  // 1 minute later: brief BLE drop
    on.tick();
    off.tick();
  }
  for (Fixture *f : {&on, &off}) {
    f->disconnect();
    f->connect();
    f->drain();
  }
  for (int i = 0; i < 60; ++i) {
    on.tick();
    off.tick();
  }
  CHECK(on.charge_polls() == off.charge_polls());  // data was fresh: no refresh
}

int main() {
  first_awake_tick_sends_one_read();
  completes_and_then_matches_upstream();
  retries_only_missing_groups();
  bounded_without_answers();
  never_polls_a_sleeping_car();
  fresh_data_on_reconnect_does_nothing_extra();
  return test_summary();
}
