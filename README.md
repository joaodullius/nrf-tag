# nRF54L15-TAG

Firmware modules for a low-power BLE workshop on the nRF54L15-TAG, built
with nRF Connect SDK v3.4.0 (Zephyr 4.4). Each folder is a complete,
independently buildable NCS app — `west build -b nrf54l15tag/nrf54l15/cpuapp`
(tag apps) or the nRF54L15 DK (central apps).

## `tag/` — runs on the nRF54L15-TAG

- **`00-starter`** — non-connectable broadcaster (based on Zephyr's
  `beacon` sample), with toggleable code blocks for experimenting with
  advertising interval and scan-response behavior.
- **`01-motion-adv`** — motion-adaptive advertising: an ADXL367
  accelerometer switches the tag between a fast advertising interval
  while moving and a slow one while stationary, reflected on the RGB LED
  (red = moving, blue = stationary).
- **`01-motion-adv-flpr`** — same motion-adaptive advertising as
  `01-motion-adv`, but the ADXL367 sampling and the moving/stationary
  decision run on the FLPR core (RISC-V VPR coprocessor) instead of the
  app core. The two cores talk over Zephyr's `ipc_service` (icmsg
  backend): the FLPR sends a single state byte on every transition, and
  the app core reacts exactly as `01-motion-adv` does today. See
  `docs/superpowers/specs/2026-07-09-tag-01-motion-adv-flpr-design.md`.
- **`02-mcuboot`** — adds MCUboot (via Sysbuild) and on-demand FOTA:
  holding the onboard button for 4 seconds opens a time-limited
  connectable BLE window (green LED blinking) for firmware updates over
  BT SMP (mcumgr); the tag returns to normal non-connectable broadcasting
  on disconnect or timeout.
- **`03-memfault`** — adds Memfault SDK observability via nRFCloud:
  custom (`motion_transition_count`) and automatic BLE metrics, plus
  coredump-on-crash uploaded over the Memfault Diagnostic Service
  (`BT_MDS`, gatewayed through the nRF Connect Device Manager app). A
  15-second button hold (red LED warning-blinking from 12s) triggers a
  deliberate `k_oops()` to test the fault/coredump flow end-to-end. See
  `docs/superpowers/specs/2026-07-08-tag-03-memfault-design.md`.
- **`04-accel-dppi-drdy`** - reference accelerometer app: the BMI270
  data-ready line (P1.04) starts every SPI read through GPIOTE -> DPPI ->
  SPIM, with no CPU in the acquisition path (ring drained in batches by
  default; one IRQ per sample with `CONFIG_APP_PER_SAMPLE_IRQ`). Based on
  `dppi_works/gpiote_dppi_spim`; validated on the tag at 400 Hz.
- **`05-accel-dppi-power`** - the same mechanism on the nRF54L15 DK
  (GPIOTE30/SPIM30, a generic 11-byte SPI burst) fed by an external
  data-ready generator, to compare current versus rate (100 Hz to 64 kS/s)
  for CPU-in-the-loop, one IRQ per sample and drained ring. The external
  rig is not part of this repo; the results are in its README.
- **`06-neuton-transporte`** - Neuton classifier of parcel transport states
  (idle, shaking, impact, free fall, carrying, in a car, placed) from the
  BMI270 acceleration magnitude, using the Nordic example model (inference
  only).
- **`07-neuton-ventilador`** - the same app with a fan-speed classifier
  (idle, vel1..vel3) trained on the BMI270 vibration with the Edge AI Lab.
  06 and 07 share `main.c`; only `model_cfg.h` and the generated model
  change. Both need the Edge AI Add-on as an extra Zephyr module; see their
  READMEs.
- **`tag/hex/`** - prebuilt, versioned images of `03-memfault` (1.0.0 and
  2.0.0) with the matching symbol files and DFU zips, for a classroom OTA
  demo.

## `central/` — runs on a second nRF54L15 DK

- **`00-scanner`** — BLE scanner that filters on the tags' custom
  manufacturer data and logs each sighting (MAC, RSSI, name, time since
  last seen) over UART.
- **`01-scanner-ansi`** — same scanner with ANSI-colorized terminal
  output.

## Flash / RAM usage (nrf54l15tag/nrf54l15/cpuapp)

Incremental cost of each module, measured on the app image
(`text+data` = Flash, `data+bss` = RAM) via `arm-zephyr-eabi-size`:

| Module | Flash (text+data) | RAM (data+bss) | Δ Flash |
|---|---:|---:|---:|
| `00-starter` | 91,856 B (89.7 KB) | 23,242 B | — |
| `01-motion-adv` | 116,128 B (113.4 KB) | 31,447 B | +24,272 B |
| `02-mcuboot` | 183,284 B (179.0 KB) | 56,393 B | +67,156 B |
| `03-memfault` | 207,672 B (202.8 KB) | 66,341 B | +24,388 B |

`00-starter` and `01-motion-adv` use the full 1524 KB flash region as a
single image (no bootloader). Starting at `02-mcuboot`, Sysbuild adds
MCUboot and splits the region into two 712 KB OTA slots — so the
`01`→`02` jump reflects a smaller flash budget as well as new code
(FOTA/mcumgr support). `03-memfault`'s coredump storage uses a separate,
dedicated 36 KB RRAM partition (`memfault_coredump_partition`), reusing
space the base image layout leaves unused — it isn't counted in the app
image sizes above.

## `03-memfault` setup (instructor)

1. Create a project at [app.memfault.com](https://app.memfault.com)
   (nRFCloud powered by Memfault) and copy its **Project Key**.
2. In `tag/03-memfault/prj.conf`, set `CONFIG_MEMFAULT_NCS_PROJECT_KEY`
   to your project key. The checked-in value is the training project's key
   and is meant to be rotated.
3. Build and flash the tag.
4. Connect with the **nRF Connect Device Manager** app → **Diagnostics**
   tab — this is the BLE-to-cloud gateway (the tag has no modem/Wi-Fi of
   its own). The device ID comes from each tag's hardware ID, and the tag
   advertises as `Tag-XXXX` (last 4 hex digits of that same ID), so the
   phone's device list can be matched with the Memfault dashboard.
5. Hold the onboard button for 15 seconds to trigger a deliberate crash
   (`k_oops()`) — the red LED starts warning-blinking fast at the 12s
   mark. After the reboot, reconnect via Diagnostics to upload the
   coredump; an Issue with a full stack trace shows up on the nRFCloud
   dashboard within a minute or two.
