# 04-accel-dppi-drdy

Referência de aplicação com acelerômetro SPI lido **por data-ready, sem CPU no caminho da aquisição**:

```
BMI270 INT (P1.04) --> GPIOTE20 IN --DPPI--> SPIM22 START --EasyDMA--> anel em RAM --lote--> k_msgq
                       (domínio PERI)         CSN por hardware (P1.07)
```

O INT do sensor vira um evento GPIOTE que dispara `SPIM.TASKS_START` pelo DPPI. Uma transação por
amostra nova, sem TIMER. A CPU só entrega as amostras à fila: uma thread esvazia o anel a cada
`APP_DRAIN_PERIOD_US` (padrão), ou a IRQ de `END` copia cada rajada (`APP_PER_SAMPLE_IRQ=y`).

Base: `dppi_works/gpiote_dppi_spim` (exemplo recomendado de lá, já validado na Tag, com o relatório de
medições e a explicação do mecanismo). Aqui ficou só a Tag (`nrf54l15tag/nrf54l15/cpuapp`) com o BMI270.
O mesmo bloco, com o sensor simulado por um gerador externo e sem sensor real, é medido na
nRF54L15-DK em [`05-accel-dppi-power`](../05-accel-dppi-power/README.md).

## Pontos que importam

- **O data-ready é nível, não pulso.** Com o INT não latched, ele já está alto quando o DPPI é ligado e
  nenhuma borda viria. Um `START` por software depois de ligar o DPPI lê e limpa; cada amostra seguinte
  gera uma borda nova.
- **O pino e o GPIOTE vêm do devicetree.** `irq-gpios` do BMI270 na placa (P1.04); o GPIOTE é o da
  porta desse pino (GPIOTE20, mesmo domínio de energia do SPIM22).
- **ODR do BMI270:** 25 a 1.600 Hz (`APP_SENSOR_ODR_HZ`, padrão 400). A taxa de transações é o ODR.
- **SPI a 8 MHz** (`APP_SPI_FREQ_HZ`) e `APP_SPI_RX_DELAY=1`; a 8 MHz o PRESCALER fica em 2 e evita o
  errata 8 do SPIM.

## Build e gravação (NCS v3.4.1)

```
nrfutil sdk-manager toolchain launch --ncs-version v3.4.1 --chdir C:\ncs\v3.4.1 -- ^
  west build -s <repo>\tag\04-accel-dppi-drdy -d <build> -b nrf54l15tag/nrf54l15/cpuapp -p always
```

O `sysbuild.conf` liga `SB_CONFIG_MERGED_HEX_FILES`: o build gera `merged_nrf54l15tag_nrf54l15_cpuapp.hex`,
que se grava com `nrfutil device program --firmware <hex> --serial-number <serial>`. A Tag grava pelo
`DEBUG OUT` de uma DK. O log sai por RTT (a Tag não tem UART).

Para o modo por amostra, acrescente `-- -DCONFIG_APP_PER_SAMPLE_IRQ=y`.

## Estado

Validado na Tag em 2026-10-19 (build desta pasta, gravado pela LM20-DK no `DEBUG OUT`, log por RTT, BMI270 a
400 Hz, modo drenado a 10 ms): `CHIP_ID 0x24`, INT1 configurado, DPPI conectado, e em 27 s de relatórios
`xfers` ≈ 404/s, `queued` = `fresh` = 402 a 406/s, `dropped=0`, `late=0`, `ovf=0`, `torn=0`. Não foram
repetidos aqui: o modo por amostra (`APP_PER_SAMPLE_IRQ`), ODR de 1.600 Hz e a medição de corrente; o
levantamento de perdas até 1.600 Hz e o consumo vêm do `dppi_works`.
