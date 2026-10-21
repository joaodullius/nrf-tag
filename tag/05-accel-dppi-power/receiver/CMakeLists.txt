# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(accel_power_receiver)

target_sources(app PRIVATE src/main.c src/spim_dppi.c src/sensor_none.c)
target_sources_ifdef(CONFIG_APP_ACQ_CPU app PRIVATE src/cpu_path.c)
