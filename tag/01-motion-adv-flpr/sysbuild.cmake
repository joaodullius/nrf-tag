# SPDX-License-Identifier: Apache-2.0

# The FLPR core needs the nordic-flpr snippet, not just `&cpuflpr_vpr {
# status = "okay"; }` in the board overlay: CONFIG_NORDIC_VPR_LAUNCHER (the
# driver that actually starts the coprocessor at boot) also depends on the
# vpr node having an `execution-memory` property, which only the snippet
# provides - along with the source-memory/RRAM and SRAM carve-out the FLPR
# image links against, and which our ipc0 reserved-memory regions below
# assume. Force it here so `west build -b nrf54l15tag/nrf54l15/cpuapp
# tag/01-motion-adv-flpr` works with no extra flag, same as
# nrf/samples/event_manager_proxy forces nordic-ppr for its nRF54H20 case.
#
# The per-image snippet variable is keyed by DEFAULT_IMAGE (the app
# directory's basename, e.g. "01-motion-adv-flpr" - see
# zephyr/share/sysbuild/cmake/modules/sysbuild_images.cmake), NOT by this
# app's CMakeLists.txt `project()` name ("beacon", inherited from
# 01-motion-adv) - those two happen to be the same string in Nordic's own
# event_manager_proxy sample, which is a coincidence, not the rule.
if(${NORMALIZED_BOARD_TARGET} STREQUAL "nrf54l15tag_nrf54l15_cpuapp")
  if(NOT nordic-flpr IN_LIST ${DEFAULT_IMAGE}_SNIPPET)
    set(${DEFAULT_IMAGE}_SNIPPET nordic-flpr CACHE STRING "" FORCE)
  endif()
endif()

# Add the FLPR (remote) motion-sampling image. Same sysbuild wiring pattern
# Nordic uses for cpuapp+cpuflpr in nrf/samples/event_manager_proxy (NCS
# v3.4.0) - PM_CPUNET_* naming is generic sysbuild plumbing, not specific to
# an actual net core.
ExternalZephyrProject_Add(
  APPLICATION remote
  SOURCE_DIR ${APP_DIR}/remote
  BOARD ${SB_CONFIG_REMOTE_BOARD}
  BOARD_REVISION ${BOARD_REVISION}
)

set_property(GLOBAL APPEND PROPERTY PM_DOMAINS CPUNET)
set_property(GLOBAL APPEND PROPERTY PM_CPUNET_IMAGES remote)
set_property(GLOBAL PROPERTY DOMAIN_APP_CPUNET remote)
set(CPUNET_PM_DOMAIN_DYNAMIC_PARTITION remote CACHE INTERNAL "")

# Build/flash the FLPR image first.
sysbuild_add_dependencies(CONFIGURE ${DEFAULT_IMAGE} remote)
sysbuild_add_dependencies(FLASH ${DEFAULT_IMAGE} remote)
