#ifndef FAULT_LOG_PORT_H
#define FAULT_LOG_PORT_H

#include <stdint.h>

enum {
    FAULT_LOG_PORT_OK = 0,
    FAULT_LOG_PORT_BUSY = 1,
    FAULT_LOG_PORT_FAIL = 2
};

/* 在 fault_log_port.c 中实现目标平台适配。
 * 链路、会话、重置计数及空闲查询也会在屏蔽中断时执行，只能读取状态，
 * 不得阻塞或调用 RTOS、发送函数；异常捕获处理函数不会调用这些接口。 */
uint8_t FaultLog_PortLinkReady(void);
uint32_t FaultLog_PortSession(void);
uint32_t FaultLog_PortDisconnectCount(void);
uint8_t FaultLog_PortTxIdle(void);

/* 仅供普通任务调用，时间单位为毫秒。
 * OK 表示完整日志行已复制进发送队列；BUSY/FAIL 不得部分入队，
 * 也不得保存调用方临时缓冲区的指针。 */
uint32_t FaultLog_PortTick(void);
uint8_t FaultLog_PortError(const char *format, ...);

#endif
