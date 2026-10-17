
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>
#include "log.h"
#define USBD_OK 0U
#define USBD_BUSY 1U
#define USBD_FAIL 2U
#define pdPASS 1U
#define configASSERT assert
#define taskENTER_CRITICAL() ((void)0)
#define taskEXIT_CRITICAL() ((void)0)
typedef struct {uint32_t unused;} StaticQueue_t;
typedef void *QueueHandle_t;
static log_event_t queue[32];static unsigned head,count;static uint8_t tx_status;
static char output[768];volatile uint8_t g_log_link_ready;
static uint32_t osKernelSysTick(void){return 1234;}
static void osDelay(uint32_t ms){(void)ms;}
static QueueHandle_t xQueueCreateStatic(unsigned n,unsigned size,uint8_t *buf,StaticQueue_t *control){(void)buf;(void)control;assert(n==32&&size==sizeof(log_event_t));return queue;}
static unsigned xQueueSend(QueueHandle_t h,const void *value,unsigned timeout){(void)h;(void)timeout;if(count==32)return 0;queue[(head+count++)%32]=*(const log_event_t*)value;return pdPASS;}
static unsigned uxQueueMessagesWaiting(QueueHandle_t h){(void)h;return count;}
static unsigned xQueuePeek(QueueHandle_t h,void *value,unsigned timeout){(void)h;(void)timeout;if(!count)return 0;*(log_event_t*)value=queue[head];return pdPASS;}
static unsigned xQueueReceive(QueueHandle_t h,void *value,unsigned timeout){if(!xQueuePeek(h,value,timeout))return 0;head=(head+1)%32;count--;return pdPASS;}
static uint8_t CDC_Transmit_HS(uint8_t *data,uint16_t len){if(tx_status==USBD_OK){memcpy(output,data,len);output[len]=0;}return tx_status;}

volatile log_tx_diag_t g_log_tx_diag;

uint8_t sys_printf(uint8_t level, const char *tag, const char *fmt, ...)
{
    if (level >= LOG_LEVEL_NUM || level >= LEVEL_SET || fmt == NULL)
        return USBD_FAIL;
    size_t len;
    char buf[LOG_FORMAT_BUFFER_SIZE];
    int off = 0;
    int format_len;
    uint8_t tx_status;
    uint32_t tx_tick;
    const char *log_tag = (tag != NULL) ? tag : "";

	off = snprintf(buf, sizeof(buf), "%s",log_tag);
    if (off < 0 || off >= (int)sizeof(buf))
        return USBD_FAIL;
    
    va_list args;
    va_start(args, fmt);
    format_len = vsnprintf(buf + off, sizeof(buf) - off, fmt, args);
    va_end(args);
	len = strlen(buf);
	
    /* CDC 将日志复制进 FIFO，OK 表示入队成功。 */
    if (format_len < 0) g_log_tx_diag.format_error++;
    else if ((size_t)format_len >= sizeof(buf) - (size_t)off)
        g_log_tx_diag.format_truncated++;
    g_log_tx_diag.last_line_bytes = (uint32_t)len;
    if (len > g_log_tx_diag.max_line_bytes) g_log_tx_diag.max_line_bytes = (uint32_t)len;
    tx_tick = osKernelSysTick();
    g_log_tx_diag.attempts++;
    g_log_tx_diag.attempted_bytes += (uint32_t)len;
    tx_status = CDC_Transmit_HS((uint8_t*)buf, len);
    g_log_tx_diag.last_status = tx_status;
    if (tx_status == USBD_OK) {
        uint32_t gap = tx_tick - g_log_tx_diag.last_accepted_tick;
        if ((g_log_tx_diag.accepted != 0U) && (gap > g_log_tx_diag.max_accepted_gap_ms))
            g_log_tx_diag.max_accepted_gap_ms = gap;
        g_log_tx_diag.accepted++;
        g_log_tx_diag.accepted_bytes += (uint32_t)len;
        g_log_tx_diag.last_accepted_tick = tx_tick;
        g_log_tx_diag.failure_streak = 0U;
    } else {
        if (tx_status == USBD_BUSY) g_log_tx_diag.busy++;
        else g_log_tx_diag.failed++;
        uint32_t failure_slot = g_log_tx_diag.failure_history_next % 8U;
        g_log_tx_diag.failure_tick[failure_slot] = tx_tick;
        g_log_tx_diag.failure_status[failure_slot] = tx_status;
        g_log_tx_diag.failure_history_next++;
        g_log_tx_diag.last_failure_tick = tx_tick;
        g_log_tx_diag.failure_streak++;
        if (g_log_tx_diag.failure_streak > g_log_tx_diag.max_failure_streak)
            g_log_tx_diag.max_failure_streak = g_log_tx_diag.failure_streak;
        osDelay(1);
    }
    return tx_status;
}





/* 静态事件队列不占用 FreeRTOS 堆，发送队列与事件队列各自独立。 */
static StaticQueue_t log_event_queue_control;
static uint8_t log_event_queue_storage[LOG_EVENT_QUEUE_CAPACITY * sizeof(log_event_t)];
static QueueHandle_t log_event_queue;
volatile log_event_diag_t g_log_event_diag;

void LogEvent_Init(void)
{
    if (log_event_queue == NULL) {
        log_event_queue = xQueueCreateStatic(LOG_EVENT_QUEUE_CAPACITY, sizeof(log_event_t),
                                            log_event_queue_storage, &log_event_queue_control);
        configASSERT(log_event_queue != NULL);
    }
}

uint8_t LogEvent_Post(const log_event_t *event)
{
    uint8_t accepted = 0U;
    /* 零等待提交；临界区同时保护多任务更新的统计值。禁止从中断调用。 */
    taskENTER_CRITICAL();
    if ((event == NULL) || ((uint32_t)event->module >= LOG_MODULE_COUNT) ||
        ((event->kind != LOG_EVENT_OFFLINE) && (event->kind != LOG_EVENT_RECOVERED))) {
        g_log_event_diag.invalid++;
    } else if (log_event_queue == NULL) {
        g_log_event_diag.not_initialized++;
    } else if (xQueueSend(log_event_queue, event, 0U) == pdPASS) {
        g_log_event_diag.posted++;
        g_log_event_diag.pending = (uint32_t)uxQueueMessagesWaiting(log_event_queue);
        if (g_log_event_diag.pending > g_log_event_diag.high_water)
            g_log_event_diag.high_water = g_log_event_diag.pending;
        accepted = 1U;
    } else {
        g_log_event_diag.queue_full++;
    }
    taskEXIT_CRITICAL();
    return accepted;
}

void LogEvent_Service(void)
{
    static const char *const names[LOG_MODULE_COUNT] = {
        "vision", "vtm", "gimbal_imu", "board",
        "fric_right", "fric_left", "driver_0", "driver_1", "yaw", "dji_trigger",
        "joint_0", "joint_1", "joint_2", "joint_3", "dm_trigger", "pit"
    };
    log_event_t event;
    uint8_t result;
    if ((log_event_queue == NULL) || !g_log_link_ready)
        return;
    /* 每周期最多发送一条告警，限制格式化开销；USB 忙时保留队头下次重试。 */
    if (xQueuePeek(log_event_queue, &event, 0U) != pdPASS)
        return;
    if (event.kind == LOG_EVENT_RECOVERED) {
        result = DEBUG_INFO("[recovered] module=%s send_tick=%lu last_rx_tick=%lu offline_ms=%lu can_index=%lu can_id=0x%lX\r\n",
            names[event.module], (unsigned long)osKernelSysTick(), (unsigned long)event.last_rx_tick,
            (unsigned long)event.offline_ms, (unsigned long)event.can_index, (unsigned long)event.can_id);
    } else {
        result = DEBUG_WARN("[offline] module=%s send_tick=%lu last_rx_tick=%lu offline_ms=%lu can_index=%lu can_id=0x%lX\r\n",
            names[event.module], (unsigned long)osKernelSysTick(), (unsigned long)event.last_rx_tick,
            (unsigned long)event.offline_ms, (unsigned long)event.can_index, (unsigned long)event.can_id);
    }
    taskENTER_CRITICAL();
    if (result == USBD_OK) {
        /* 只有本任务消费，发送期间其他任务只能在队尾追加。 */
        (void)xQueueReceive(log_event_queue, &event, 0U);
        g_log_event_diag.sent++;
        g_log_event_diag.pending = (uint32_t)uxQueueMessagesWaiting(log_event_queue);
    } else {
        g_log_event_diag.usb_retry++;
    }
    taskEXIT_CRITICAL();
}

int main(void){unsigned i;log_event_t e={1000,42,1,0x101,LOG_MODULE_BOARD,LOG_EVENT_OFFLINE};
 assert(!LogEvent_Post(&e)&&g_log_event_diag.not_initialized==1);LogEvent_Init();assert(!LogEvent_Post(NULL));
 for(i=0;i<32;i++)assert(LogEvent_Post(&e));assert(!LogEvent_Post(&e)&&count==32);
 LogEvent_Service();assert(count==32&&!g_log_event_diag.sent);g_log_link_ready=1;tx_status=USBD_BUSY;LogEvent_Service();assert(count==32&&g_log_event_diag.usb_retry==1);
 tx_status=USBD_OK;LogEvent_Service();assert(count==31&&strstr(output,"WARN: [offline] module=board")&&strstr(output,"offline_ms=1000"));
 for(i=0;i<31;i++)LogEvent_Service();assert(!count&&g_log_event_diag.sent==32);
 e.kind=LOG_EVENT_RECOVERED;assert(LogEvent_Post(&e));LogEvent_Service();assert(!count&&strstr(output,"INFO: [recovered]"));
 assert(sys_printf(LOG_LEVEL_NUM,"INFO: ","test")==USBD_FAIL);assert(sys_printf(LOG_INFO_LEVEL,"INFO: ",NULL)==USBD_FAIL);
 tx_status=USBD_BUSY;assert(sys_printf(LOG_ERROR_LEVEL,"ERROR: ","test")==USBD_BUSY);tx_status=USBD_OK;assert(sys_printf(LOG_ERROR_LEVEL,"ERROR: ","test")==USBD_OK&&!strcmp(output,"ERROR: test"));
 puts("PASS: actual sys_printf status, event initialization/validation, bounded queue, READY gate, BUSY preservation, offline/recovered formatting and bounded drain");return 0;}
