# Changelog

English | [简体中文](CHANGELOG.md)

Newest first. Behavior and flashing changes only; pure refactors and comment
cleanups live in the git history.

---

## Documentation system rebuild (bilingual)

Everything this branch changes relative to main, summarized as one
documentation rebuild — no firmware behavior changes:

- **Restructure**: the root README is just the entry point (identity, quick
  start, index, 3D models, license); the details moved into seven
  audience-scoped guides — `USER_GUIDE` (usage), `FLASH` (flashing and
  updates, user-facing), `APP_PROTOCOL` (app integration protocol,
  developer-facing), `VEHICLES` (vehicle profiles), `THEMES` (themes,
  merged from four old theme docs), `TROUBLESHOOTING` (merged from three
  old docs) and `DEVELOPMENT` (development guide, incl. the release
  workflow); the `firmware/` / `themes/` / `theme_store/` directory READMEs
  became slim pointers. Twelve obsolete documents were removed, including
  `docs/BRANCH_COMPARISON.md` describing a two-branch world that no longer
  exists
- **Fixed dangerous stale facts**: some old tables still flashed bootmedia
  to `0x620000` — that address is now the theme_0 partition, so following
  them destroyed the theme partition. Unified to bootmedia `0xA20000` /
  theme `0x620000`, governed by `partitions.csv`. Also corrected the
  vehicle profile count (12 → **17**), the `model/Subaru/brz_zc6/` path,
  the "app partition is 4 MB" claim (it is 3 MB) and similar
- **Bilingual**: all 11 documents now have 1:1 mirrored English editions
  (`X.md` ↔ `X.en.md`) with a language-switch line under the title;
  English editions cross-link via `.en.md`. Chinese remains the source of
  truth; the maintenance rules live in `DEVELOPMENT.md`
- **Documentation-in-code fixes**: `partitions.csv` bootmedia size comment
  7.875 MB → 5.875 MB; `ads1115_oil_pressure.h` header corrected from
  "direct ESP32 ADC" to the ADS1115 I2C ADC; `make_boot_block.py` docstring
  gained the _v2 format; `one_shot.py` / `analyze_proble.py` no longer
  reference the nonexistent `analyze_probe.py` filename

## Theme-engine fixes and direct BMW gear read

- BMW F/G reads the current gear directly via the EGS extended-address DID
  `DA2E` (request header `ATSH6F1`, receive filter `ATCRA618`), replacing
  ratio estimation
- Intake air temperature (IAT) added to the theme data snapshot; fixed the
  "--" tofu on the OBD gear page when no data was available
- Theme engine rolled back to build 118 (regressions from the conditional
  rules engine)
- Fixed CST816 touch I2C pins; touch now runs on its own bus

## Linked RPM warning sync across the cluster

- The master now broadcasts `rpm_warn_linked_en` to slaves, so the three
  gauges no longer need per-gauge setup
- In linked mode the gauges light up in position order and all flash at the
  threshold

## New vehicle profiles (17 built in)

- Added `Supra A90` (B58, OBD oil-pressure DID `4436` replacing ADS1115),
  `BMW E` (N55 oil temp `4402`/`5822` and oil pressure `586F` over the
  `6F1` header), `MINI R55`, `jeep`, and `Honda Integra` (29-bit functional
  addressing `18DB33F1`, CVT with ratio estimation disabled)
- Full capability table in `docs/VEHICLES.md`

## V2/V3 hardware variants (tai_ji_xiao_pai 24/25)

- Build-time hardware-version choice added (menuconfig → OBD DSP
  Configuration): V1 Waveshare board (default) / V2 new board A (ST77916 v1
  init sequence) / V3 new board B (v2 sequence); V2/V3 are direct-GPIO
  boards without TCA9554 / ADS1115

## Theme partition system (theme_0)

- New 4 MB `theme_0` partition (`0x620000`) holding runtime themes:
  manifest + layout.json + 360×360 dial/ring artwork, loaded at boot by
  `theme_engine`, with automatic fallback to the built-in default theme on
  corruption
- **bootmedia moved `0x620000` → `0xA20000`, shrunk 9.875 MB → 5.875 MB**
  (actual usage ~312 KB)
- WiFi OTA gained `/ota/theme`, `/ota/theme/prepare` and `/ota/theme/erase`;
  BLE-side theme transfer is not implemented yet
- Theme-declared pages replace the built-in gauge pages in the carousel
  (saving 40–60 KB RAM); `logo` / `intro` / `boot_video` are protected and
  never themed
- ⚠️ devices on the old partition table must be upgraded with a **one-time
  full USB reflash** — OTA cannot rewrite the partition table

## On-device OTA: BLE service + WiFi transfer

- New **OTA mode screen** (entered via the **OTA button on the version
  page**): publishes the OTA BLE service (`0x1FFB`) and starts a WiFi
  SoftAP (`OBD-Gauge-OTA-XXXX`, password `obd2024`) whose HTTP endpoints
  accept SHA256-verified firmware and boot animations
- Firmware lands in the inactive OTA slot and is marked valid only after a
  15 s boot self-check; early crashes roll back via the bootloader
- Boot-animation updates are transactional (`boot_block.txt.new`/`bin.new`
  staged, then atomically committed); RS485 and ESP-NOW pause during
  transfers
- The version page shows the firmware build tag (branch-commitcount-shorthash,
  injected at build time)

## RaceChrono toggle and simplified boot-animation modes

- New **RACECHRONO** toggle in Settings: OFF = minimal BLE mode (Info + OTA
  services only, no advertising); ON = full RaceChrono + pairing + OTA set
- Boot-animation modes simplified to **OFF / RACE / VIDEO**; VIDEO plays a
  custom animation flashed from the phone app (replacing the old
  REI/SHINJI/ASUKA slots, with legacy values migrated to VIDEO)

## OBD data-path fixes

- The ELM327 client runs **single-threaded**; no more interleaved OBD/CAN
  parallel requests
- Brake-temperature / oil-pressure alarms throttled to one alert per 30 s
- Self-healing data link: re-init + auto-reconnect on stalls — power on and
  drive, no manual reconnect

## OTA dual-slot layout and device manifest

- Partition table moved to `ota_0` + `ota_1` (3 MB each) + `bootmedia` with
  rollback support
- A read-only BLE manifest service (`0x1FFA`) exposes hardware and build
  info for the app's compatibility check; the manifest carries only fields
  the app reads, staying under 512 bytes

## Themes as data (compiled TOML themes)

- Themes moved from C code to **data declarations**: `theme.toml` + artwork
  PNGs under `themes/`, compiled into `ui_theme_generated.c` by
  `tools/gen_themes.py` at CMake configure time
- 8 decorative color roles + 3 optional artwork kinds (bezel/needle/dial)
  under a 1536 KB budget
- `registry.txt` slots are append-only, preventing silent re-skins after OTA
- Semantic colors (warning red etc.) are global and never themed
