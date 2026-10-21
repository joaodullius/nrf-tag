# 06-neuton-transporte

Inferência rodando, sem treino: o **modelo de exemplo da Nordic** (estados de transporte de uma
encomenda) classifica a aceleração do BMI270 do Tag a cada janela e pinta o LED RGB. É o ponto de
partida: funciona sem treinar nada, com a fiação inteira de ponta a ponta. O
[07-neuton-ventilador](../07-neuton-ventilador/) é o mesmo app com outro modelo.

Origem: `edge_ai/04_classify_led` do curso nrf-manaus-2, derivado de
`samples/nrf_edgeai/classification` do Edge AI Add-on v2.3.0 (licença Nordic em
[LICENSE](LICENSE)).

## O que mostra

| Item | Valor |
|---|---|
| Entrada | 1 canal: magnitude da aceleração do BMI270 em **mili-g** (1 g ≈ 1000) |
| Janela | 50 amostras a 100 Hz (0,5 s) |
| Saída | 7 classes, em cores no LED RGB (tabela abaixo) |
| Custo | 87.616 B de flash, 19.488 B de RAM, na CPU (Neuton) |
| Solução | Edge AI Lab id 90449 |

| Classe | Estado | LED |
|---:|---|---|
| 0 | Idle | apagado |
| 1 | Shaking | vermelho |
| 2 | Impact | amarelo |
| 3 | Free Fall | magenta |
| 4 | Carrying | verde |
| 5 | in Car | azul |
| 6 | Placed | ciano |

## Mesma base do 07, só muda o modelo

`src/main.c`, `prj.conf`, `sysbuild.conf` e o `CMakeLists.txt` (fora o nome do modelo) são iguais nos
dois exemplos. O que depende do modelo está em dois lugares:

- `src/model_cfg.h`: o contrato (janela, entradas, classes), as cores das classes e a leitura do IMU
  na escala que o modelo espera (aqui, uma entrada em mili-g; no 07, seis eixos em micro-unidades SI);
- `src/nrf_edgeai_generated/Neuton/`: os arquivos gerados (`nrf_edgeai_user_model.c/.h` e
  `nrf_edgeai_user_types.h`).

## Build (NCS v3.4.1 + Edge AI Add-on como módulo extra)

```
nrfutil sdk-manager toolchain launch --ncs-version v3.4.1 --chdir C:\ncs\v3.4.1 -- ^
  west build -s <repo>\tag\06-neuton-transporte -d <build> ^
  -b nrf54l15tag/nrf54l15/cpuapp -p always --sysbuild ^
  -- "-DEXTRA_ZEPHYR_MODULES=C:/ncs/sdk-edge-ai/edge-ai"
```

O Add-on (v2.3.0) fixa o NCS v3.4.0 no seu `west.yml`. Aqui ele entra só como módulo
Zephyr extra sobre o NCS v3.4.1, que compilou, mas não é uma combinação que a Nordic declara
suportada. O build gera `merged_nrf54l15tag_nrf54l15_cpuapp.hex` (gravável com `nrfutil device program`).

## Saída esperada (RTT)

```
main: 06-neuton-transporte — IMU -> inferencia -> LED
main: nRF Edge AI Lab Solution id: 90449
main: janela 50 · entradas 1 · classes 7 · 100 Hz
main: janela em ... ms (esperado 500)
main: classe 0 — Idle (98%)
```

Estado: compila com a estrutura atual (este repositório, 2026-10-20). No curso nrf-manaus-2, com a
mesma lógica de leitura, o Tag parado na mesa reportou `Idle (98%)`. As demais classes
(`Shaking`, `Impact`, `Free Fall`, `Carrying`, `in Car`, `Placed`) não foram testadas aqui.

## Armadilha que vale mostrar: a escala da entrada

O modelo espera os números **na escala do dataset em que foi treinado**, e errar isso é silencioso.
Este modelo espera a magnitude em **mili-g**. Isso **não está escrito na doc do sample**: foi preciso ler
os vetores embarcados dele. O vetor da classe Idle (Tag parada, só gravidade) tem valores ~1017:

```c
CLASS_0_PARCEL_IDLE_ACCEL_DATA[] = {1019.23, 1018.65, 1016.69, ...};
```

Alimentar em m/s² (1 g ≈ 9,81) não dá erro nenhum: compila, roda, e classifica errado. Medido com o Tag
parado na mesa: `Free Fall (65%)` em m/s²; `Idle (98%)` em mili-g.

O `main.c` também confere o contrato do modelo com `__ASSERT_NO_MSG()`: trocar o modelo sem ajustar
`USER_WINDOW_SIZE`, `USER_UNIQ_INPUTS_NUM` e `USER_MODELS_CLASS_NUM` em `model_cfg.h` trava o boot.

`flash_and_capture.ps1` grava (`--erase-mode ranges`) e captura o RTT.
