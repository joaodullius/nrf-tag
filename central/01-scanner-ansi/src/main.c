/*
 * main.c - BLE scanner filtering for the nRF54L15-TAG's custom
 * manufacturer data (Nordic company ID + our fixed pattern prefix).
 *
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/printk.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/*
 * Filter: Nordic company ID (0x0059, little-endian: 0x59, 0x00) followed by
 * the first two bytes of our fixed pattern (0xAA, 0xBB) - see the mfg_data[]
 * arrays in tag/00-starter/src/main.c and tag/01-motion-adv/src/main.c.
 */
#define FILTER_BYTE_0 0x59
#define FILTER_BYTE_1 0x00
#define FILTER_BYTE_2 0xAA
#define FILTER_BYTE_3 0xBB
#define FILTER_LEN    4

/* Continuous active scanning (window == interval), no duplicate filtering:
 * we want every single advertising report, not a deduplicated summary, so
 * we can measure real inter-arrival timing per tag. Active scanning also
 * requests the scan response, which carries the device name.
 */
static const struct bt_le_scan_param scan_param = {
	.type = BT_LE_SCAN_TYPE_ACTIVE,
	.options = BT_LE_SCAN_OPT_NONE,
	.interval = BT_GAP_SCAN_FAST_INTERVAL,
	.window = BT_GAP_SCAN_FAST_INTERVAL,
};

#define MAX_TRACKED_TAGS 20
#define MAX_NAME_LEN     32

struct tracked_tag {
	bt_addr_le_t addr;
	bool in_use;
	int32_t last_seen_ms;
	char name[MAX_NAME_LEN];
};

static struct tracked_tag tracked[MAX_TRACKED_TAGS];

/*
 * Dashboard: one fixed screen row per tracked tag slot, rewritten in place
 * on every matching packet instead of scrolling the log. Requires a
 * terminal that understands ANSI/VT100 escape codes (PuTTY, Tera Term,
 * `screen`, minicom, Windows Terminal - not verified to work in the
 * nRF Connect Serial Terminal, try it and fall back to a plain terminal
 * app if it doesn't render correctly).
 *
 * Layout (screen rows, 1-indexed):
 *   1: title
 *   2: status/warnings line (table full, etc.)
 *   3: column header
 *   4..4+MAX_TRACKED_TAGS-1: one row per tag slot
 */
#define DASHBOARD_STATUS_ROW    2
#define DASHBOARD_HEADER_ROW    3
#define DASHBOARD_FIRST_TAG_ROW 4

static void ansi_goto(int row, int col)
{
	printk("\033[%d;%dH", row, col);
}

static void ansi_clear_line(void)
{
	printk("\033[2K");
}

static void dashboard_init(void)
{
	printk("\033[2J\033[H"); /* clear screen, cursor to top-left */
	printk("TAG Scanner - filtering company ID 0x0059, pattern AA BB\n");

	ansi_goto(DASHBOARD_HEADER_ROW, 1);
	printk("    %-27s | %-20s | %4s | %-20s | %-7s | %s",
	       "Address", "Name", "RSSI", "Manufacturer Data", "Delta", "Last seen");

	for (int i = 0; i < MAX_TRACKED_TAGS; i++) {
		ansi_goto(DASHBOARD_FIRST_TAG_ROW + i, 1);
		printk("%2d) --", i);
	}

	ansi_goto(DASHBOARD_FIRST_TAG_ROW + MAX_TRACKED_TAGS, 1);
}

static void dashboard_status(const char *msg)
{
	ansi_goto(DASHBOARD_STATUS_ROW, 1);
	ansi_clear_line();
	printk("%s", msg);
}

static void dashboard_update_row(int slot, const char *addr_str, const char *name,
				  int8_t rssi, const char *mfg_hex, int32_t delta_ms,
				  int32_t now_ms)
{
	ansi_goto(DASHBOARD_FIRST_TAG_ROW + slot, 1);
	ansi_clear_line();

	if (delta_ms >= 0) {
		printk("%2d) %-27s | %-20s | %4d | %-20s | %d ms | %d ms",
		       slot, addr_str, name, rssi, mfg_hex, delta_ms, now_ms);
	} else {
		printk("%2d) %-27s | %-20s | %4d | %-20s | -- | %d ms",
		       slot, addr_str, name, rssi, mfg_hex, now_ms);
	}
}

/* Look up an already-tracked entry. Does NOT create one - we only start
 * tracking a device once it has matched our manufacturer-data filter (see
 * scan_cb), otherwise every phone/laptop/earbud advertising nearby would
 * fill the table within a couple of seconds.
 */
static struct tracked_tag *tag_lookup(const bt_addr_le_t *addr)
{
	for (int i = 0; i < MAX_TRACKED_TAGS; i++) {
		if (tracked[i].in_use && bt_addr_le_eq(&tracked[i].addr, addr)) {
			return &tracked[i];
		}
	}
	return NULL;
}

static struct tracked_tag *tag_add(const bt_addr_le_t *addr)
{
	for (int i = 0; i < MAX_TRACKED_TAGS; i++) {
		if (!tracked[i].in_use) {
			tracked[i].in_use = true;
			tracked[i].addr = *addr;
			tracked[i].last_seen_ms = -1;
			tracked[i].name[0] = '\0';
			return &tracked[i];
		}
	}

	dashboard_status("WARNING: tracked tag table full, dropping a new matching tag");
	return NULL;
}

struct ad_parse_result {
	bool mfg_matched;
	const uint8_t *mfg_data;
	uint8_t mfg_data_len;
	bool has_name;
	char name[MAX_NAME_LEN];
};

static bool ad_parse_cb(struct bt_data *data, void *user_data)
{
	struct ad_parse_result *result = user_data;
	size_t len;

	switch (data->type) {
	case BT_DATA_NAME_COMPLETE:
	case BT_DATA_NAME_SHORTENED:
		len = MIN(data->data_len, sizeof(result->name) - 1);
		memcpy(result->name, data->data, len);
		result->name[len] = '\0';
		result->has_name = true;
		break;
	case BT_DATA_MANUFACTURER_DATA:
		if (data->data_len >= FILTER_LEN &&
		    data->data[0] == FILTER_BYTE_0 && data->data[1] == FILTER_BYTE_1 &&
		    data->data[2] == FILTER_BYTE_2 && data->data[3] == FILTER_BYTE_3) {
			result->mfg_matched = true;
			result->mfg_data = data->data;
			result->mfg_data_len = data->data_len;
		}
		break;
	default:
		break;
	}

	return true; /* keep parsing the rest of this report's AD structures */
}

static void format_hex(const uint8_t *data, uint8_t len, char *out, size_t out_size)
{
	size_t pos = 0;

	for (uint8_t i = 0; i < len && pos + 3 < out_size; i++) {
		pos += snprintf(&out[pos], out_size - pos, "%02X ", data[i]);
	}
}

static void scan_cb(const bt_addr_le_t *addr, int8_t rssi, uint8_t adv_type,
		     struct net_buf_simple *buf)
{
	struct ad_parse_result result = {0};
	struct tracked_tag *tag;
	char addr_str[BT_ADDR_LE_STR_LEN];
	char mfg_hex[3 * 32 + 1];
	int32_t now_ms = (int32_t)k_uptime_get();
	int32_t delta_ms;

	ARG_UNUSED(adv_type);

	bt_data_parse(buf, ad_parse_cb, &result);

	tag = tag_lookup(addr);

	if (!result.mfg_matched) {
		/* Not a report from one of our tags (or the scan response of
		 * one we're already tracking) - only cache the name if we
		 * already track this address, never create a new entry just
		 * because we saw a name.
		 */
		if (tag != NULL && result.has_name) {
			strncpy(tag->name, result.name, sizeof(tag->name) - 1);
			tag->name[sizeof(tag->name) - 1] = '\0';
		}
		return;
	}

	if (tag == NULL) {
		tag = tag_add(addr);
		if (tag == NULL) {
			return;
		}
	}

	if (result.has_name) {
		strncpy(tag->name, result.name, sizeof(tag->name) - 1);
		tag->name[sizeof(tag->name) - 1] = '\0';
	}

	bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));
	format_hex(result.mfg_data, result.mfg_data_len, mfg_hex, sizeof(mfg_hex));

	delta_ms = (tag->last_seen_ms >= 0) ? (now_ms - tag->last_seen_ms) : -1;
	tag->last_seen_ms = now_ms;

	dashboard_update_row((int)(tag - tracked), addr_str,
			      tag->name[0] ? tag->name : "(unknown)", rssi, mfg_hex, delta_ms,
			      now_ms);
}

int main(void)
{
	int err;

	LOG_INF("Starting TAG scanner");

	err = bt_enable(NULL);
	if (err) {
		LOG_ERR("Bluetooth init failed (err %d)", err);
		return 0;
	}
	LOG_INF("Bluetooth initialized");

	err = bt_le_scan_start(&scan_param, scan_cb);
	if (err) {
		LOG_ERR("Scan start failed (err %d)", err);
		return 0;
	}
	LOG_INF("Scanning for tags (company ID 0x0059, pattern AA BB ...)");

	dashboard_init();

	return 0;
}
