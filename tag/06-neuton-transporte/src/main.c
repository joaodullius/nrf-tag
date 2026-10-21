/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/*
 * Neuton no Tag: IMU -> inferencia -> LED RGB.
 *
 * ORIGEM: derivado de samples/nrf_edgeai/classification do Edge AI Add-on v2.3.0,
 *   via edge_ai/04_classify_led do curso nrf-manaus-2. O sample original alimenta o
 *   modelo com VETORES EMBARCADOS; aqui a entrada vem do BMI270 do nRF54L15-TAG e a
 *   saida pinta o LED RGB.
 *
 * Este main.c e IGUAL nos exemplos 06-neuton-transporte e 07-neuton-ventilador.
 * O que muda de um para o outro esta em dois lugares, e so neles:
 *   - src/model_cfg.h  o contrato do modelo (janela, entradas, classes), as cores
 *                      das classes e a leitura do IMU na escala que o modelo espera
 *   - src/nrf_edgeai_generated/  os arquivos gerados pelo Edge AI Lab
 *
 * A engine Edge AI inteira cabe em quatro chamadas:
 *
 *     nrf_edgeai_user_model()    pega o modelo gerado
 *     nrf_edgeai_init()          inicializa
 *     nrf_edgeai_feed_inputs()   entrega UMA amostra por vez; o runtime acumula
 *     nrf_edgeai_run_inference() roda quando a janela fecha
 *
 * O modelo tem CONTRATO: as constantes USER_* de model_cfg.h sao conferidas contra
 * o modelo carregado pelos __ASSERT_NO_MSG() em main(). Errar uma trava o boot.
 * A ESCALA da entrada tambem faz parte do contrato, e errar ela e SILENCIOSO:
 * compila, roda e classifica errado. Veja imu_read_sample() em model_cfg.h.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>

#include <math.h>

#include <nrf_edgeai/nrf_edgeai.h>
#include "nrf_edgeai_user_model.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* Taxa de amostragem: a do dataset em que o modelo foi treinado (100 Hz nos dois
 * modelos deste repositorio).
 */
#define SAMPLE_RATE_HZ 100U
#define SAMPLE_PERIOD_US (1000000U / SAMPLE_RATE_HZ)

/* O ritmo de amostragem e um k_timer periodico + semaforo. NAO use k_msleep(10)
 * no laco: o tempo da leitura SPI soma ao sleep e o laco cai para ~97 Hz (medido:
 * janela de 128 amostras em 1321 ms em vez de 1280). Com features de frequencia
 * isso desloca o espectro inteiro e o modelo passa a ver outra coisa. A taxa da
 * inferencia tem de ser a taxa da coleta.
 */
static K_SEM_DEFINE(tick_sem, 0, 1);
static void tick_handler(struct k_timer *t)
{
	ARG_UNUSED(t);
	k_sem_give(&tick_sem);
}
static K_TIMER_DEFINE(tick_timer, tick_handler, NULL);

/* LED RGB da TAG (led1_red / led1_green / led1_blue no DTS do board). E GPIO
 * liga/desliga por canal: 3 bits, 8 combinacoes, uma delas apagada. Acima de 8
 * classes as cores se repetem; use PWM (o board expoe rgb_led_1 como
 * leds-group-multicolor) ou numero de piscadas.
 */
static const struct gpio_dt_spec led_r = GPIO_DT_SPEC_GET(DT_NODELABEL(led1_red), gpios);
static const struct gpio_dt_spec led_g = GPIO_DT_SPEC_GET(DT_NODELABEL(led1_green), gpios);
static const struct gpio_dt_spec led_b = GPIO_DT_SPEC_GET(DT_NODELABEL(led1_blue), gpios);

typedef struct {
	uint8_t r, g, b;
	const char *nome;
} class_color_t;

static const struct device *const imu = DEVICE_DT_GET_ONE(bosch_bmi270);

/* Parte do modelo: USER_*, IMU_*_FS, CLASS_COLORS e imu_read_sample(). */
#include "model_cfg.h"

/* Tolerancia para o aviso de janela fora do ritmo: 2% da janela. */
#define WINDOW_EXPECTED_MS ((USER_WINDOW_SIZE * 1000U) / SAMPLE_RATE_HZ)
#define WINDOW_TOLERANCE_MS (WINDOW_EXPECTED_MS / 50U)

/* Sem isto, aumentar USER_MODELS_CLASS_NUM e esquecer de acrescentar cores
 * COMPILA: o C preenche o resto com zero, e as classes novas ficam com LED
 * apagado e nome NULL indo para o %s do LOG_INF. Falha silenciosa.
 */
BUILD_ASSERT(ARRAY_SIZE(CLASS_COLORS) == USER_MODELS_CLASS_NUM,
	     "CLASS_COLORS precisa ter exatamente USER_MODELS_CLASS_NUM entradas");

static void led_set(const class_color_t *c)
{
	(void)gpio_pin_set_dt(&led_r, c->r);
	(void)gpio_pin_set_dt(&led_g, c->g);
	(void)gpio_pin_set_dt(&led_b, c->b);
}

static int leds_init(void)
{
	const struct gpio_dt_spec *leds[] = {&led_r, &led_g, &led_b};

	for (size_t i = 0; i < ARRAY_SIZE(leds); i++) {
		if (!gpio_is_ready_dt(leds[i])) {
			LOG_ERR("LED %zu nao esta pronto", i);
			return -ENODEV;
		}
		int err = gpio_pin_configure_dt(leds[i], GPIO_OUTPUT_INACTIVE);

		if (err) {
			LOG_ERR("gpio_pin_configure_dt LED %zu: %d", i, err);
			return err;
		}
	}
	return 0;
}

static int imu_init(void)
{
	struct sensor_value fs_a = {.val1 = IMU_ACCEL_FS_G, .val2 = 0};
	struct sensor_value fs_g = {.val1 = IMU_GYRO_FS_DPS, .val2 = 0};
	struct sensor_value odr = {.val1 = SAMPLE_RATE_HZ, .val2 = 0};

	if (!device_is_ready(imu)) {
		LOG_ERR("BMI270 nao esta pronto");
		return -ENODEV;
	}
	/* Frequencia por ultimo: e ela que liga o sensor. */
	(void)sensor_attr_set(imu, SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_FULL_SCALE, &fs_a);
	(void)sensor_attr_set(imu, SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_SAMPLING_FREQUENCY, &odr);
	(void)sensor_attr_set(imu, SENSOR_CHAN_GYRO_XYZ, SENSOR_ATTR_FULL_SCALE, &fs_g);
	(void)sensor_attr_set(imu, SENSOR_CHAN_GYRO_XYZ, SENSOR_ATTR_SAMPLING_FREQUENCY, &odr);
	return 0;
}

int main(void)
{
	nrf_edgeai_t *p_model = nrf_edgeai_user_model();

	/* O modelo tem contrato. Se voce trocou o modelo e nao ajustou o
	 * model_cfg.h, o boot para aqui, de proposito.
	 */
	__ASSERT_NO_MSG(nrf_edgeai_input_window_size(p_model) == USER_WINDOW_SIZE);
	__ASSERT_NO_MSG(nrf_edgeai_uniq_inputs_num(p_model) == USER_UNIQ_INPUTS_NUM);
	__ASSERT_NO_MSG(nrf_edgeai_model_outputs_num(p_model) == USER_MODELS_CLASS_NUM);

	nrf_edgeai_err_t res = nrf_edgeai_init(p_model);

	if (res != NRF_EDGEAI_ERR_SUCCESS) {
		LOG_ERR("nrf_edgeai_init falhou: %d", (int)res);
		return -EIO;
	}

	nrf_edgeai_rt_version_t v = nrf_edgeai_runtime_version();

	LOG_INF("%s — IMU -> inferencia -> LED", APP_NAME);
	LOG_INF("Edge AI runtime %d.%d.%d", v.field.major, v.field.minor, v.field.patch);
	LOG_INF("nRF Edge AI Lab Solution id: %s", nrf_edgeai_solution_id_str(p_model));
	LOG_INF("janela %u · entradas %u · classes %u · %u Hz",
		USER_WINDOW_SIZE, USER_UNIQ_INPUTS_NUM, USER_MODELS_CLASS_NUM, SAMPLE_RATE_HZ);

	if (leds_init() || imu_init()) {
		return -ENODEV;
	}

	int32_t ultima_classe = -1;
	uint32_t t_janela = k_uptime_get_32();
	uint32_t janelas = 0;

	k_timer_start(&tick_timer, K_NO_WAIT, K_USEC(SAMPLE_PERIOD_US));

	while (1) {
		flt32_t amostra[USER_UNIQ_INPUTS_NUM];

		k_sem_take(&tick_sem, K_FOREVER);

		if (imu_read_sample(amostra) == 0) {
			res = nrf_edgeai_feed_inputs(p_model, amostra, USER_UNIQ_INPUTS_NUM);

			/* INPROGRESS domina: o runtime so infere quando a janela
			 * fecha. Nao e erro.
			 */
			if (res == NRF_EDGEAI_ERR_SUCCESS) {
				/* Ritmo real da janela. Se sair da tolerancia, o que
				 * o modelo ve nao e o do dataset.
				 */
				uint32_t agora = k_uptime_get_32();
				uint32_t dt = agora - t_janela;

				t_janela = agora;
				if (dt > WINDOW_EXPECTED_MS + WINDOW_TOLERANCE_MS ||
				    dt + WINDOW_TOLERANCE_MS < WINDOW_EXPECTED_MS) {
					LOG_WRN("janela em %u ms (esperado %u): taxa fora "
						"da da coleta", dt, WINDOW_EXPECTED_MS);
				} else if (janelas < 3) {
					/* As primeiras, para conferir o ritmo no boot. */
					LOG_INF("janela em %u ms (esperado %u)", dt,
						WINDOW_EXPECTED_MS);
				}
				janelas++;

				res = nrf_edgeai_run_inference(p_model);
				if (res == NRF_EDGEAI_ERR_SUCCESS) {
					/* A saida decodificada fica no proprio handle. */
					uint16_t classe =
						p_model->decoded_output.classif.predicted_class;
					const flt32_t *probs =
						p_model->decoded_output.classif.probabilities.p_f32;

					if (classe < USER_MODELS_CLASS_NUM &&
					    (int32_t)classe != ultima_classe) {
						ultima_classe = classe;
						led_set(&CLASS_COLORS[classe]);
						LOG_INF("classe %u — %s (%u%%)", classe,
							CLASS_COLORS[classe].nome,
							(uint16_t)(probs[classe] * 100.0f));
					}
				} else {
					LOG_WRN("run_inference: %d", (int)res);
				}
			} else if (res != NRF_EDGEAI_ERR_INPROGRESS) {
				LOG_WRN("feed_inputs: %d", (int)res);
			}
		}
	}

	return 0;
}
