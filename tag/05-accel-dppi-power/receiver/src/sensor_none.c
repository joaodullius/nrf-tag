/*
 * "No sensor" backend (nRF54L15 DK). The SPIM still clocks the same burst on SCK/MOSI/MISO/CSN, so the SoC does exactly the work it does with
 * a real sensor; MISO is pulled down, every burst reads back zeros. What this
 * measures is the cost of the acquisition mechanism, not of any sensor.
 */
#include <zephyr/sys/util.h>

#include <zephyr/logging/log.h>

#include "sensor.h"

LOG_MODULE_REGISTER(sensor_none, LOG_LEVEL_INF);

static const uint8_t burst_tx[1] = { 0x23 };   /* read command, MSB 0: not hit by the nRF54L SPIM errata 8 */

static int none_init(void)
{
	return 0;
}

static int none_enable_drdy_int(void)
{
	return 0;
}

/* The external SPI slave rig (not part of this repo) sends 0xC0 + index. Count bursts that differ from
 * it: a slave that was not ready answers 0xEE, a noisy 8 MHz wire flips bits. */
static uint32_t bad;
static uint32_t seen;

uint32_t sensor_bad_count(void)
{
	return bad;
}

static void none_decode(const uint8_t *rx, struct sensor_sample *out)
{
	/* Sample one burst per 500 so the raw bytes show up in the log at any rate. */
	if ((seen++ % 500) == 0) {
		LOG_INF("raw burst: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
			rx[0], rx[1], rx[2], rx[3], rx[4], rx[5], rx[6], rx[7], rx[8],
			rx[9], rx[10]);
	}
	for (int i = 0; i < SENSOR_BURST_LEN && i < 32; i++) {
		if (rx[i] != (uint8_t)(0xC0 + i)) {
			if (bad < 5) {
				LOG_INF("bad burst: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
					rx[0], rx[1], rx[2], rx[3], rx[4], rx[5], rx[6], rx[7], rx[8],
					rx[9], rx[10]);
			}
			bad++;
			break;
		}
	}
	out->x = out->y = out->z = 0;
	out->fresh = true;
}

static struct sensor_ops ops = {
	.name = "no sensor",
	.init = none_init,
	.enable_drdy_int = none_enable_drdy_int,
	.burst_tx = burst_tx,
	.burst_tx_len = sizeof(burst_tx),
	.decode = none_decode,
	.fresh_offset = 2,
	.fresh_mask = 0x80,
	.mg_per_lsb = 2000.0f / 32768.0f,
};

const struct sensor_ops *const sensor = &ops;

uint16_t sensor_odr_hz_x10(void)
{
	return 0;
}
