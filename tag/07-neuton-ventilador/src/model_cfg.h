/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/*
 * Parte do main.c que depende do modelo. Modelo: ventilador_95922 (Edge AI Lab),
 * velocidade de um ventilador pela vibracao: 6 entradas, janela de 128 amostras,
 * 4 classes, features de frequencia.
 *
 * Incluido por main.c depois da definicao de `imu` e de class_color_t.
 */

#ifndef MODEL_CFG_H_
#define MODEL_CFG_H_

#define APP_NAME "07-neuton-ventilador"

/* Contrato do modelo: CONFERIDO contra o .c gerado pelos asserts em main().
 * Os valores reais estao no .c gerado: INPUT_WINDOW_SIZE, INPUT_UNIQ_FEATURES_NUM
 * e MODEL_OUTPUTS_NUM.
 */
#define USER_WINDOW_SIZE      128U /* amostras por janela de inferencia (1,28 s a 100 Hz) */
#define USER_UNIQ_INPUTS_NUM  6U   /* entradas por amostra: ax,ay,az,gx,gy,gz */
#define USER_MODELS_CLASS_NUM 4U   /* classes na saida */

/* Fundo de escala do BMI270: o MESMO da coleta (+-4 g / +-1000 dps). Nao muda a
 * unidade, so o teto antes de saturar, mas o modelo aprendeu com um sinal que
 * satura num ponto e precisa ver o mesmo aqui.
 */
#define IMU_ACCEL_FS_G   4
#define IMU_GYRO_FS_DPS  1000

/* Uma cor por classe, NA ORDEM DO DICIONARIO de classes do dataset. */
static const class_color_t CLASS_COLORS[USER_MODELS_CLASS_NUM] = {
	{0, 0, 1, "idle"},  /* azul     */
	{0, 1, 0, "vel1"},  /* verde    */
	{1, 1, 0, "vel2"},  /* amarelo  */
	{1, 0, 0, "vel3"},  /* vermelho */
};

/* Seis canais em MICRO-unidades SI: m/s2 x 1e6 e rad/s x 1e6 (o giroscopio do
 * Zephyr e rad/s, nao dps). E o que o Data Forwarder manda com INT32_VALUES=y e o
 * que esta no CSV que treinou o modelo: mesma funcao (sensor_value_to_micro),
 * mesma ordem (ax,ay,az,gx,gy,gz), nenhum fator.
 */
static int imu_read_sample(flt32_t out[USER_UNIQ_INPUTS_NUM])
{
	struct sensor_value accel[3], gyro[3];
	int err = sensor_sample_fetch(imu);

	if (err) {
		return err;
	}
	err = sensor_channel_get(imu, SENSOR_CHAN_ACCEL_XYZ, accel);
	if (err) {
		return err;
	}
	err = sensor_channel_get(imu, SENSOR_CHAN_GYRO_XYZ, gyro);
	if (err) {
		return err;
	}
	for (int i = 0; i < 3; i++) {
		out[i] = (flt32_t)sensor_value_to_micro(&accel[i]);
		out[3 + i] = (flt32_t)sensor_value_to_micro(&gyro[i]);
	}
	return 0;
}

#endif /* MODEL_CFG_H_ */
