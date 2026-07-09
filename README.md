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
2. In `tag/03-memfault/prj.conf`, replace:
   ```
   CONFIG_MEMFAULT_NCS_PROJECT_KEY=""
   ```
   with your project key.
3. Build and flash the tag.
4. Connect with the **nRF Connect Device Manager** app → **Diagnostics**
   tab — this is the BLE-to-cloud gateway (the tag has no modem/Wi-Fi of
   its own). The device ID is generated automatically from each tag's BT
   address.
5. Hold the onboard button for 15 seconds to trigger a deliberate crash
   (`k_oops()`) — the red LED starts warning-blinking fast at the 12s
   mark. After the reboot, reconnect via Diagnostics to upload the
   coredump; an Issue with a full stack trace shows up on the nRFCloud
   dashboard within a minute or two.
