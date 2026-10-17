#ifndef FAULT_LOG_PORT_H
#define FAULT_LOG_PORT_H

#include <stdint.h>

enum {
    FAULT_LOG_PORT_OK = 0,
    FAULT_LOG_PORT_BUSY = 1,
    FAULT_LOG_PORT_FAIL = 2
};

/* Implement these in fault_log_port.c for the destination firmware.
 * Link/session/disconnect/idle accessors also run with IRQs disabled: they
 * must only read state, never block or call RTOS/USB transmission functions.
 * They are not called from the fault capture handler. */
uint8_t FaultLog_PortLinkReady(void);
uint32_t FaultLog_PortSession(void);
uint32_t FaultLog_PortDisconnectCount(void);
uint8_t FaultLog_PortTxIdle(void);

/* Normal task context only; tick is in milliseconds.
 * OK means the complete line has been copied into the transport queue.
 * BUSY/FAIL must not partially enqueue a line or retain its temporary buffer. */
uint32_t FaultLog_PortTick(void);
uint8_t FaultLog_PortError(const char *format, ...);

#endif
