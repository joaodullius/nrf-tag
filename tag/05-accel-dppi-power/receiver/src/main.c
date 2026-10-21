/*
 * 04-cpu-vs-dppi - the same SPI burst at the same rate, acquired three ways:
 *
 *   APP_ACQ_CPU               GPIOTE interrupt -> thread -> SPI transfer, per sample
 *   APP_ACQ_DPPI_PERSAMPLE    GPIOTE IN -> DPPI -> SPIM START, EasyDMA, one END
 *                             interrupt per sample
 *   APP_ACQ_DPPI_DRAIN        GPIOTE IN -> DPPI -> SPIM START, EasyDMA ring,
 *                             a thread drains it every APP_DRAIN_PERIOD_US
 *
 * Measure the current of each build with the PPK2 and compare. The consumer
 * below sleeps for the whole report period and then empties the queue, so it
 * adds the same small cost in every mode and does not hide the difference.
 *
 * The sample rate is set from outside: an external rig raises the
 * data-ready pin P0.01 once per sample, like the INT pin of an accelerometer. Engine from
 * dppi_works/timer_dppi_spim (nRF Connect SDK v3.4.1).
 */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <stdlib.h>
#include <string.h>

#include "sensor.h"
#include "spim_dppi.h"
#if defined(CONFIG_APP_ACQ_CPU)
#include "cpu_path.h"
#endif

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

#if defined(CONFIG_APP_ACQ_CPU)
#define MODE_NAME "CPU in the loop (GPIOTE interrupt + thread + SPI transfer)"
#elif defined(CONFIG_APP_ACQ_DPPI_PERSAMPLE)
#define MODE_NAME "DPPI, one END interrupt per sample"
#else
#define MODE_NAME "DPPI, EasyDMA ring drained in batches"
#endif

static uint32_t xfers(void)
{
#if defined(CONFIG_APP_ACQ_CPU)
	return cpu_path_xfers();
#else
	return spim_dppi_total_xfers();
#endif
}

static uint32_t dropped(void)
{
#if defined(CONFIG_APP_ACQ_CPU)
	return cpu_path_dropped();
#else
	return spim_dppi_dropped();
#endif
}

int main(void)
{
	uint8_t raw[SENSOR_BURST_LEN];
	struct sensor_sample s;
	uint32_t elapsed_ms = 0, last_xfers = 0;
	int err;

	LOG_INF("05-accel-dppi-power: %s, data-ready P0.%02d, burst %u B, SPI %u Hz", MODE_NAME,
		CONFIG_APP_DRDY_PIN, SENSOR_BURST_LEN, CONFIG_APP_SPI_FREQ_HZ);

	err = spim_dppi_init();
	if (err) {
		LOG_ERR("init failed (%d)", err);
		return err;
	}
#if defined(CONFIG_APP_ACQ_CPU)
	err = cpu_path_start();
#else
	err = spim_dppi_start();
#endif
	if (err) {
		LOG_ERR("start failed (%d)", err);
		return err;
	}

#if defined(CONFIG_APP_CSNDUR_SWEEP)
	static char sweep_list[] = CONFIG_APP_CSNDUR_SWEEP;
	char *sweep_save = NULL;
	char *tok = strtok_r(sweep_list, ",", &sweep_save);
	uint32_t csndur = tok ? strtoul(tok, NULL, 10) : 0;
	uint32_t win_ms = 0, win_q = 0, win_bad0 = sensor_bad_count();

	if (tok) {
		spim_dppi_set_csndur(csndur);
	}
#endif

	while (1) {
		uint32_t n = 0, fresh = 0;
		int16_t zmin = INT16_MAX, zmax = INT16_MIN;

		k_msleep(CONFIG_APP_REPORT_PERIOD_MS);
		elapsed_ms += CONFIG_APP_REPORT_PERIOD_MS;
		while (k_msgq_get(&sample_q, raw, K_NO_WAIT) == 0) {
			sensor->decode(raw, &s);
			n++;
			fresh += s.fresh;
			zmin = MIN(zmin, s.z);
			zmax = MAX(zmax, s.z);
		}
		uint32_t x = xfers();

		LOG_INF("t=%u ms xfers/s=%u queued=%u fresh=%u dropped=%u Z raw min=%d max=%d bad=%u",
			elapsed_ms, (x - last_xfers) * 1000 / CONFIG_APP_REPORT_PERIOD_MS,
			n, fresh, dropped(), n ? zmin : 0, n ? zmax : 0, sensor_bad_count());
		last_xfers = x;
#if defined(CONFIG_APP_CSNDUR_SWEEP)
		win_ms += CONFIG_APP_REPORT_PERIOD_MS;
		win_q += n;
		if (tok && win_ms >= CONFIG_APP_CSNDUR_WINDOW_MS) {
			LOG_INF("SWEEP csndur=%u (%u ns) window_ms=%u queued=%u bad=%u", csndur,
				csndur * 1000 / 16, win_ms, win_q, sensor_bad_count() - win_bad0);
			win_ms = 0;
			win_q = 0;
			win_bad0 = sensor_bad_count();
			tok = strtok_r(NULL, ",", &sweep_save);
			if (tok) {
				csndur = strtoul(tok, NULL, 10);
				spim_dppi_set_csndur(csndur);
			} else {
				LOG_INF("SWEEP done");
			}
		}
#endif
	}
	return 0;
}
