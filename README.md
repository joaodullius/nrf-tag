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

## `central/` — runs on a second nRF54L15 DK

- **`00-scanner`** — BLE scanner that filters on the tags' custom
  manufacturer data and logs each sighting (MAC, RSSI, name, time since
  last seen) over UART.
- **`01-scanner-ansi`** — same scanner with ANSI-colorized terminal
  output.
