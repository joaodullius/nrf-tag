/* main.c - Application main entry point */

/*
 * Copyright (c) 2015-2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/types.h>
#include <stddef.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>

#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

/*
 * Custom Manufacturer Specific Data (AD type 0xFF):
 *   - bytes 0-1: Company Identifier, little-endian over the air.
 *                0x59, 0x00 = 0x0059 = Nordic Semiconductor
 *                (same value as Kconfig CONFIG_BT_COMPANY_ID_NORDIC).
 *   - bytes 2-5: 4 constant bytes — a fixed pattern a scanner can filter on.
 */
static const uint8_t mfg_data[] = {
	0x59, 0x00,             /* Company ID: Nordic Semiconductor (0x0059) */
	0xAA, 0xBB, 0xCC, 0xDD, /* Constant 4-byte pattern for scan filtering */
};

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_NO_BREDR),
	BT_DATA(BT_DATA_MANUFACTURER_DATA, mfg_data, sizeof(mfg_data)),
};

/* Set Scan Response data */
static const struct bt_data sd[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

/* ---- TEST 2: advertising interval — uncomment ONE of the two blocks ---- */

/* Fast interval (~100-150 ms) */
static const struct bt_le_adv_param *adv_param = BT_LE_ADV_PARAM(
	BT_LE_ADV_OPT_USE_IDENTITY,
	BT_GAP_ADV_FAST_INT_MIN_2, BT_GAP_ADV_FAST_INT_MAX_2, NULL);

/* Slow interval (~1000-1200 ms) */
// static const struct bt_le_adv_param *adv_param = BT_LE_ADV_PARAM(
// 	BT_LE_ADV_OPT_USE_IDENTITY,
// 	BT_GAP_ADV_SLOW_INT_MIN, BT_GAP_ADV_SLOW_INT_MAX, NULL);


static void bt_ready(int err)
{
	bt_addr_le_t addr = {0};
	size_t count = 1;

	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
		return;
	}

	printk("Bluetooth initialized\n");

	/* ---- TEST 1 & 3: AD+SD vs AD-only — uncomment ONE of the two lines ---- */

	/* Test 1: TX + RX (AD + SD) — scannable, responds to scan requests */
	err = bt_le_adv_start(adv_param, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));

	/* Test 3: TX only (AD, no SD) — non-scannable, radio RX never turns on */
	// err = bt_le_adv_start(adv_param, ad, ARRAY_SIZE(ad), NULL, 0);
	

	if (err) {
		printk("Advertising failed to start (err %d)\n", err);
		return;
	}


	/* For connectable advertising you would use
	 * bt_le_oob_get_local().  For non-connectable non-identity
	 * advertising an non-resolvable private address is used;
	 * there is no API to retrieve that.
	 */

	bt_id_get(&addr, &count);

	printk("Beacon started, advertising as %s\n", bt_addr_le_str(&addr));
}

int main(void)
{
	int err;

	printk("Starting Beacon Demo\n");

	/* Initialize the Bluetooth Subsystem */
	err = bt_enable(bt_ready);
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
	}
	return 0;
}
