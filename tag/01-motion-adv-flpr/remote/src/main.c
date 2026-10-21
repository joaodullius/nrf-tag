/* main.c - FLPR-side ADXL367 sampling and motion decision. Sends state
 * transitions to the cpuapp core over the "motion" ipc_service endpoint
 * (see ../../src/main.c for the receiving side).
 */

/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <math.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>
#include <zephyr/ipc/ipc_service.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

static struct ipc_ept motion_ept;
static bool ipc_bound;

static void ipc_bound_cb(void *priv)
{
	ARG_UNUSED(priv);
	ipc_bound = true;
	LOG_INF("IPC endpoint 'motion' bound to cpuapp");
}

static void ipc_error_cb(const char *message, void *priv)
{
	ARG_UNUSED(priv);
	LOG_ERR("IPC error: %s", message);
}

static const struct ipc_ept_cfg motion_ept_cfg = {
	.name = "motion",
	.cb = {
		.bound = ipc_bound_cb,
		.error = ipc_error_cb,
	},
};

static void send_motion_state(uint8_t state)
{
	int err;

	if (!ipc_bound) {
		LOG_WRN("IPC not bound yet, dropping state=0x%02x", state);
		return;
	}

	err = ipc_service_send(&motion_ept, &state, sizeof(state));
	if (err < 0) {
		LOG_ERR("ipc_service_send failed (err %d)", err);
	}
}

#define MOTION_STATE_STATIONARY 0x00
#define MOTION_STATE_MOVING     0x01

/*
 * Motion detection: continuous data-ready sampling (ODR set to 25 Hz in the
 * board overlay) with a software threshold on acceleration magnitude - see
 * the comment above data_ready_handler() in tag/01-motion-adv/src/main.c
 * for why we don't use the ADXL367's own hardware ACT/INACT engine on this
 * chip. Algorithm ported unchanged; only the side effect differs (send the
 * new state over IPC instead of driving BLE/LED locally).
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

static const struct device *const accel_dev = DEVICE_DT_GET(DT_ALIAS(motion_detector));

static bool is_moving;

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
		is_moving = true;
		LOG_INF("Motion detected -> sending state=0x%02x", MOTION_STATE_MOVING);
		send_motion_state(MOTION_STATE_MOVING);
	} else if (is_moving && hold_count >= MOTION_HOLD_SAMPLES &&
		   still_count >= MOTION_STILL_SAMPLES) {
		is_moving = false;
		LOG_INF("No motion -> sending state=0x%02x", MOTION_STATE_STATIONARY);
		send_motion_state(MOTION_STATE_STATIONARY);
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
	const struct device *ipc_instance = DEVICE_DT_GET(DT_NODELABEL(ipc0));
	int err;

	LOG_INF("Starting FLPR motion sampler");

	err = ipc_service_open_instance(ipc_instance);
	if (err < 0 && err != -EALREADY) {
		LOG_ERR("ipc_service_open_instance failed (err %d)", err);
	}

	err = ipc_service_register_endpoint(ipc_instance, &motion_ept, &motion_ept_cfg);
	if (err) {
		LOG_ERR("ipc_service_register_endpoint failed (err %d)", err);
	}

	err = motion_detector_init();
	if (err) {
		LOG_ERR("Motion detector init failed (err %d)", err);
	}

	for (uint32_t tick = 0;; tick++) {
		k_sleep(K_SECONDS(5));
		LOG_INF("[FLPR PING] alive, tick=%u, is_moving=%d", tick, is_moving);
	}

	return 0;
}
