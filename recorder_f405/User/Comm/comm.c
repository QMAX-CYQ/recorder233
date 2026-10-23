#include "comm.h"
#include "control_def.h"
#include <stddef.h>

#if COMM_INTERFACE == COMM_INTERFACE_USB
#include "usb_comm.h"
#endif

comm_t comm;

void comm_init(void)
{
    if (comm.initialized) return;

#if COMM_INTERFACE == COMM_INTERFACE_USB
    comm.type = COMM_TYPE_USB;
    comm.init = usb_comm_init;
    comm.process = usb_comm_process;
    comm.get_status = usb_comm_get_status;
#else
    comm.type = COMM_TYPE_NONE;
    comm.init = NULL;
    comm.process = NULL;
    comm.get_status = NULL;
#endif

    if (comm.init != NULL) comm.init();
    /* Publish only after all callbacks and backend state are initialized. */
    comm.initialized = 1U;
}

void comm_poll(void)
{
    comm_status_t status = {0U, 0U};

    if (!comm.initialized) return;
    if (comm.process != NULL) comm.process();
    if (comm.get_status != NULL) comm.get_status(&status);
    comm.status.ready = status.ready;
    comm.status.rx_count = status.rx_count;
}
