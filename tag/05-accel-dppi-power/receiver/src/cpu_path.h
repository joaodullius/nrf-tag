/*
 * Conventional acquisition for the comparison: the data-ready edge interrupts the CPU; it wakes a
 * thread, which runs one SPI transfer and queues the
 * burst. Same SPIM, same pins, same queue as the DPPI paths; only the CPU is
 * in the loop.
 */
#ifndef CPU_PATH_H_
#define CPU_PATH_H_

#include <stdint.h>

/* Arm the data-ready interrupt (after spim_dppi_init) */
int cpu_path_start(void);

/* Transfers completed and samples that did not fit in the queue */
uint32_t cpu_path_xfers(void);
uint32_t cpu_path_dropped(void);

#endif /* CPU_PATH_H_ */
