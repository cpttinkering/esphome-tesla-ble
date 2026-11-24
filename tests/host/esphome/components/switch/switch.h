#pragma once
#include <entity.h>
#include <optional>
namespace esphome::switch_ {
struct Switch : host::Entity<bool> {
  virtual void write_state(bool) = 0;
  // Real ESPHome restores from flash per restore mode; on host there is no
  // flash, so tests set the value a fresh device would restore (none = default).
  std::optional<bool> restored_state{};
  std::optional<bool> get_initial_state_with_restore_mode() { return restored_state; }
};
}
