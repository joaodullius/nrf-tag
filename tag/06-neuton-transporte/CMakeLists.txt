#
# Copyright (c) 2025 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

cmake_minimum_required(VERSION 3.20.0)

# Zephyr CMake project
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(neuton_transporte)

# Modelo que entra no binario: subpasta de src/nrf_edgeai_generated/ com os
# arquivos gerados pelo Edge AI Lab (nrf_edgeai_user_model.c/.h e
# nrf_edgeai_user_types.h). Trocar o modelo = trocar este nome e o
# src/model_cfg.h (contrato, cores das classes e leitura do IMU). Recompile com -p:
# o CMake so rele este arquivo num build pristine.
set(MODELO "Neuton")

set(NN_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/nrf_edgeai_generated/${MODELO}")
if(NOT EXISTS "${NN_SOURCE}/nrf_edgeai_user_model.c")
  message(FATAL_ERROR "Modelo '${MODELO}' nao encontrado em ${NN_SOURCE}.")
endif()
message(STATUS "Edge AI model: ${MODELO}")

target_sources(app PRIVATE ${NN_SOURCE}/nrf_edgeai_user_model.c)
target_include_directories(app PRIVATE ${NN_SOURCE})

target_sources(app PRIVATE src/main.c)
target_include_directories(app PRIVATE src)
