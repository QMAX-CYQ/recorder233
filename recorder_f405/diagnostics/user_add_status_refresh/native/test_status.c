#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "status_log.h"
static log_event_t events[100];static unsigned count,attempts;static uint8_t accept=1;
uint8_t LogEvent_Post(const log_event_t *e){attempts++;if(!accept)return 0;assert(count<100);events[count++]=*e;return 1;}
static void reset(uint32_t start){count=attempts=0;accept=1;memset(events,0,sizeof(events));StatusLog_Init(start);}
static void obs(log_module_t m,int active,int online,uint32_t rx,uint32_t now,uint32_t hal){StatusLog_Observe(m,(uint8_t)active,(uint8_t)online,rx,2,0x205,now,hal);}
int main(void){
 obs(LOG_MODULE_VISION,1,0,0,99999,99999);assert(count==0);
 reset(1000);obs(LOG_MODULE_VISION,1,0,0,5999,9999);assert(count==0);
 obs(LOG_MODULE_VISION,1,0,9950,6000,10000);assert(count==1 && events[0].kind==LOG_EVENT_OFFLINE && events[0].offline_ms==0 && events[0].last_rx_tick==5950 && events[0].can_index==2 && events[0].can_id==0x205);
 obs(LOG_MODULE_VISION,1,0,0,6999,10999);assert(count==1);
 obs(LOG_MODULE_VISION,1,0,10990,7000,11000);assert(count==2 && events[1].offline_ms==1000 && events[1].last_rx_tick==6990);
 obs(LOG_MODULE_BOARD,1,0,0,7000,11000);assert(count==3 && events[2].module==LOG_MODULE_BOARD && events[2].last_rx_tick==0);
 obs(LOG_MODULE_VISION,1,1,11099,7100,11100);assert(count==4 && events[3].kind==LOG_EVENT_RECOVERED && events[3].offline_ms==1100);
 obs(LOG_MODULE_VISION,1,1,0,7200,11200);assert(count==4);
 obs(LOG_MODULE_PIT,0,0,0,7200,11200);assert(count==4);
 obs((log_module_t)-1,1,0,0,7200,11200);obs(LOG_MODULE_COUNT,1,0,0,7200,11200);assert(count==4 && g_status_log_invalid_module==2);
 puts("PASS: startup grace, initial offline, per-module rate limit, HAL/RTOS conversion, recovery, inactive/invalid modules");
 reset(0);accept=0;obs(LOG_MODULE_VISION,1,0,0,5000,5000);obs(LOG_MODULE_VISION,1,0,0,5010,5010);assert(!count && attempts==2);
 accept=1;obs(LOG_MODULE_VISION,1,0,0,5020,5020);assert(count==1 && events[0].offline_ms==20);
 obs(LOG_MODULE_VISION,1,0,0,6019,6019);assert(count==1);obs(LOG_MODULE_VISION,1,0,0,6020,6020);assert(count==2);
 puts("PASS: queue-full offline retry does not advance successful warning timer");
 reset(0);obs(LOG_MODULE_VISION,1,0,0,5000,5000);accept=0;obs(LOG_MODULE_VISION,1,1,0,5100,5100);assert(count==1);
 accept=1;obs(LOG_MODULE_VISION,1,0,0,5200,5200);assert(count==3 && events[1].kind==LOG_EVENT_RECOVERED && events[1].offline_ms==100 && events[2].kind==LOG_EVENT_OFFLINE);
 puts("PASS: pending recovery is retried before a new offline warning");
 reset(0);obs(LOG_MODULE_VISION,1,0,0,5000,5000);accept=0;obs(LOG_MODULE_VISION,1,1,0,5100,5100);obs(LOG_MODULE_VISION,1,0,0,5200,5200);obs(LOG_MODULE_VISION,1,1,0,5350,5350);assert(count==1 && g_status_log_recovery_coalesced==1);
 accept=1;obs(LOG_MODULE_VISION,1,1,0,5400,5400);assert(count==2 && events[1].kind==LOG_EVENT_RECOVERED && events[1].offline_ms==150);
 puts("PASS: congested reconnects retain latest recovery and count coalescing");
 reset(0);obs(LOG_MODULE_VISION,1,0,0,5000,5000);accept=0;obs(LOG_MODULE_VISION,1,1,0,5100,5100);accept=1;obs(LOG_MODULE_VISION,0,0,0,5200,5200);assert(count==2 && events[1].kind==LOG_EVENT_RECOVERED);obs(LOG_MODULE_VISION,0,0,0,8000,8000);assert(count==2);
 puts("PASS: inactive module can drain an older pending recovery without new alarms");
 {uint32_t start=0xffffff00U,now=start+5000U;reset(start);obs(LOG_MODULE_VISION,1,0,0,start+4999U,0);assert(!count);obs(LOG_MODULE_VISION,1,0,0xfffffff0U,now,0x10);assert(count==1 && events[0].last_rx_tick==now-32U);obs(LOG_MODULE_VISION,1,0,0,now+1000U,0x410);assert(count==2 && events[1].offline_ms==1000);}
 puts("PASS: 32-bit tick wrap for startup grace, warning intervals and HAL timestamp conversion");return 0;
}
