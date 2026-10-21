# 07-neuton-ventilador

Inferência rodando, sem treino: um modelo Neuton já treinado classifica a **velocidade de um ventilador**
pela vibração do BMI270 do Tag, a cada janela, e pinta o LED RGB. É o mesmo app do
[06-neuton-transporte](../06-neuton-transporte/) com outro modelo, e com features de frequência.

Origem: `edge_ai/04_classify_led` do curso nrf-manaus-2, derivado de
`samples/nrf_edgeai/classification` do Edge AI Add-on v2.3.0 (licença Nordic em
[LICENSE](LICENSE)). O modelo `ventilador_95922` foi treinado no Edge AI Lab com o dataset de
referência do `05_data_forwarder` do curso.

## O que mostra

| Item | Valor |
|---|---|
| Entrada | 6 eixos do BMI270 (ax..gz), micro-unidades SI, ±4 g / ±1000 dps |
| Janela | 128 amostras a 100 Hz (1,28 s), features no domínio da frequência |
| Saída | 4 classes: `idle` (azul), `vel1` (verde), `vel2` (amarelo), `vel3` (vermelho) |
| Custo | 89.408 B de flash, 24.016 B de RAM, na CPU (Neuton) |
| Solução | Edge AI Lab id 95922 |

## Mesma base do 06, só muda o modelo

`src/main.c`, `prj.conf`, `sysbuild.conf` e o `CMakeLists.txt` (fora o nome do modelo) são iguais nos
dois exemplos. O que depende do modelo está em dois lugares:

- `src/model_cfg.h`: o contrato (janela, entradas, classes), as cores das classes e a leitura do IMU
  na escala que o modelo espera (aqui, seis eixos em micro-unidades SI; no 06, uma entrada em mili-g);
- `src/nrf_edgeai_generated/ventilador_95922/`: os arquivos gerados pelo Edge AI Lab.

## Build (NCS v3.4.1 + Edge AI Add-on como módulo extra)

```
nrfutil sdk-manager toolchain launch --ncs-version v3.4.1 --chdir C:\ncs\v3.4.1 -- ^
  west build -s <repo>\tag\07-neuton-ventilador -d <build> ^
  -b nrf54l15tag/nrf54l15/cpuapp -p always --sysbuild ^
  -- "-DEXTRA_ZEPHYR_MODULES=C:/ncs/sdk-edge-ai/edge-ai"
```

O Add-on (v2.3.0) fixa o NCS v3.4.0 no seu `west.yml`. Aqui ele entra só como módulo
Zephyr extra sobre o NCS v3.4.1, que compilou e rodou no Tag, mas não é uma combinação
que a Nordic declara suportada. O build gera `merged_nrf54l15tag_nrf54l15_cpuapp.hex`.

## Saída esperada (RTT)

```
main: 07-neuton-ventilador — IMU -> inferencia -> LED
main: nRF Edge AI Lab Solution id: 95922
main: janela 128 · entradas 6 · classes 4 · 100 Hz
main: janela em 1282 ms (esperado 1280)
main: classe 0 — idle (99%)
```

Validado no Tag em 2026-10-19 (antes da reestruturação em `model_cfg.h`, mesma lógica): boot limpo,
janelas a 1273 a 1282 ms, classe `idle` (99 %) com o Tag parado. As classes `vel1` a `vel3` dependem de o
Tag estar sobre o ventilador ligado; isso ainda não foi testado.

## Armadilhas que valem mostrar

**O contrato do modelo.** O `main.c` confere o contrato com `__ASSERT_NO_MSG()`: trocar o modelo sem
ajustar `USER_WINDOW_SIZE`, `USER_UNIQ_INPUTS_NUM` e `USER_MODELS_CLASS_NUM` em `model_cfg.h` trava o
boot em um assert. A escala da entrada também faz parte do contrato, e errar ela é silencioso.

**A taxa de amostragem.** Com features de frequência, o espectro que o modelo vê depende da taxa em
que a app entrega amostras. `k_msleep(10)` no laço soma o tempo da leitura SPI (~0,3 ms): o laço cai
para 96,9 Hz e a janela de 128 amostras fecha em 1321 ms em vez de 1280 (medido no Tag); a classe sai uma
velocidade acima da real, sem erro nem aviso. O `main.c` amostra por `k_timer` periódico + semáforo e
imprime o ritmo das três primeiras janelas; qualquer janela fora de 2% vira `<wrn>`.

`flash_and_capture.ps1` grava (`--erase-mode ranges`) e captura o RTT.
