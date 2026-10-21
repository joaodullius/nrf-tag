/* main.c - Motion-adaptive advertising, FLPR variant: BLE/LED only. Motion
 * state is driven by IPC events from the FLPR core over the "motion"
 * ipc_service endpoint (see remote/src/main.c for the sending side).
 */

/*
 * Copyright (c) 2015-2016 Intel Corporation
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/types.h>
#include <stddef.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/ipc/ipc_service.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

/*
 * Custom Manufacturer Specific Data (AD type 0xFF) - identical layout to
 * tag/01-motion-adv. byte 5 (state) is set from the IPC message received
 * from the FLPR core instead of a local sensor callback.
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

static const struct bt_le_adv_param *adv_param_moving = BT_LE_ADV_PARAM(
	BT_LE_ADV_OPT_USE_IDENTITY,
	BT_GAP_MS_TO_ADV_INTERVAL(500), BT_GAP_MS_TO_ADV_INTERVAL(520), NULL);

static const struct bt_le_adv_param *adv_param_stationary = BT_LE_ADV_PARAM(
	BT_LE_ADV_OPT_USE_IDENTITY,
	BT_GAP_MS_TO_ADV_INTERVAL(2000), BT_GAP_MS_TO_ADV_INTERVAL(2020), NULL);

/* RGB LED (individual GPIO channels of LED1): blue = stationary, red = moving */
static const struct gpio_dt_spec led_red = GPIO_DT_SPEC_GET(DT_NODELABEL(led1_red), gpios);
static const struct gpio_dt_spec led_blue = GPIO_DT_SPEC_GET(DT_NODELABEL(led1_blue), gpios);

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

static struct ipc_ept motion_ept;

static void motion_bound_cb(void *priv)
{
	ARG_UNUSED(priv);
	LOG_INF("IPC endpoint 'motion' bound to FLPR");
}

static void motion_received_cb(const void *data, size_t len, void *priv)
{
	ARG_UNUSED(priv);

	if (len != 1) {
		LOG_WRN("Unexpected motion IPC payload length: %zu", len);
		return;
	}

	if (*(const uint8_t *)data == MFG_STATE_MOVING) {
		enter_moving_state();
	} else {
		enter_stationary_state();
	}
}

static void motion_error_cb(const char *message, void *priv)
{
	ARG_UNUSED(priv);
	LOG_ERR("IPC error: %s", message);
}

static const struct ipc_ept_cfg motion_ept_cfg = {
	.name = "motion",
	.cb = {
		.bound = motion_bound_cb,
		.received = motion_received_cb,
		.error = motion_error_cb,
	},
};

int main(void)
{
	int err;

	LOG_INF("Starting Motion-Adaptive Advertising Demo (FLPR variant)");

	gpio_pin_configure_dt(&led_red, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&led_blue, GPIO_OUTPUT_INACTIVE);

	err = bt_enable(NULL);
	if (err) {
		LOG_ERR("Bluetooth init failed (err %d)", err);
		return 0;
	}
	LOG_INF("Bluetooth initialized");

	{
		const struct device *ipc_instance = DEVICE_DT_GET(DT_NODELABEL(ipc0));

		err = ipc_service_open_instance(ipc_instance);
		if (err < 0 && err != -EALREADY) {
			LOG_ERR("ipc_service_open_instance failed (err %d)", err);
		}

		err = ipc_service_register_endpoint(ipc_instance, &motion_ept, &motion_ept_cfg);
		if (err) {
			LOG_ERR("ipc_service_register_endpoint failed (err %d)", err);
		}
	}

	/* Start advertising in stationary state regardless of whether IPC
	 * registration above succeeded - fail open, matches the design
	 * spec's error-handling section (the demo shouldn't go silent just
	 * because the FLPR link isn't up).
	 */
	enter_stationary_state();

	for (uint32_t tick = 0;; tick++) {
		k_sleep(K_SECONDS(5));
		LOG_INF("[PING] alive, tick=%u, is_moving=%d", tick, is_moving);
	}

	return 0;
}
