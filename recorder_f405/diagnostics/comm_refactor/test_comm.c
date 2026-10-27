#include <assert.h>
#include <stdio.h>
#include "comm.h"
#include "control_def.h"
#if COMM_INTERFACE == COMM_INTERFACE_SPI
#include "spi_comm.h"
#define usb_comm_init spi_comm_init
#define usb_comm_process spi_comm_process
#define usb_comm_get_status spi_comm_get_status
#endif

static unsigned init_calls;
static unsigned process_calls;
static comm_status_t backend_status;

void usb_comm_init(void)
{
    init_calls++;
}

void usb_comm_process(void)
{
    process_calls++;
}

void usb_comm_get_status(comm_status_t *status)
{
    *status = backend_status;
}

int main(void)
{
    comm_poll();
    assert(!comm.initialized && process_calls == 0U);
    comm_init();
    assert(comm.initialized);
#if COMM_INTERFACE == COMM_INTERFACE_USB || COMM_INTERFACE == COMM_INTERFACE_SPI
#if COMM_INTERFACE == COMM_INTERFACE_SPI
    assert(comm.type == COMM_TYPE_SPI && init_calls == 1U);
#else
    assert(comm.type == COMM_TYPE_USB && init_calls == 1U);
#endif
    assert(comm.init == usb_comm_init && comm.process == usb_comm_process);
    assert(comm.get_status == usb_comm_get_status);
    comm_init();
    assert(init_calls == 1U);
    comm_poll();
    assert(process_calls == 1U && comm.status.ready == 0U);
    backend_status.ready = 1U;
    backend_status.rx_count = UINT32_MAX;
    comm_poll();
    assert(comm.status.ready == 1U && comm.status.rx_count == UINT32_MAX);
    backend_status.rx_count = 0U;
    comm_poll();
    assert(comm.status.ready == 1U && comm.status.rx_count == 0U);
    backend_status.ready = 0U;
    comm_poll();
    assert(comm.status.ready == 0U && process_calls == 4U);
    puts("PASS: backend selection, init once, polling, link transitions, RX counter wrap");
#else
    assert(comm.type == COMM_TYPE_NONE && init_calls == 0U);
    assert(comm.init == NULL && comm.process == NULL && comm.get_status == NULL);
    comm_poll();
    assert(process_calls == 0U && comm.status.ready == 0U && comm.status.rx_count == 0U);
    puts("PASS: disabled communication has no backend calls or stale status");
#endif
    return 0;
}
