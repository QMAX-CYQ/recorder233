#include "comm_task.h"
#include "comm.h"
#include "cmsis_os.h"

void comm_task(void const *argument)
{
    uint32_t wake_time;
    (void)argument;

    for (;;) {
        wake_time = osKernelSysTick();
        comm_poll();
        osDelayUntil(&wake_time, 1U);
    }
}
