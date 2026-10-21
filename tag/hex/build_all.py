#!/usr/bin/env python3
"""
build_all.py - gera os .hex de referencia de cada exemplo de tag/.

Cada variante e compilada PRISTINE num build dir proprio (build/hex/<nome>) com
sysbuild. Todo exemplo tem um sysbuild.conf com SB_CONFIG_MERGED_HEX_FILES=y, que
faz o sysbuild gravar merged_<board>.hex no build dir: o hex unico (app, MCUboot,
FLPR, quando houver) que se grava com `nrfutil device program`, sem west.

Uso (de qualquer pasta):
    python tag/hex/build_all.py              # todas as variantes
    python tag/hex/build_all.py 04 06        # so as que comecam com 04 ou 06
    python tag/hex/build_all.py --list

Requisitos: nRF Connect SDK v3.4.1 via nrfutil sdk-manager (C:/ncs/v3.4.1). O 06 e o 07 usam
o Edge AI Add-on como modulo extra (C:/ncs/sdk-edge-ai/edge-ai).

O 03-memfault NAO e gerado aqui: seus hex/elf/zip (1.0.0 e 2.0.0) estao congelados nesta pasta
e recompilar trocaria o Build ID, que e o que casa o firmware com os simbolos no Memfault.
"""

import shutil
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
TAGDIR = REPO / "tag"
HEX = TAGDIR / "hex"
BUILD = REPO / "build" / "hex"
NCS_WS = "C:/ncs/v3.4.1"
NCS_VER = "v3.4.1"
EDGE_AI = "C:/ncs/sdk-edge-ai/edge-ai"

TAG = "nrf54l15tag/nrf54l15/cpuapp"
DK15 = "nrf54l15dk/nrf54l15/cpuapp"


def mode(app, name):
    return [f"-DEXTRA_CONF_FILE={(TAGDIR / app / 'modes' / (name + '.conf')).as_posix()}"]


# nome do hex (sem extensao), pasta do app (sob tag/), placa, argumentos extras do cmake
VARIANTES = [
    ("00_starter_tag", "00-starter", TAG, []),
    ("00_starter_dk15", "00-starter", DK15, []),
    ("01_motion_adv_tag", "01-motion-adv", TAG, []),
    ("01_motion_adv_flpr_tag", "01-motion-adv-flpr", TAG, []),
    ("02_mcuboot_tag", "02-mcuboot", TAG, []),
    ("04_accel_dppi_drdy_tag", "04-accel-dppi-drdy", TAG, []),
    ("04_accel_dppi_drdy_por_amostra_tag", "04-accel-dppi-drdy", TAG, ["-DCONFIG_APP_PER_SAMPLE_IRQ=y"]),
    ("06_neuton_transporte_tag", "06-neuton-transporte", TAG, [f"-DEXTRA_ZEPHYR_MODULES={EDGE_AI}"]),
    ("07_neuton_ventilador_tag", "07-neuton-ventilador", TAG, [f"-DEXTRA_ZEPHYR_MODULES={EDGE_AI}"]),
    ("05_accel_dppi_power_cpu_dk15", "05-accel-dppi-power/receiver", DK15, mode("05-accel-dppi-power/receiver", "cpu")),
    ("05_accel_dppi_power_sample_dk15", "05-accel-dppi-power/receiver", DK15, mode("05-accel-dppi-power/receiver", "dppi-sample")),
    ("05_accel_dppi_power_drain_dk15", "05-accel-dppi-power/receiver", DK15, mode("05-accel-dppi-power/receiver", "dppi-drain-q9")),
]


def build(nome, app, board, extra):
    bdir = BUILD / nome
    cmd = [
        "nrfutil", "sdk-manager", "toolchain", "launch", "--ncs-version", NCS_VER,
        "--chdir", NCS_WS, "--",
        "west", "build", "-s", str(TAGDIR / app), "-d", str(bdir), "-b", board,
        "-p", "always", "--sysbuild",
    ]
    if extra:
        cmd += ["--"] + extra
    print(f"== {nome}: {app} @ {board}", flush=True)
    r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if r.returncode != 0:
        print(r.stdout[-3000:])
        raise RuntimeError(f"build de {nome} falhou (exit {r.returncode})")
    tail = [l for l in r.stdout.splitlines() if "FLASH:" in l or "RAM:" in l]
    for l in tail[-2:]:
        print("  ", l.strip())
    merged = sorted(bdir.glob("merged*.hex"))
    if not merged:
        raise RuntimeError(f"{nome}: o sysbuild nao gerou merged*.hex em {bdir}")
    shutil.copyfile(merged[0], HEX / f"{nome}.hex")
    print(f"   -> tag/hex/{nome}.hex ({merged[0].name}, {merged[0].stat().st_size} B)")


def main():
    args = sys.argv[1:]
    if args == ["--list"]:
        for v in VARIANTES:
            print(v[0])
        return 0
    sel = [v for v in VARIANTES if not args or any(v[0].startswith(a) for a in args)]
    falhas = []
    for v in sel:
        try:
            build(*v)
        except Exception as e:  # segue para as demais e resume no fim
            print("   FALHOU:", e)
            falhas.append(v[0])
    print(f"\n{len(sel) - len(falhas)}/{len(sel)} hex gerados", ("; falhas: " + ", ".join(falhas)) if falhas else "")
    return 1 if falhas else 0


if __name__ == "__main__":
    sys.exit(main())
