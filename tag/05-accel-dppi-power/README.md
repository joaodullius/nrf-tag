# 05-accel-dppi-power

Medição de consumo do caminho **data-ready → GPIOTE → DPPI → SPIM → RAM**, sem TIMER, na
nRF54L15-DK. É a mesma implementação do exemplo `04-accel-dppi-drdy` (acelerômetro SPI com gatilho
por data-ready), com o sensor simulado por um gerador externo, o que permite varrer taxas de 100 Hz
a 64 mil amostras por segundo. Três modos de aquisição são comparados, medindo a corrente do SoC
com o PPK2. O sensor é genérico: uma rajada SPI de 11 B a cada pulso de data-ready.

```
gerador (LM20-DK)        nRF54L15-DK
PWM em P1.30  --fio-->  P0.01 (GPIOTE30 IN) --DPPI--> SPIM30 START --EasyDMA--> ring em RAM --lote--> CPU
 (INT do sensor)          domínio LP, sem PERI         SCK P0.00, MOSI P0.02, MISO P0.03, CSN P0.04
```

O mesmo desenho vale para o exemplo `04-accel-dppi-drdy`, com sensor real na Tag (BMI270: INT em P1.04,
GPIOTE20, SPIM22, domínio PERI): só mudam os rótulos de pino e domínio.

## Peças

| Pasta | Onde roda | O que faz |
|---|---|---|
| `linktest/` | nRF54L15-DK | ferramenta local de teste de conexão (conta bordas em P0.01); **não versionada** |
| `receiver/` | nRF54L15-DK | os três modos de aquisição, log por RTT |

**A bancada externa não faz parte deste repositório.** O data-ready e o escravo SPI vêm de uma
nRF54LM20-DK com firmware de teste, mantido fora do repo (`C:/work/nrf-tag-test-rig`) e não versionado.
O que o receptor espera dela:

| Sinal | DK 15 (SPIM30 e GPIOTE30, domínio LP) | Bancada |
|---|---|---|
| Data-ready | `P0.01`, borda de subida, pulso curto (~2 µs) | PWM de hardware, taxa ajustável |
| SCK, MOSI, CSN | `P0.00`, `P0.02`, `P0.04` (saídas do mestre) | entradas do escravo |
| MISO | `P0.03` (entrada do mestre) | saída do escravo; responde `0xC0 + índice` nos 11 bytes |

Sem escravo ligado o MISO lê zero (pull-down), e o contador `bad` do receptor passa a contar todas as
rajadas; as tabelas de corrente deste README foram medidas assim, sem escravo.

Modos do receptor (`receiver/modes/*.conf`, passado por `-DEXTRA_CONF_FILE`):

| Modo | O que acorda a CPU |
|---|---|
| `cpu.conf` | cada borda do data-ready: interrupção do GPIOTE, thread, transferência SPI |
| `dppi-sample.conf` | uma interrupção SPIM END por amostra, só para copiar a rajada |
| `dppi-drain.conf` | uma vez a cada 20 ms, em lote |

## Ligação e cuidados

- Bancada externa (LM20-DK) → `P0.01` (nRF54L15-DK), GND comum pelo USB. As duas DKs a **1,8 V** (`vddio` da
  LM20-DK lido pelo Board Controller; padrão de fábrica da DK 15, cuja leitura falha com `rc -257`).
- `P0.00` a `P0.03` da DK 15 são a UART30/VCOM1, e `P0.04` é o botão 3. O overlay desliga a UART30. O
  teste de conexão mostrou que o VCOM1 **não** dirige o P0.01, então não foi preciso mudar nada no
  Board Configurator.
- Build e gravação no NCS v3.4.1, sempre com `west flash --no-rebuild`.

## Resultados (2026-10-19)

nRF54L15-DK, só o SoC, 1,8 V, SPI a 8 MHz (SPIM30), rajada de 11 B, MISO solto (pull-down interno,
sem escravo), PPK2 em Ampere Meter. **Protocolo limpo, sem RTT nem depurador:** gravar, power-cycle,
esperar 20 s, medir 12 s; corrente = mediana de 3 power-cycles (as 3 medidas diferem em no máximo 8 µA).
Gerador com HFXO: 10, 20, 32 e 64 kHz saíram a 0,04% da taxa pedida.

| ODR | CPU no laço | DPPI, 1 IRQ por amostra | DPPI, ring drenado |
|---:|---:|---:|---:|
| 0 (piso) | 129 µA | 128 µA | 136 µA |
| 100 Hz | 156 µA | 137 µA | 142 µA |
| 1 kHz | 668 µA | 219 µA | 171 µA |
| 4 kHz | 1.449 µA | 504 µA | 271 µA |
| 8 kHz | 1.882 µA | 701 µA | 409 µA |
| 16 kHz | 2.930 µA | 904 µA | 633 µA |
| 32 kHz | 3.530 µA (*) | 1.374 µA | 1.102 µA |
| 64 kHz | 3.368 µA (*) | 1.631 µA (**) | 1.870 µA (fila de 9.000) |

De 1 a 8 kHz o ring drenado usa 3,9 a 4,6 vezes menos corrente que a CPU no laço (171 contra 668 µA
a 1 kHz; 409 contra 1.882 µA a 8 kHz). A 64 kHz o ring drenado passa a IRQ por amostra, mas esta não entrega.

(*) Modo CPU: a CPU satura (~3,4 a 3,5 mA) e a thread principal não imprime relatório, então a vazão
não foi verificada. (**) A 64 kHz a interrupção por amostra colapsa (29 amostras por segundo chegam à
fila, medido numa rodada anterior com RTT e **não repetido** no protocolo limpo): sua corrente menor
não significa menos trabalho, e sim que ele não entrega. A taxa entregue desta tabela não foi
remedida (sem RTT); a coluna de taxa real das seções abaixo vem das rodadas anteriores.

**Os dois valores de corrente do ring drenado a 32 e 64 kHz (720/1.031 e 1.110/1.783 µA nas rodadas
antigas) não aparecem no protocolo limpo.** Eram medidas feitas depois de uma sessão RTT. Foi visto de novo
uma vez (1º ciclo depois de uma captura RTT, ~1.115 µA a 64 kHz, os cinco seguintes em ~2.170 µA) e
não se repetiu numa segunda tentativa, então a causa dentro do depurador não foi isolada. Conclusão
prática: **meça sempre sem RTT nem depurador**. As tabelas antigas (com RTT antes das 3 medidas)
ficaram 3 a 8% abaixo desta no ring drenado e no máximo 11% abaixo no modo CPU; não se isolou o motivo.

**Escravo no MISO:** com o escravo SPI da bancada dirigindo o MISO, o ring drenado mediu 414, 651,
1.143 e 2.020 µA a 8, 16, 32 e 64 kHz contra 409, 633, 1.102 e 1.870 µA com o MISO solto: +1% a +8%.
Os valores da tabela acima são com o MISO solto.

**Forma de onda (PPK2, 10 kS/s, 1 s):** a 64 kHz a corrente tem base contínua de ~1,3 mA e grupos de
picos curtos a cada ~20 ms (204 amostras de 100 µs, igual ao `APP_DRAIN_PERIOD_US`); a 16 kHz a base é
~0,4 mA. Isso confirma atividade e periodicidade do laço de drenagem; não resolve as rajadas de 15,6 µs
nem prova que cada transação terminou. Os picos de 10 a 13 mA não foram investigados.

### Teto a 8 MHz e perdas a 64 kHz (fila de 9.000, ring drenado, rajada de 11 B)

| ODR pedido | Taxa real | Perdas | Corrente (mediana) |
|---:|---:|---:|---:|
| 64 kHz | 63.992/s | 0 | 1.785 µA (primeira medida: 1.112 µA) |
| 72 kHz | 72.062/s | 0 | 1.211 µA |
| 80 kHz | 79.989/s | 0 | 1.314 µA |
| 88 kHz | 80.035/s | 117.747 | 1.349 µA |
| 96 kHz | 79.953/s | 167.472 | 1.371 µA |

- Com a fila de 9.000 as perdas a 64 kHz **zeraram**: eram estouro da fila do receptor.
- O receptor registrou **80 mil amostras por segundo sem perda** (fisicamente plausível: rajada de 11 µs
  para um período de 12,5 µs), mas veja a limitação da métrica abaixo.
- A 88 e 96 kHz a taxa entregue satura em ~80.000/s com muitas perdas. O mais provável é o gargalo ser o
  laço do receptor que esvazia a fila (~80 mil amostras por segundo), e **não** o barramento. O teto real
  do SPI a 8 MHz (11 µs de rajada mais a temporização do CSN) fica acima de 80 mil e, na conta, abaixo de
  ~88 mil, mas não foi isolado.

### Limitação da métrica de "taxa real" (importante)

A taxa real do ring drenado vem do ponteiro do DMA, que avança a cada disparo, e **não prova que a
transação SPI foi concluída**. Teste decisivo com SPI a 1 MHz (rajada de 11 B = 88 µs, teto ~11 mil/s):

| SPI | Pedido | Taxa "entregue" | Perdas | Corrente |
|---|---:|---:|---:|---:|
| 1 MHz | 5 kHz | 5.000/s | 0 | 411 µA |
| 1 MHz | 10 kHz | 10.000/s | 0 | 669 µA |
| 1 MHz | 20 kHz | 20.000/s | 0 | 801 µA |
| 1 MHz | 32 kHz | 31.999/s | 0 | 946 µA |

A 20 e 32 kHz o período (50 e 31 µs) é menor que a rajada (88 µs), então nem todas as transações
podem ter sido concluídas, e mesmo assim o receptor entrega a taxa pedida sem perdas. Consequências:

- Os resultados com SPI a **4 MHz** (63.993/s a 64 kHz, 0 perdas; 16, 32, 40 e 48 kHz idem) **não valem
  como evidência**: a 4 MHz a rajada leva 22 µs, e o teto físico é ~40 mil/s.
- A coluna de taxa real só tem sentido onde a rajada cabe no período (a 8 MHz e 11 B: rajada de 11 µs,
  cabe até ~88 mil/s na conta). A tabela de **corrente** continua valendo como medida.
- Nos modos DPPI, uma sobrecarga do barramento **não gera erro nem perda visível**: o dado fica velho
  na posição do anel. Num projeto real, compare o número de disparos com o de transações concluídas
  (por exemplo, contando os eventos END do SPIM).
- Para verificar a ocupação real do barramento: ligar o CSN (P0.04) a uma entrada digital do PPK2 e
  medir a fração de tempo em que ele fica baixo (~72% a 8 MHz e 64 kHz).

Carga por amostra acima do piso, ring drenado: 32 nC a 4 kHz, 30 nC a 8 e 16 kHz, 28 nC a 32 kHz,
25 nC a 64 kHz. No modo CPU: 326 nC a 4 kHz e 172 nC a 16 kHz.

Ressalvas:
- **O sensor é simulado e genérico.** A rajada de 11 B e as taxas (100 Hz a 64 kHz) são parâmetros do
  teste, não de um sensor específico. Para um sensor real, confirme no datasheet dele a taxa de
  dados, o clock SPI máximo e o tamanho da rajada.
- **Piso de ~130 a 139 µA** nos três modos a 0 Hz, inclusive no CPU, que nem arma o SPIM: o piso do
  disparo por GPIOTE30 não é o ~24 µA estimado, e a causa não foi isolada. O app vazio, 3,5 µA. Pista
  não testada: em repouso (taxa 0) a linha de data-ready do gerador fica em nível **alto**
  (`linktest`, `level=1`), e o modo CPU a 1 kHz, com a linha quase sempre baixa, repousa em ~15 µA
  entre as acordadas (captura de 100 kS/s, mediana).
- As 71 perdas a 64 kHz vieram da **fila do receptor**, não do SPI nem do DPPI: cada ciclo do laço
  principal leva ~109 a 110 ms (100 ms de espera mais ~9 ms esvaziando e imprimindo), e nesse
  intervalo chegam ~6.975 amostras para uma fila de 7.000 posições. Todos os 47 relatórios em que o
  contador de perdas subiu estavam com a fila cheia (`queued=7000`), com 1 a 3 amostras perdidas
  cada. Confirmado no log; fila de 9.000 (ou relatório a cada 50 ms) deve zerar, ainda não refeito.
- Três power-cycles por ponto, mediana. As 3 medidas do protocolo limpo diferem em no máximo 8 µA.

### Armadilhas (custaram horas)

- O buffer RTT sobrevive ao reset: o log pode ter mais de um boot, e os timestamps recomeçam do zero.
  **Calcule a taxa só com as linhas depois do último boot**; senão a taxa sai inflada (~12%, o que
  parecia um gerador impreciso).
- O gerador sem HFXO sai até +16% fora da taxa pedida.
- `west flash` recompila por padrão; use `--no-rebuild`.
- Depois da troca de um cabo USB o servidor do PPK2 pode perder o dispositivo (`Device connection lost`)
  e as leituras ficam do buffer antigo. Reconecte o PPK2 e reinicie o servidor.

## Pendente

- Verificar a ocupação real do barramento (CSN no PPK2 ou contagem de eventos END) e se o dado recebido está correto. A tentativa com SPIS na bancada mediu END do escravo ≈ taxa do gerador, mas 100% das rajadas recebidas vieram diferentes do padrão `0xC0 + i`; a causa não foi diagnosticada.
- Remedir a taxa entregue (sem RTT) para o modo CPU a 32/64 kHz e a IRQ por amostra a 64 kHz.
- Descobrir a origem do piso de ~130 a 139 µA do disparo por GPIOTE30.
- Diagramas de bloco (convencional e por hardware).
