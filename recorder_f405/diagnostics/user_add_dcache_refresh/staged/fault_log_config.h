#ifndef FAULT_LOG_CONFIG_H
#define FAULT_LOG_CONFIG_H

/* Shared by C and the preprocessed scatter file. Edit here, not in C-only
 * compiler defines: 1 = IRAM1/DTCM, 0 = IRAM2/AXI SRAM. Rebuild after changing. */
#define FAULT_LOG_TEST_IRAM1 1

/* Distribution default: real faults only. Set to 1 for repeated injection. */
#ifndef FAULT_LOG_ENABLE_TEST
#define FAULT_LOG_ENABLE_TEST 0
#endif

#if FAULT_LOG_TEST_IRAM1 != 0 && FAULT_LOG_TEST_IRAM1 != 1
#error FAULT_LOG_TEST_IRAM1 must be 0 or 1
#endif

#endif
