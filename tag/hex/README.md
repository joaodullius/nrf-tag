# tag/hex — firmware pronto para gravar

Binários prontos para gravar nas Tags **sem compilar**. Cada `.hex` é o `merged` do sysbuild (MCUboot + app).
Alvo: `nrf54l15tag/nrf54l15/cpuapp` · NCS v3.4.1.

## 03_memfault_tag (exemplo 03: MCUboot + FOTA + Memfault)

Duas versões do mesmo firmware, para gravar a 1.0.0 e atualizar para a 2.0.0 por FOTA:

| Versão | Build ID (GNU) | O que muda |
|---|---|---|
| **1.0.0+0** | `522af75443d5895ecef9c64be3b709b931b9c7c8` | firmware base; LED de estado (azul parado, vermelho movendo) aceso fixo |
| **2.0.0+0** | `d1bf54e7d7304615fd3fd0781eeeb783384d210f` | **só para a demonstração do FOTA:** o LED de estado pisca rápido (100 ms). Gasta mais energia; não é para uso real |

Cada versão tem três arquivos:

| Arquivo | Para que serve |
|---|---|
| `03_memfault_tag_<versão>.hex` | gravação direta (MCUboot + app) |
| `03_memfault_tag_<versão>.elf` | **arquivo de símbolos** para o Memfault (um por versão) |
| `03_memfault_tag_<versão>_dfu.zip` | atualização por BLE: é o **payload da release** no nRF Cloud |

- O `.hex`, o `.elf` e o `.zip` de uma versão são **do mesmo build**. Não recompile: um build novo muda o Build ID e os coredumps deixam de ser decodificados.
- O `.zip` contém `<app>.signed.bin` e `manifest.json`. Na v2 o nome interno do app é `src03v2` (veio do diretório de build); não tem efeito.
- Cada Tag anuncia como `Tag-XXXX` (4 últimos dígitos do ID de hardware) e aparece no Memfault com o serial de 16 dígitos terminado igual. Todas as Tags gravadas com estes arquivos entram no mesmo projeto (`nrf-tag`).
- A versão que o firmware reporta ao Memfault (`CONFIG_MEMFAULT_NCS_FW_VERSION`) e a gravada na imagem do MCUboot (`CONFIG_MCUBOOT_IMGTOOL_SIGN_VERSION`) são a mesma, `1.0.0+0` / `2.0.0+0`.

### Gravar

A Tag grava pelo `DEBUG OUT` de uma DK (Tag encaixada e alimentada; o debugger da DK aponta para ela).

```
nrfutil device list
nrfutil device program --firmware tag/hex/03_memfault_tag_1.0.0.hex --options chip_erase_mode=ERASE_ALL --serial-number <serial da DK>
nrfutil device reset --serial-number <serial da DK>
```

### Usar

1. Segure o botão da Tag por **4 s**: LED verde piscando = janela de conexão (60 s).
2. No celular, abra o **nRF Connect Device Manager**, conecte em `Tag-XXXX` e abra a aba Diagnostics: os dados sobem para o Memfault.
3. Falha provocada (para gerar um coredump): segure o botão por **15 s**. O LED vermelho pisca rápido a partir de 12 s e o firmware reinicia aos 15 s. Reconecte (passo 1) para enviar o coredump.

### FOTA da 1.0.0 para a 2.0.0 pelo nRF Cloud

Preparação (uma vez, no painel do projeto):

1. **Symbol Files:** suba `03_memfault_tag_2.0.0.elf` (e o da 1.0.0, se ainda não subiu).
2. **Software → OTA Releases → Create Release:** versão `2.0.0+0`.
3. **Add OTA payload:** Hardware Version `nrf54l15tag`, Software Type `app`, arquivo `03_memfault_tag_2.0.0_dfu.zip`. O hardware e o software type precisam ser os que o dispositivo reporta; senão o app diz "up to date".
4. **Activate:** cohort `default`, rollout `Normal`.

Na Tag (gravada com a 1.0.0), no celular:

1. Segure o botão 4 s e conecte no **nRF Connect Device Manager**.
2. Aba **Image** → **Check for Updates** → **Download** → **Start**. Quando o app pedir o modo, escolha **Confirm Only**.
3. Ao terminar, a Tag reinicia: o MCUboot valida a assinatura e troca a imagem. O LED passa a **piscar rápido** e o dispositivo aparece no Memfault com a versão `2.0.0+0`.

A confirmação da imagem é feita pelo aplicativo do celular ("Confirm Only"); o firmware não chama `boot_write_img_confirmed()`.

### Integridade (SHA-256, primeiros 16 caracteres)

- `03_memfault_tag_1.0.0.elf`: 30f7a5e60546b209…
- `03_memfault_tag_1.0.0.hex`: 052250ad12cdf208…
- `03_memfault_tag_1.0.0_dfu.zip`: 626205080caedbaf…
- `03_memfault_tag_2.0.0.elf`: 0198fc7d39eebf5d…
- `03_memfault_tag_2.0.0.hex`: 075d0301a2279e68…
- `03_memfault_tag_2.0.0_dfu.zip`: dd06d495b61f3ca9…
