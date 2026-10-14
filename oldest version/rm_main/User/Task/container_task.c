#include "container_task.h"
#include "cmsis_os.h"
#include "container_bus.h"

void container_task(const void *argu)
{
	uint32_t thread_wake_time = osKernelSysTick();
	for(;;)
	{
		thread_wake_time = osKernelSysTick();
	
		container_bus_poll();//contain_bus 
		
		osDelayUntil(&thread_wake_time,10);
	}
}
