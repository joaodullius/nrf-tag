/* main.c - Motion-adaptive advertising using the ADXL367 accelerometer. */

/*
 * Copyright (c) 2015-2016 Intel Corporation
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/types.h>
#include <stddef.h>
#include <math.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

/*
 * Custom Manufacturer Specific Data (AD type 0xFF):
 *   - bytes 0-1: Company Identifier, little-endian over the air.
 *                0x59, 0x00 = 0x0059 = Nordic Semiconductor
 *   - bytes 2-4: 3 constant bytes - a fixed pattern a scanner can filter on.
 *   - byte  5:   state byte, updated at runtime (0x00 stationary, 0x01
 *                moving) - a scanner can read this without connecting.
 * mfg_data is not const because the last byte is rewritten on every state
 * transition; ad[] still just points at it, no need to rebuild ad[] itself.
 */
#define MFG_DATA_STATE_IDX   5
#define MFG_STATE_STATIONARY 0x00
#define MFG_STATE_MOVING     0x01

static uint8_t mfg_data[] = {
	0x59, 0x00,
	0xAA, 0xBB, 0xCC,
	MFG_STATE_STATIONARY,
};

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_NO_BREDR),
	BT_DATA(BT_DATA_MANUFACTURER_DATA, mfg_data, sizeof(mfg_data)),
};

static const struct bt_data sd[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

/*
 * Advertising intervals modeled after Apple AirTag's documented behavior
 * (~500 ms while moving, ~2 s while stationary - see "Track You: A Deep
 * Dive into Safety Alerts for Apple AirTags", PETS 2023). Both are well
 * within the 10.24 s legacy advertising interval maximum (HCI field range
 * 0x0020-0x4000), so - unlike our first attempt with a 60 s stationary
 * period - both states just use a continuous native interval; no
 * stop/sleep/restart duty-cycling needed.
 */
static const struct bt_le_adv_param *adv_param_moving = BT_LE_ADV_PARAM(
	BT_LE_ADV_OPT_USE_IDENTITY,
	BT_GAP_MS_TO_ADV_INTERVAL(500), BT_GAP_MS_TO_ADV_INTERVAL(520), NULL);

static const struct bt_le_adv_param *adv_param_stationary = BT_LE_ADV_PARAM(
	BT_LE_ADV_OPT_USE_IDENTITY,
	BT_GAP_MS_TO_ADV_INTERVAL(2000), BT_GAP_MS_TO_ADV_INTERVAL(2020), NULL);

/* RGB LED (individual GPIO channels of LED1): blue = stationary, red = moving */
static const struct gpio_dt_spec led_red = GPIO_DT_SPEC_GET(DT_NODELABEL(led1_red), gpios);
static const struct gpio_dt_spec led_blue = GPIO_DT_SPEC_GET(DT_NODELABEL(led1_blue), gpios);

static const struct device *const accel_dev = DEVICE_DT_GET(DT_ALIAS(motion_detector));

static bool is_moving;

static void set_led(bool moving)
{
	gpio_pin_set_dt(&led_red, moving);
	gpio_pin_set_dt(&led_blue, !moving);
}

static void adv_start(const struct bt_le_adv_param *param)
{
	int err = bt_le_adv_start(param, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));

	if (err) {
		LOG_ERR("Advertising failed to start (err %d)", err);
	}
}

static void enter_moving_state(void)
{
	is_moving = true;
	LOG_INF("Motion detected: continuous advertising, ~500 ms interval");

	(void)bt_le_adv_stop();
	mfg_data[MFG_DATA_STATE_IDX] = MFG_STATE_MOVING;
	set_led(true);
	adv_start(adv_param_moving);
}

static void enter_stationary_state(void)
{
	is_moving = false;
	LOG_INF("No motion: continuous advertising, ~2 s interval");

	(void)bt_le_adv_stop();
	mfg_data[MFG_DATA_STATE_IDX] = MFG_STATE_STATIONARY;
	set_led(false);
	adv_start(adv_param_stationary);
}

/*
 * Motion detection: continuous data-ready sampling (ODR set to 25 Hz in the
 * board overlay) with a software threshold on acceleration magnitude.
 *
 * We tried the ADXL367's own hardware activity/inactivity interrupt first
 * (SENSOR_TRIG_THRESHOLD). Every relevant register was confirmed correctly
 * configured via direct I2C readback (ACT_EN/INACT_EN, referenced mode,
 * LINKLOOP=Linked, autosleep, INTMAP1 routing ACT_INT/INACT_INT to INT1) -
 * it still only ever fired once, at boot, never again despite real,
 * sustained physical motion. Nordic's own Fast Pair Locator Tag sample
 * deliberately avoids hardware ACT/INACT for the ADXL367 too (see
 * CONFIG_APP_MOTION_DETECTOR_ACT_INACT_DETECTION_SUPPORTED in that sample,
 * which only depends on ADXL362) and falls back to continuous sampling +
 * software filtering for this chip - we do the same here.
 *
 * The magnitude of the acceleration vector is used instead of any single
 * axis: at rest, |a| is always ~9.81 m/s^2 regardless of the tag's
 * orientation (it's just gravity), so comparing |a| against a fixed
 * "at rest" band avoids needing any per-orientation reference/calibration.
 *
 * Hysteresis (all configurable below, expressed as a number of samples
 * since the ODR is fixed):
 *   - MOTION_ACTIVE_SAMPLES: how many consecutive over-threshold samples
 *     are needed to confirm motion has *started* (fast, so the tag reacts
 *     quickly once picked up).
 *   - MOTION_HOLD_SAMPLES: once in the moving state, how long to stay
 *     there as a minimum, regardless of brief pauses (e.g. stopping at a
 *     red light while being carried) - avoids flapping back to stationary
 *     too eagerly.
 *   - MOTION_STILL_SAMPLES: how many consecutive *under*-threshold samples
 *     are needed, continuously, to confirm the tag is stationary again.
 */
#define MOTION_MAG_REST_MPS2      9.81f
#define MOTION_MAG_THRESHOLD_MPS2 2.0f

#define MOTION_ODR_HZ         25

#define MOTION_ACTIVE_TIME_MS 120
#define MOTION_ACTIVE_SAMPLES ((MOTION_ACTIVE_TIME_MS * MOTION_ODR_HZ) / 1000)

#define MOTION_HOLD_TIME_S    5
#define MOTION_HOLD_SAMPLES   (MOTION_HOLD_TIME_S * MOTION_ODR_HZ)

#define MOTION_STILL_TIME_S   5
#define MOTION_STILL_SAMPLES  (MOTION_STILL_TIME_S * MOTION_ODR_HZ)

static void data_ready_handler(const struct device *dev, const struct sensor_trigger *trig)
{
	static uint32_t active_count;
	static uint32_t still_count;
	static uint32_t hold_count;
	struct sensor_value xyz[3];
	float x, y, z, mag;
	int err;

	ARG_UNUSED(trig);

	err = sensor_sample_fetch(dev);
	if (err) {
		LOG_ERR("sensor_sample_fetch failed (err %d)", err);
		return;
	}

	err = sensor_channel_get(dev, SENSOR_CHAN_ACCEL_XYZ, xyz);
	if (err) {
		LOG_ERR("sensor_channel_get failed (err %d)", err);
		return;
	}

	x = (float)xyz[0].val1 + (float)xyz[0].val2 / 1000000.0f;
	y = (float)xyz[1].val1 + (float)xyz[1].val2 / 1000000.0f;
	z = (float)xyz[2].val1 + (float)xyz[2].val2 / 1000000.0f;
	mag = sqrtf(x * x + y * y + z * z);

	if (fabsf(mag - MOTION_MAG_REST_MPS2) > MOTION_MAG_THRESHOLD_MPS2) {
		active_count++;
		still_count = 0;
	} else {
		still_count++;
		active_count = 0;
	}

	if (is_moving) {
		hold_count++;
	}

	if (active_count >= MOTION_ACTIVE_SAMPLES && !is_moving) {
		hold_count = 0;
		enter_moving_state();
	} else if (is_moving && hold_count >= MOTION_HOLD_SAMPLES &&
		   still_count >= MOTION_STILL_SAMPLES) {
		enter_stationary_state();
	}
}

static int motion_detector_init(void)
{
	struct sensor_trigger trig = {
		.type = SENSOR_TRIG_DATA_READY,
		.chan = SENSOR_CHAN_ACCEL_XYZ,
	};
	int err;

	if (!device_is_ready(accel_dev)) {
		LOG_ERR("ADXL367 device not ready");
		return -ENODEV;
	}

	LOG_INF("ADXL367 device ready: %s", accel_dev->name);

	err = sensor_trigger_set(accel_dev, &trig, data_ready_handler);
	if (err) {
		LOG_ERR("sensor_trigger_set (data ready) failed (err %d)", err);
	} else {
		LOG_INF("ADXL367 data-ready motion detector armed");
	}

	return err;
}

int main(void)
{
	int err;

	LOG_INF("Starting Motion-Adaptive Advertising Demo");

	gpio_pin_configure_dt(&led_red, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&led_blue, GPIO_OUTPUT_INACTIVE);

	err = bt_enable(NULL);
	if (err) {
		LOG_ERR("Bluetooth init failed (err %d)", err);
		return 0;
	}
	LOG_INF("Bluetooth initialized");

	err = motion_detector_init();
	if (err) {
		LOG_ERR("Motion detector init failed (err %d)", err);
	}

	/* Start in stationary state until the ADXL367 reports motion. */
	enter_stationary_state();

	/* Heartbeat: confirms the firmware is still alive and not hung. */
	for (uint32_t tick = 0;; tick++) {
		k_sleep(K_SECONDS(5));
		LOG_INF("[PING] alive, tick=%u, is_moving=%d", tick, is_moving);
	}

	return 0;
}
