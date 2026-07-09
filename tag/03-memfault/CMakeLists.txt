# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(beacon)

target_sources(app PRIVATE src/main.c)

# Memfault SDK config overrides (heartbeat metric keys, platform config).
zephyr_include_directories(memfault_config)
