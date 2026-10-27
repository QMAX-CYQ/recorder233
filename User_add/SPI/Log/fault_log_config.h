#ifndef FAULT_LOG_CONFIG_H
#define FAULT_LOG_CONFIG_H

/* C 源码与预处理链接脚本共用此配置，不要仅通过 C 编译宏修改。
 * 1 表示 IRAM1/DTCM，0 表示 IRAM2/AXI SRAM，修改后须重新构建。 */
#define FAULT_LOG_TEST_IRAM1 1

/* 发布版默认只捕获真实故障；设为 1 可启用重复故障注入。 */
#ifndef FAULT_LOG_ENABLE_TEST
#define FAULT_LOG_ENABLE_TEST 0
    #if FAULT_LOG_ENABLE_TEST == 1
    #warning "FAULT_LOG_ENABLE_TEST is enabled. This is for testing only."
    #endif
#endif

#if FAULT_LOG_TEST_IRAM1 != 0 && FAULT_LOG_TEST_IRAM1 != 1
#error FAULT_LOG_TEST_IRAM1 must be 0 or 1
#endif

#endif
