# Changelog (cpttinkering fork)

Versions use MAJOR.MINOR. 2.0 is based on upstream
[yoziru/esphome-tesla-ble](https://github.com/yoziru/esphome-tesla-ble) 2026.10.4
(1.x on 2026.10.3).

## 2.1

### Added

- Arrival refresh: on BLE connect or wake-up, data older than
  `refresh_max_age` is read in full and retried (20 s, 40 s, 80 s, then every
  `refresh_retry_max_interval`) until every data group has arrived, requesting
  only missing groups; gives up after `refresh_max_attempts` with an error log
  until the next connect / wake-up. Never wakes the car; upstream's awake probe
  and idle timer are unchanged.
- `packages/fork_defaults.yml`: the single place for the fork's own setting
  defaults, included by `dashboard.yml`, `dashboard.local.yml` and `base.yml`.

### Changed

- The schedule defaults moved from `dashboard.yml`, `dashboard.local.yml` and
  `client_defaults.yml` to `fork_defaults.yml` (same values).

### Configuration changes (review on upgrade)

New, all optional; defaults in `packages/fork_defaults.yml`.

| Substitution | Default | Purpose |
|---|---|---|
| `refresh_max_age` | `"600"` | Re-read data older than this on connect / wake (s, 0 = off) |
| `refresh_retry_max_interval` | `"120"` | Back-off cap between attempts (s) |
| `refresh_max_attempts` | `"10"` | Attempts before giving up until the next connect / wake |

Component keys (CLI / custom configs): `tesla_ble_vehicle.refresh_max_age`,
`refresh_retry_max_interval`, `refresh_max_attempts`. Offline builds: replace
the `esphome-tesla-ble/` folder (library unchanged, 7.0).

## 2.0

### Changed

- Merged upstream 2026.10.4. Main behaviour change from upstream: after a
  (re)connect, the car's status is requested first, and a fresh "awake"
  report opens a bounded three-minute window of awake-cadence data polling
  (shared eleven-minute cooldown). Automatic polling still never wakes the car.
- Requires the cpttinkering tesla-ble library 7.0 (upstream 6.4.0 merged; the
  fork's schedule-list API renamed so upstream's stays unchanged).
- Schedule polling uses the same fresh-status "awake" test as data polling.
- Host tests: the native component test (from upstream) now also builds the
  schedule code; its switch stub supports restore-mode lookups.

### Configuration changes (review on upgrade)

| Substitution | Old default | New default |
|---|---|---|
| `tesla_ble_library_ref` | `v6.5` | `v7.0` |

Offline builds: replace both folders (`esphome-tesla-ble/`, `tesla-ble/`)
with the 2.0 / 7.0 copies. Nothing to change in your device YAML.

## 1.1

### Fixed

- Crash (stack overflow in loopTask) when schedules were received. Needs the
  tesla-ble library 6.5, which decodes schedule responses outside the deep
  message handler.

### Configuration changes (review on upgrade)

| Substitution | Old default | New default |
|---|---|---|
| `tesla_ble_library_ref` | `v6.4` | `v6.5` |

Offline builds: replace the `tesla-ble/` folder with the 6.5 copy.

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
