/* main.c - Motion-adaptive advertising (ADXL367) with a 4s button-hold
 * triggered connectable window for FOTA over BT SMP (mcumgr), backed by
 * MCUboot, plus Memfault observability (nRFCloud): BLE/custom metrics,
 * coredump-on-crash, and a 15s button-hold fault trigger for testing it.
 */

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
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/hci.h>

#include <memfault/metrics/metrics.h>

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/sys/printk.h>
#include <hw_id.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

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

/*
 * Device name unique per Tag: "Tag-" + last 4 hex digits of the hardware ID. It is the same ID
 * (hw_id library) that Memfault reports as the device serial, so the phone's device list and the
 * Memfault dashboard can be matched. The buffer is filled at boot, before advertising starts;
 * CONFIG_BT_DEVICE_NAME is only the placeholder used until then.
 */
#define TAG_NAME_PREFIX "Tag-"
#define TAG_NAME_SUFFIX_LEN 4

static char dev_name[sizeof(TAG_NAME_PREFIX) + TAG_NAME_SUFFIX_LEN];

static const struct bt_data sd[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, dev_name, sizeof(dev_name) - 1),
};

static void device_name_init(void)
{
	char id[HW_ID_LEN] = "";
	size_t n;

	snprintk(dev_name, sizeof(dev_name), TAG_NAME_PREFIX "%s", "0000");
	if (hw_id_get(id, sizeof(id)) == 0) {
		n = strlen(id);
		if (n >= TAG_NAME_SUFFIX_LEN) {
			snprintk(dev_name, sizeof(dev_name), TAG_NAME_PREFIX "%s", &id[n - TAG_NAME_SUFFIX_LEN]);
			for (char *c = dev_name + sizeof(TAG_NAME_PREFIX) - 1; *c; c++) {
				*c = toupper((unsigned char)*c);
			}
		}
	}
	(void)bt_set_name(dev_name);
	LOG_INF("Device name %s (hardware id %s)", dev_name, id);
}

/*
 * Advertising intervals modeled after Apple AirTag's documented behavior
 * (~500 ms while moving, ~2 s while stationary - see "Track You: A Deep
 * Dive into Safety Alerts for Apple AirTags", PETS 2023). Both are well
 * within the 10.24 s legacy advertising interval maximum (HCI field range
 * 0x0020-0x4000), so both states just use a continuous native interval;
 * no stop/sleep/restart duty-cycling needed.
 */
static const struct bt_le_adv_param *adv_param_moving = BT_LE_ADV_PARAM(
	BT_LE_ADV_OPT_USE_IDENTITY,
	BT_GAP_MS_TO_ADV_INTERVAL(500), BT_GAP_MS_TO_ADV_INTERVAL(520), NULL);

static const struct bt_le_adv_param *adv_param_stationary = BT_LE_ADV_PARAM(
	BT_LE_ADV_OPT_USE_IDENTITY,
	BT_GAP_MS_TO_ADV_INTERVAL(2000), BT_GAP_MS_TO_ADV_INTERVAL(2020), NULL);

/* Connectable advertising for the FOTA pairing window - standard fast
 * connectable interval (100-150 ms), not the slow broadcast-only ones
 * above, so a scanning phone finds it quickly.
 */
static const struct bt_le_adv_param *adv_param_pairing = BT_LE_ADV_PARAM(
	BT_LE_ADV_OPT_CONN | BT_LE_ADV_OPT_USE_IDENTITY,
	BT_GAP_ADV_FAST_INT_MIN_2, BT_GAP_ADV_FAST_INT_MAX_2, NULL);

#define PAIRING_TIMEOUT_SECONDS 60
#define LED_BLINK_INTERVAL_MS   300
#define BUTTON_HOLD_MS          4000

/* Fault trigger: holding the button well past the FOTA hold (BUTTON_HOLD_MS)
 * deliberately crashes the firmware so Memfault has something to capture and
 * upload as a coredump - there's no second button on the TAG to dedicate to
 * this like the ble_workshop's BTN4 (nRF54L15 DK). FAULT_WARN_MS is when the
 * red LED starts blinking fast as a "let go now" warning; FAULT_HOLD_MS is
 * when k_oops() actually fires. Both are armed from the same button press as
 * BUTTON_HOLD_MS and canceled together on release (see button_pressed).
 */
#define FAULT_WARN_MS                 12000
#define FAULT_HOLD_MS                 15000
#define FAULT_WARN_BLINK_INTERVAL_MS  100

/* RGB LED (individual GPIO channels of LED1): blue = stationary,
 * red = moving, green = FOTA pairing window / connected.
 */
static const struct gpio_dt_spec led_red = GPIO_DT_SPEC_GET(DT_NODELABEL(led1_red), gpios);
static const struct gpio_dt_spec led_blue = GPIO_DT_SPEC_GET(DT_NODELABEL(led1_blue), gpios);
static const struct gpio_dt_spec led_green = GPIO_DT_SPEC_GET(DT_NODELABEL(led1_green), gpios);

static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
static struct gpio_callback button_cb_data;

static const struct device *const accel_dev = DEVICE_DT_GET(DT_ALIAS(motion_detector));

static bool is_moving;

/*
 * Connection state machine:
 *   BROADCASTING - non-connectable motion-adaptive advertising (default).
 *   PAIRING      - button held 4s: connectable advertising, green LED
 *                  blinking, PAIRING_TIMEOUT_SECONDS to connect or revert.
 *   CONNECTED    - a central is connected, green LED solid, mcumgr/BT-SMP
 *                  FOTA available (no custom GATT service - see design
 *                  spec docs/superpowers/specs/2026-07-08-tag-02-mcuboot-
 *                  design.md).
 * A button hold past BUTTON_HOLD_MS is ignored unless currently
 * BROADCASTING - it never interrupts an open pairing window or an active
 * connection. The fault trigger (FAULT_WARN_MS/FAULT_HOLD_MS, see their
 * #defines) is independent of conn_state and fires regardless - see
 * docs/superpowers/specs/2026-07-08-tag-03-memfault-design.md.
 */
enum conn_state {
	STATE_BROADCASTING,
	STATE_PAIRING,
	STATE_CONNECTED,
};

static enum conn_state conn_state = STATE_BROADCASTING;

static struct k_work_delayable button_hold_work;
static struct k_work_delayable pairing_timeout_work;
static struct k_work_delayable led_blink_work;
static bool led_blink_on;

static struct k_work_delayable fault_warn_work;
static struct k_work_delayable fault_work;
static bool fault_warn_on;

static void set_led_motion(bool moving)
{
	gpio_pin_set_dt(&led_green, 0);
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

/* Returns to BROADCASTING and resumes advertising for whatever motion
 * state is current - called on disconnect, on pairing timeout, and once
 * at boot.
 */
static void resume_broadcasting(void)
{
	conn_state = STATE_BROADCASTING;
	set_led_motion(is_moving);

	if (is_moving) {
		LOG_INF("Broadcasting: continuous advertising, ~500 ms interval");
		adv_start(adv_param_moving);
	} else {
		LOG_INF("Broadcasting: continuous advertising, ~2 s interval");
		adv_start(adv_param_stationary);
	}
}

static void led_blink_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	led_blink_on = !led_blink_on;
	gpio_pin_set_dt(&led_green, led_blink_on);
	k_work_schedule(&led_blink_work, K_MSEC(LED_BLINK_INTERVAL_MS));
}

static void pairing_timeout_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	if (conn_state != STATE_PAIRING) {
		return;
	}

	LOG_INF("Pairing window timed out, no central connected");
	(void)k_work_cancel_delayable(&led_blink_work);
	(void)bt_le_adv_stop();
	resume_broadcasting();
}

static void button_hold_expired(struct k_work *work)
{
	int err;

	ARG_UNUSED(work);

	if (conn_state != STATE_BROADCASTING) {
		/* Already pairing or connected - ignore. */
		return;
	}

	LOG_INF("Button held %d ms: opening FOTA pairing window", BUTTON_HOLD_MS);
	conn_state = STATE_PAIRING;

	(void)bt_le_adv_stop();

	err = bt_le_adv_start(adv_param_pairing, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
	if (err) {
		LOG_ERR("Connectable advertising failed to start (err %d)", err);
		resume_broadcasting();
		return;
	}

	gpio_pin_set_dt(&led_red, 0);
	gpio_pin_set_dt(&led_blue, 0);
	led_blink_on = false;
	k_work_schedule(&led_blink_work, K_NO_WAIT);
	k_work_schedule(&pairing_timeout_work, K_SECONDS(PAIRING_TIMEOUT_SECONDS));
}

static void fault_warn_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	fault_warn_on = !fault_warn_on;
	gpio_pin_set_dt(&led_red, fault_warn_on);
	k_work_schedule(&fault_warn_work, K_MSEC(FAULT_WARN_BLINK_INTERVAL_MS));
}

static void fault_triggered(struct k_work *work)
{
	ARG_UNUSED(work);

	(void)k_work_cancel_delayable(&fault_warn_work);
	gpio_pin_set_dt(&led_red, 0);

	LOG_ERR("Button held %d ms: deliberate k_oops() - Memfault will capture a coredump",
		FAULT_HOLD_MS);
	k_oops();
}

/* Cancels the fault-trigger timers and, if the warning blink had already
 * started, clears it. Called on button release, before FAULT_HOLD_MS.
 */
static void fault_sequence_cancel(void)
{
	(void)k_work_cancel_delayable(&fault_warn_work);
	(void)k_work_cancel_delayable(&fault_work);

	gpio_pin_set_dt(&led_red, 0);
	if (conn_state == STATE_BROADCASTING) {
		set_led_motion(is_moving);
	}
}

/*
 * Configured for GPIO_INT_EDGE_BOTH (see button_init): fires on both press
 * and release. On press, arms a BUTTON_HOLD_MS timer (FOTA pairing window)
 * plus the FAULT_WARN_MS/FAULT_HOLD_MS pair (deliberate crash, see comment
 * above their #defines); on release, cancels all three - so neither
 * button_hold_expired() nor fault_triggered() ever run unless the button was
 * held down continuously for the full duration. This also acts as debounce:
 * brief contact noise around either edge doesn't survive a multi-second
 * window.
 */
static void button_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	if (gpio_pin_get_dt(&button) > 0) {
		k_work_schedule(&button_hold_work, K_MSEC(BUTTON_HOLD_MS));
		fault_warn_on = false;
		k_work_schedule(&fault_warn_work, K_MSEC(FAULT_WARN_MS));
		k_work_schedule(&fault_work, K_MSEC(FAULT_HOLD_MS));
	} else {
		(void)k_work_cancel_delayable(&button_hold_work);
		fault_sequence_cancel();
	}
}

static void connected(struct bt_conn *conn, uint8_t err)
{
	ARG_UNUSED(conn);

	if (err) {
		LOG_ERR("Connection failed (err %u)", err);
		return;
	}

	LOG_INF("Central connected: FOTA session active");
	conn_state = STATE_CONNECTED;
	(void)k_work_cancel_delayable(&pairing_timeout_work);
	(void)k_work_cancel_delayable(&led_blink_work);
	gpio_pin_set_dt(&led_red, 0);
	gpio_pin_set_dt(&led_blue, 0);
	gpio_pin_set_dt(&led_green, 1);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	ARG_UNUSED(conn);

	LOG_INF("Central disconnected (reason %u): resuming broadcasting", reason);
	resume_broadcasting();
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
};

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
 *
 * While PAIRING or CONNECTED, motion is still tracked (is_moving keeps
 * updating below) but the LED/advertising are not touched - the green
 * pairing/connected indication takes priority. resume_broadcasting()
 * picks up whatever is_moving ended up being once back in BROADCASTING.
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
		is_moving = true;
		mfg_data[MFG_DATA_STATE_IDX] = MFG_STATE_MOVING;
		LOG_INF("Motion detected");
		memfault_metrics_heartbeat_add(MEMFAULT_METRICS_KEY(motion_transition_count), 1);
		if (conn_state == STATE_BROADCASTING) {
			(void)bt_le_adv_stop();
			set_led_motion(true);
			adv_start(adv_param_moving);
		}
	} else if (is_moving && hold_count >= MOTION_HOLD_SAMPLES &&
		   still_count >= MOTION_STILL_SAMPLES) {
		is_moving = false;
		mfg_data[MFG_DATA_STATE_IDX] = MFG_STATE_STATIONARY;
		LOG_INF("No motion");
		memfault_metrics_heartbeat_add(MEMFAULT_METRICS_KEY(motion_transition_count), 1);
		if (conn_state == STATE_BROADCASTING) {
			(void)bt_le_adv_stop();
			set_led_motion(false);
			adv_start(adv_param_stationary);
		}
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

static int button_init(void)
{
	int err;

	if (!device_is_ready(button.port)) {
		LOG_ERR("Button device not ready");
		return -ENODEV;
	}

	err = gpio_pin_configure_dt(&button, GPIO_INPUT);
	if (err) {
		LOG_ERR("Failed to configure button pin (err %d)", err);
		return err;
	}

	err = gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_BOTH);
	if (err) {
		LOG_ERR("Failed to configure button interrupt (err %d)", err);
		return err;
	}

	gpio_init_callback(&button_cb_data, button_pressed, BIT(button.pin));
	gpio_add_callback(button.port, &button_cb_data);

	return 0;
}

int main(void)
{
	int err;

	LOG_INF("Starting Motion-Adaptive Advertising + FOTA + Memfault Demo");

	gpio_pin_configure_dt(&led_red, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&led_blue, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&led_green, GPIO_OUTPUT_INACTIVE);

	k_work_init_delayable(&button_hold_work, button_hold_expired);
	k_work_init_delayable(&pairing_timeout_work, pairing_timeout_handler);
	k_work_init_delayable(&led_blink_work, led_blink_handler);
	k_work_init_delayable(&fault_warn_work, fault_warn_handler);
	k_work_init_delayable(&fault_work, fault_triggered);

	err = bt_enable(NULL);
	if (err) {
		LOG_ERR("Bluetooth init failed (err %d)", err);
		return 0;
	}
	LOG_INF("Bluetooth initialized");
	device_name_init();

	err = button_init();
	if (err) {
		LOG_ERR("Button init failed (err %d)", err);
	}

	err = motion_detector_init();
	if (err) {
		LOG_ERR("Motion detector init failed (err %d)", err);
	}

	/* Start in stationary state until the ADXL367 reports motion. */
	resume_broadcasting();

	/* Heartbeat: confirms the firmware is still alive and not hung. */
	for (uint32_t tick = 0;; tick++) {
		k_sleep(K_SECONDS(5));
		LOG_INF("[PING] alive, tick=%u, conn_state=%d, is_moving=%d",
			tick, conn_state, is_moving);
	}

	return 0;
}
