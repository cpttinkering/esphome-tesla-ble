# Changelog (cpttinkering fork)

Versions use MAJOR.MINOR. 1.0 is based on upstream
[yoziru/esphome-tesla-ble](https://github.com/yoziru/esphome-tesla-ble) 2026.10.3.

## 1.0

### Added

- Charge and departure (preconditioning) schedules, read-only:
  - next/active window sensors, schedule counts and summary text sensors;
  - one diagnostic JSON text sensor per schedule slot, merged into a single
    entity with a `schedules` attribute by
    `home-assistant/tesla_ble_schedules.yaml`;
  - legacy single-schedule fields from the charge state (disabled by default);
  - **Schedule Location Privacy** diagnostic switch, on by default: hides each
    schedule's latitude/longitude.
- Schedules are read on every (re)connect once the car is awake, every
  `schedule_poll_interval`, and on **Force data update**; automatic polls never
  wake the car.
- `packages/repository.yml`: all source locations (repository, ref, library)
  as overridable substitutions. No package hard-codes a repository any more.
- Offline / local installation: `tesla-ble-<board>.local.yml` and
  `packages/dashboard.local.yml` build from unpacked copies of both repositories.

### Changed

- Requires the tesla-ble library v6.4+ (cpttinkering fork).
- `packages/board.yml` no longer adds the library; `library.git.yml` /
  `library.local.yml` do. Include `packages/dashboard.yml` (or a board file)
  rather than `board.yml` directly and nothing changes for you.
- ESPHome project name `cpttinkering.esphome-tesla-ble`, version 1.0.

### Configuration changes (review on upgrade)

New, all optional, with defaults in the packages. Set them in your device
YAML's `substitutions:` only to change the default.

| Substitution | Default | Purpose |
|---|---|---|
| `schedule_poll_interval` | `"1800"` | Seconds between schedule reads; `0` = connect / force update only |
| `schedule_slots` | `"5"` | JSON slot sensors per schedule type (0-16) |
| `tesla_ble_repo_url` | cpttinkering/esphome-tesla-ble | Components repository |
| `tesla_ble_library_url` | cpttinkering/tesla-ble | Library repository |
| `tesla_ble_library_ref` | `v6.4` | Library tag / SHA |
| `tesla_ble_local_path` | `/config/esphome/esphome-tesla-ble` | Local builds only |
| `tesla_ble_library_path` | `/config/esphome/tesla-ble` | Local builds only |
| `tesla_ble_project_name` | `cpttinkering.esphome-tesla-ble` | ESPHome project identity |

Component keys (CLI / custom configs): `tesla_ble_vehicle.schedule_poll_interval`
and `tesla_ble_vehicle.schedule_slots`.

In your device YAML, change the remote package `url:` to
`https://github.com/cpttinkering/esphome-tesla-ble.git`.
