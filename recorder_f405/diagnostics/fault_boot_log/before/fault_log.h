#ifndef FAULT_LOG_H
#define FAULT_LOG_H

#include <stdint.h>

/* 本次测试启用自动故障注入；正常固件设为 0，避免运行中人为停机。 */
#ifndef FAULT_LOG_ENABLE_TEST
#define FAULT_LOG_ENABLE_TEST 1
#endif
#define FAULT_LOG_TEST_DELAY_MS 10000U
#define FAULT_LOG_MAGIC 0x48464C47UL
#define FAULT_LOG_VERSION 1U
#define FAULT_LOG_STACK_BYTES 1024U

typedef struct {
    uint32_t magic, version, size, checksum;
    uint32_t pending, count, exception, tick, tick_valid, reset_before;
    uint32_t stack_valid, frame[8], callee[8];
    uint32_t msp, psp, exc_return, control, primask, basepri, faultmask;
    uint32_t cfsr, hfsr, dfsr, afsr, bfar, mmfar, shcsr, icsr, vtor, abfsr, fpccr;
} fault_record_t;

typedef struct {
    uint32_t pending, valid, invalid, cold_boot, reset_after;
    uint32_t reports, retries, replay_restarts, part;
    uint32_t last_tick, tick_valid;
} fault_log_diag_t;
extern volatile fault_record_t g_fault_record;
extern volatile fault_log_diag_t g_fault_log_diag;

/* main 最早期调用；异常处理本身不依赖 HAL、RTOS 或 USB。 */
void FaultLog_BootInit(void);
void FaultLog_Heartbeat(uint32_t tick);
/* 返回 1 表示故障补报尚未结束，debug_task 暂缓普通日志。 */
uint8_t FaultLog_Service(void);

/* 每轮由 debug_task 调用，关闭测试宏时为空操作。 */
void FaultLog_TestService(uint32_t tick);

#if FAULT_LOG_ENABLE_TEST
extern volatile uint32_t g_fault_test_latch[2];
/* 普通 RAM：Watch 写 1 重新开始 10 秒倒计时，写 2 取消手动倒计时。
 * 请求被消费后自动清零；复位后不保留，不需要修改 noinit 标记。 */
extern volatile uint32_t g_fault_test_request;
/* 状态：0 空闲、1 倒计时、2 已触发；剩余时间单位为毫秒。 */
extern volatile uint32_t g_fault_test_state;
extern volatile uint32_t g_fault_test_remaining_ms;
void FaultLog_TestTrigger(void);
#endif

#endif
