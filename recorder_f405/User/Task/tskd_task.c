#include "tskd_task.h"
#include "cmsis_os.h"
#include "recorder.h"
#include "recorder_inter.h"

void tskd_Task(const void *argu)
{
    RecorderInit();
    for (;;)
    {
        RecorderStateUpdate();
        osDelay(TSKD_FRE);
    }
}
