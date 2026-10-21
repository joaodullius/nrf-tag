#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include "cpu_path.h"
#include "sensor.h"
#include "spim_dppi.h"

LOG_MODULE_REGISTER(cpu_path, LOG_LEVEL_INF);

/*
 * The conventional design: the data-ready edge raises a GPIOTE interrupt (the Zephyr GPIO
 * driver allocates a GPIOTE30 IN channel for it), the callback wakes a thread, the thread
 * runs one SPI transfer and queues the burst. Same SPIM, pins and queue as the DPPI paths;
 * only the CPU is in the loop.
 */
static const struct device *const gpio = DEVICE_DT_GET(DT_NODELABEL(gpio0));
static struct gpio_callback cb;
static K_SEM_DEFINE(tick_sem, 0, 1);
/* EasyDMA reads the TX bytes from RAM, so the backend's flash copy is not usable */
static uint8_t tx[8];
static uint32_t xfers, dropped;

static void drdy_cb(const struct device *dev, struct gpio_callback *c, uint32_t pins)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(c);
	ARG_UNUSED(pins);
	k_sem_give(&tick_sem);
}

static void cpu_thread(void *a, void *b, void *c)
{
	uint8_t rx[SENSOR_BURST_LEN];

	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	while (1) {
		k_sem_take(&tick_sem, K_FOREVER);
		if (spim_xfer_blocking(tx, sensor->burst_tx_len, rx, sizeof(rx)) != 0) {
			continue;
		}
		xfers++;
		if (k_msgq_put(&sample_q, rx, K_NO_WAIT) != 0) {
			dropped++;
		}
	}
}

/* Cooperative priority, like the DPPI drain thread: the comparison is about
 * how often the core wakes, not about scheduling */
K_THREAD_DEFINE(cpu_tid, 1024, cpu_thread, NULL, NULL, NULL, -1, 0, 0);

int cpu_path_start(void)
{
	int err;

	if (sensor->burst_tx_len > sizeof(tx) || !device_is_ready(gpio)) {
		return -EINVAL;
	}
	memcpy(tx, sensor->burst_tx, sensor->burst_tx_len);
	err = gpio_pin_configure(gpio, CONFIG_APP_DRDY_PIN, GPIO_INPUT | GPIO_PULL_DOWN);
	if (err) {
		return err;
	}
	gpio_init_callback(&cb, drdy_cb, BIT(CONFIG_APP_DRDY_PIN));
	gpio_add_callback(gpio, &cb);
	err = gpio_pin_interrupt_configure(gpio, CONFIG_APP_DRDY_PIN, GPIO_INT_EDGE_RISING);
	LOG_INF("CPU path: GPIOTE interrupt on P0.%02d -> thread -> SPI transfer", CONFIG_APP_DRDY_PIN);
	return err;
}

uint32_t cpu_path_xfers(void)
{
	return xfers;
}

uint32_t cpu_path_dropped(void)
{
	return dropped;
}
