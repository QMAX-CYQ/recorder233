#ifndef COMM_H
#define COMM_H

#include <stdint.h>

typedef enum {
    COMM_TYPE_NONE = 0,
    COMM_TYPE_USB,
    COMM_TYPE_SPI,
    COMM_TYPE_UART
} comm_type_t;

typedef struct {
    uint32_t ready;
    uint32_t rx_count;
} comm_status_t;

typedef struct {
    comm_type_t type;
    void (*init)(void);
    void (*process)(void);
    void (*get_status)(comm_status_t *status);
    volatile uint8_t initialized;
    volatile comm_status_t status;
} comm_t;

extern comm_t comm;

/* Bind once after CubeMX peripheral initialization, before service polling. */
void comm_init(void);
/* Call from the communication task only; publishes status for observers. */
void comm_poll(void);

#endif
