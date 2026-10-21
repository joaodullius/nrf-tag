/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/*
 * Parte do main.c que depende do modelo. Modelo: exemplo da Nordic (pasta
 * nrf_edgeai_generated/Neuton/), estados de transporte de uma encomenda a partir
 * da magnitude da aceleracao: 1 entrada, janela de 50 amostras, 7 classes.
 *
 * Incluido por main.c depois da definicao de `imu` e de class_color_t.
 */

#ifndef MODEL_CFG_H_
#define MODEL_CFG_H_

#define APP_NAME "06-neuton-transporte"

/* Contrato do modelo: CONFERIDO contra o .c gerado pelos asserts em main().
 * Os valores reais estao no .c gerado: INPUT_WINDOW_SIZE, INPUT_UNIQ_FEATURES_NUM
 * e MODEL_OUTPUTS_NUM.
 */
#define USER_WINDOW_SIZE      50U /* amostras por janela de inferencia (0,5 s a 100 Hz) */
#define USER_UNIQ_INPUTS_NUM  1U  /* entradas por amostra: so a magnitude */
#define USER_MODELS_CLASS_NUM 7U  /* classes na saida */

/* Fundo de escala do BMI270. Nao muda a unidade, so o teto antes de saturar. */
#define IMU_ACCEL_FS_G   4
#define IMU_GYRO_FS_DPS  1000

/* Uma cor por classe, NA ORDEM DO MODELO. */
static const class_color_t CLASS_COLORS[USER_MODELS_CLASS_NUM] = {
	{0, 0, 0, "Idle"},       /* apagado  */
	{1, 0, 0, "Shaking"},    /* vermelho */
	{1, 1, 0, "Impact"},     /* amarelo  */
	{1, 0, 1, "Free Fall"},  /* magenta  */
	{0, 1, 0, "Carrying"},   /* verde    */
	{0, 0, 1, "in Car"},     /* azul     */
	{0, 1, 1, "Placed"},     /* ciano    */
};

/* UMA entrada: a magnitude da aceleracao em MILI-G,
 * sqrt(x^2 + y^2 + z^2) / 9.80665 * 1000.
 *
 * A unidade nao esta escrita na doc do sample; foi preciso ler os vetores
 * embarcados dele. O vetor da classe IDLE (TAG parada, so gravidade) tem valores
 * ~1017, nao ~9.8:
 *
 *     CLASS_0_PARCEL_IDLE_ACCEL_DATA[] = {1019.23, 1018.65, 1016.69, ...}
 *
 * Alimentar em m/s2 (1 g ~ 9.81) NAO da erro nenhum: compila, roda, e classifica
 * errado. 9.8 cai no fundo da faixa que o modelo conhece
 * (INPUT_FEATURES_SCALE_MIN = 6.26), e o fundo da faixa e justamente queda livre.
 * Verificado no hardware: a TAG parada na mesa reportava "Free Fall (65%)" em
 * m/s2 e "Idle (98%)" em mili-g.
 */
#define MILLI_G_POR_MS2 (1000.0 / 9.80665)

static int imu_read_sample(flt32_t out[USER_UNIQ_INPUTS_NUM])
{
	struct sensor_value accel[3];
	int err = sensor_sample_fetch(imu);

	if (err) {
		return err;
	}
	err = sensor_channel_get(imu, SENSOR_CHAN_ACCEL_XYZ, accel);
	if (err) {
		return err;
	}

	double x = sensor_value_to_double(&accel[0]);
	double y = sensor_value_to_double(&accel[1]);
	double z = sensor_value_to_double(&accel[2]);

	out[0] = (flt32_t)(sqrt(x * x + y * y + z * z) * MILLI_G_POR_MS2);
	return 0;
}

#endif /* MODEL_CFG_H_ */
