from pathlib import Path
import json, hashlib

root=Path(__file__).resolve().parent
stage=root/'ready_link';stage.mkdir(exist_ok=True)
roots={'receiver':root.parent,'sender':Path('D:/RM/rm_main_gimbal')}
manifest=[]
def read(side,name):
    p=roots[side]/name;b=p.read_bytes()
    manifest.append({'side':side,'name':name,'sha256':hashlib.sha256(b).hexdigest(),'exists':True})
    try:s=b.decode('utf-8-sig')
    except UnicodeDecodeError:s=b.decode('gbk')
    return s.replace('\r\n','\n')
def save(side,name,s,new=False):
    p=stage/side/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(s.replace('\n','\r\n').encode('utf-8'))
    if new:manifest.append({'side':side,'name':name,'exists':False})

protocol='''#ifndef LOG_LINK_PROTOCOL_H
#define LOG_LINK_PROTOCOL_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define LOG_LINK_FRAME_SIZE 16U
#define LOG_LINK_READY 1U
#define LOG_LINK_ACK 2U
typedef struct { uint8_t bytes[16]; uint8_t used; } log_link_parser_t;
typedef void (*log_link_frame_callback_t)(uint8_t type, uint32_t session);

static inline uint32_t LogLinkRead32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}
static inline void LogLinkWrite32(uint8_t *p, uint32_t value)
{
    p[0]=(uint8_t)value; p[1]=(uint8_t)(value>>8); p[2]=(uint8_t)(value>>16); p[3]=(uint8_t)(value>>24);
}
/* 控制帧：RMLG、类型、版本、长度、会话号、FNV-1a 校验值。 */
static inline uint32_t LogLinkChecksum(const uint8_t *p)
{
    uint32_t value=2166136261UL; uint32_t i;
    for(i=0;i<12U;i++) value=(value^p[i])*16777619UL;
    return value;
}
static inline void LogLinkBuild(uint8_t *p, uint8_t type, uint32_t session)
{
    memcpy(p,"RMLG",4); p[4]=type; p[5]=1U; p[6]=16U; p[7]=0U;
    LogLinkWrite32(p+8,session); LogLinkWrite32(p+12,LogLinkChecksum(p));
}
/* 控制帧可能跨包或与业务数据合包；只消费完整且校验正确的帧。
 * 非控制数据按原顺序输出，最多额外释放上一包暂存的 15 字节。
 * 调用者必须提供 input_len+15 字节空间。allow 为 0 时丢弃握手前数据。 */
static inline size_t LogLinkFilter(log_link_parser_t *state, const uint8_t *input, size_t len,
    uint8_t *output, volatile uint8_t *allow, log_link_frame_callback_t callback)
{
    size_t i, out=0; static const uint8_t magic[4]={'R','M','L','G'};
    for(i=0;i<len;i++) {
        state->bytes[state->used++]=input[i];
        while(state->used) {
            size_t prefix=state->used<4U ? state->used : 4U;
            if(memcmp(state->bytes,magic,prefix)!=0 ||
               (state->used>=8U && (state->bytes[5]!=1U || state->bytes[6]!=16U || state->bytes[7]!=0U ||
                (state->bytes[4]!=LOG_LINK_READY && state->bytes[4]!=LOG_LINK_ACK))) ||
               (state->used==16U && LogLinkRead32(state->bytes+12)!=LogLinkChecksum(state->bytes))) {
                if(allow==NULL || *allow) output[out++]=state->bytes[0];
                state->used--; memmove(state->bytes,state->bytes+1,state->used);
                continue;
            }
            if(state->used==16U) {
                callback(state->bytes[4],LogLinkRead32(state->bytes+8)); state->used=0U;
            }
            break;
        }
    }
    return out;
}
#endif
'''
save('sender','USB_DEVICE/App/log_link_protocol.h',protocol,True)
save('receiver','User/Task/log_link_protocol.h',protocol,True)

name='USB_DEVICE/App/usbd_cdc_if.h';s=read('sender',name)
s=s.replace('/* USER CODE BEGIN EXPORTED_VARIABLES */','''/* USER CODE BEGIN EXPORTED_VARIABLES */
/* 日志握手独立于 USB 枚举，收到模块 READY 并成功排队 ACK 后才置位。 */
extern volatile uint8_t g_log_link_ready;
typedef struct { uint32_t ready_rx, ack_queued, ack_failed, session, waiting_loops; } log_link_diag_t;
extern volatile log_link_diag_t g_log_link_diag;''',1)
s=s.replace('/* USER CODE BEGIN EXPORTED_FUNCTIONS */','/* USER CODE BEGIN EXPORTED_FUNCTIONS */\nvoid LogLinkService_HS(void);',1)
save('sender',name,s)
name='USB_DEVICE/App/usbd_cdc_if.c';s=read('sender',name)
s=s.replace('/* USER CODE BEGIN INCLUDE */','/* USER CODE BEGIN INCLUDE */\n#include "log_link_protocol.h"',1)
s=s.replace('/* USER CODE BEGIN PV */','''/* USER CODE BEGIN PV */
volatile uint8_t g_log_link_ready;
volatile log_link_diag_t g_log_link_diag;
static volatile uint8_t log_link_ack_needed;
static log_link_parser_t log_link_parser;
static uint8_t log_link_filtered[APP_RX_DATA_SIZE+16U];''',1)
s=s.replace('/* USER CODE BEGIN PRIVATE_FUNCTIONS_DECLARATION */','/* USER CODE BEGIN PRIVATE_FUNCTIONS_DECLARATION */\nstatic void LogLinkOnFrame(uint8_t type, uint32_t session);',1)
s=s.replace('  CDC_DIAG_ADD(init_count, 1U);','  g_log_link_ready = 0U;\n  log_link_ack_needed = 0U;\n  log_link_parser.used = 0U;\n  CDC_DIAG_ADD(init_count, 1U);',1)
s=s.replace('  CDC_DIAG_ADD(deinit_count, 1U);','  g_log_link_ready = 0U;\n  log_link_ack_needed = 0U;\n  log_link_parser.used = 0U;\n  CDC_DIAG_ADD(deinit_count, 1U);',1)
start=s.index('  /* USER CODE BEGIN 11 */');end=s.index('  /* USER CODE END 11 */',start)
s=s[:start]+'''  /* USER CODE BEGIN 11 */
  size_t payload_len;
  payload_len = LogLinkFilter(&log_link_parser, Buf,
      (*Len > APP_RX_DATA_SIZE) ? APP_RX_DATA_SIZE : *Len,
      log_link_filtered, NULL, LogLinkOnFrame);
  /* READY 不交给视觉解析器，其他数据保持原顺序。 */
  if (payload_len != 0U) vision_get_data(log_link_filtered, (uint16_t)payload_len);
  USBD_CDC_SetRxBuffer(&hUsbDeviceHS, &Buf[0]);
  USBD_CDC_ReceivePacket(&hUsbDeviceHS);
  return (USBD_OK);
'''+s[end:]
s=s.replace('/* USER CODE BEGIN PRIVATE_FUNCTIONS_IMPLEMENTATION */','''/* USER CODE BEGIN PRIVATE_FUNCTIONS_IMPLEMENTATION */
static void LogLinkOnFrame(uint8_t type, uint32_t session)
{
  if (type != LOG_LINK_READY) return;
  if (g_log_link_diag.session != session) g_log_link_ready = 0U;
  g_log_link_diag.session = session;
  g_log_link_diag.ready_rx++;
  log_link_ack_needed = 1U;
}

void LogLinkService_HS(void)
{
  uint8_t ack[16]; uint8_t result;
  uint32_t irq_state = __get_PRIMASK();
  __disable_irq();
  if (hUsbDeviceHS.dev_state != USBD_STATE_CONFIGURED) {
    g_log_link_ready = 0U;
  } else if (log_link_ack_needed) {
    LogLinkBuild(ack, LOG_LINK_ACK, g_log_link_diag.session);
    /* 同一 FIFO 中 ACK 排在新日志之前，接收端先确认握手再接收日志。 */
    result = CDC_Transmit_HS(ack, sizeof(ack));
    if (result == USBD_OK) {
      log_link_ack_needed = 0U;
      g_log_link_ready = 1U;
      g_log_link_diag.ack_queued++;
    } else g_log_link_diag.ack_failed++;
  }
  __set_PRIMASK(irq_state);
}
''',1)
save('sender',name,s)
name='User/Task/debug_task.c';s=read('sender',name)
s=s.replace('    for (;;) {\n        uint32_t tick = osKernelSysTick();','''    for (;;) {
        uint32_t tick;
        LogLinkService_HS();
        CDC_TxService_HS();
        if (!g_log_link_ready) {
            /* 等待期间不生成日志、不推进 seq/通道，也不补跑等待时间。 */
            g_log_link_diag.waiting_loops++;
            wake_time = osKernelSysTick();
            osDelay(DEBUG_LOG_PERIOD_MS);
            continue;
        }
        tick = osKernelSysTick();''',1)
# 等待重连不作为日志任务调度迟到统计；正常运行周期保持原样。
s=s.replace('            wake_time = osKernelSysTick();\n            osDelay(', '            wake_time = osKernelSysTick();\n            g_debug_task_diag.last_tick = 0U;\n            osDelay(',1)
s=s.replace('if (g_debug_task_diag.loops != 0U) {','if (g_debug_task_diag.last_tick != 0U) {',1)
save('sender',name,s)

name='User/Recorder/recorder.h';s=read('receiver',name).replace('FRESULT SdCardInit(void);','FRESULT SdCardInit(void);\n/* 文件头已经写入并同步、文件可以接收日志。 */\nuint8_t RecorderLogIsReady(void);')
save('receiver',name,s)
name='User/Recorder/recorder.c';s=read('receiver',name)
s=s.replace('volatile recorder_diag_t g_recorder_diag;','volatile recorder_diag_t g_recorder_diag;\nstatic volatile uint8_t recorder_log_ready;\n\nuint8_t RecorderLogIsReady(void)\n{\n    return recorder_log_ready;\n}',1)
s=s.replace('    g_recorder_diag.card_not_ready++;','    recorder_log_ready = 0U;\n    g_recorder_diag.card_not_ready++;',1)
s=s.replace('    // 新建当前文件\n','    /* 创建完成前不发布写卡就绪。 */\n    recorder_log_ready = 0U;\n    // 新建当前文件\n',1)
s=s.replace('    pcfile_status.current_index = next_index;','''    /* 文件头落盘成功后才允许主板开始生成日志。 */
    if (f_sync(&pcfile_now) != FR_OK) {
        f_close(&pcfile_now);
        return;
    }
    recorder_log_ready = 1U;
    pcfile_status.current_index = next_index;''',1)
s=s.replace('        pcfile_status.state = RS_READY;','        pcfile_status.state = recorder_log_ready ? RS_READY : RS_INIT;',1)
s=s.replace('void RecorderInit(void)\n{','void RecorderInit(void)\n{\n    recorder_log_ready = 0U;',1)
save('receiver',name,s)

name='User/Task/usb_task.h';s=read('receiver',name)
s=s.replace('void usb_task(void const *argument);','''/* 接收模块的启动握手统计。 */
typedef struct {
    uint32_t session, ready_attempts, ready_started, ready_completed, ack_received, tx_failed;
    uint32_t discarded_before_ready;
} log_host_link_diag_t;
extern volatile log_host_link_diag_t g_log_host_link_diag;
extern volatile uint8_t g_log_host_ack_received;
void usb_task(void const *argument);''',1)
save('receiver',name,s)
name='User/Task/usb_task.c';s=read('receiver',name)
s=s.replace('#include <string.h>','#include <string.h>\n#include "log_link_protocol.h"',1)
s=s.replace('uint8_t usb_buffer[512]', '''volatile log_host_link_diag_t g_log_host_link_diag;
volatile uint8_t g_log_host_ack_received;
static volatile uint8_t log_host_tx_pending;
static uint8_t log_host_rx_armed;
static uint8_t log_host_session_active;
static uint32_t log_host_last_ready_tick;
static uint32_t log_host_session_counter;
static uint8_t log_host_ready_frame[16];
static log_link_parser_t log_host_parser;
static uint8_t log_host_filtered[512U+16U];

static void log_host_on_frame(uint8_t type, uint32_t session)
{
    if (type == LOG_LINK_ACK && log_host_session_active &&
        session == g_log_host_link_diag.session) {
        if (!g_log_host_ack_received) g_log_host_link_diag.ack_received++;
        g_log_host_ack_received = 1U;
    }
}

void USBH_CDC_TransmitCallback(USBH_HandleTypeDef *phost)
{
    if (phost == &hUsbHostHS) {
        log_host_tx_pending = 0U;
        g_log_host_link_diag.ready_completed++;
    }
}

uint8_t usb_buffer[512]''',1)
# 以上代码引用全局主机句柄，在结构定义之前显式声明。
s=s.replace('volatile log_host_link_diag_t g_log_host_link_diag;', 'extern USBH_HandleTypeDef hUsbHostHS;\nvolatile log_host_link_diag_t g_log_host_link_diag;',1)
s=s.replace('    rx_len = USBH_CDC_GetLastReceivedDataSize(phost);','''    rx_len = USBH_CDC_GetLastReceivedDataSize(phost);
    {
        size_t filtered;
        uint16_t input_len = rx_len > sizeof(usb_buffer) ? sizeof(usb_buffer) : rx_len;
        filtered = LogLinkFilter(&log_host_parser, usb_buffer, input_len,
            log_host_filtered, &g_log_host_ack_received, log_host_on_frame);
        if (!g_log_host_ack_received) g_log_host_link_diag.discarded_before_ready += input_len;
        /* 当前接收请求为 64 字节，暂存前缀最多追加 15 字节，512 字节足够。 */
        if (filtered <= sizeof(usb_buffer)) {
            memcpy(usb_buffer, log_host_filtered, filtered);
            rx_len = (uint16_t)filtered;
        } else rx_len = 0U;
    }''',1)
# 在主机未就绪分支重置握手；初始化建立会话只在 APPLICATION_READY 内进行。
s=s.replace('        if (Appli_state == APPLICATION_READY)\n        {','''        if (Appli_state == APPLICATION_READY)
        {
            if (!log_host_session_active) {
                log_host_session_counter++;
                g_log_host_link_diag.session = HAL_GetTick() ^ (log_host_session_counter * 2654435761UL);
                log_host_session_active = 1U;
                g_log_host_ack_received = 0U;
                log_host_rx_armed = 0U;
                log_host_parser.used = 0U;
                log_host_last_ready_tick = HAL_GetTick() - 200U;
            }''',1)
s=s.replace('                    if (receive_status != USBH_OK)\n','                    if (receive_status == USBH_OK) log_host_rx_armed = 1U;\n                    if (receive_status != USBH_OK)\n',1)
marker='''            }
        }
        else
        {
            usb_rx_pending = 0U;'''
assert marker in s
s=s.replace(marker,'''            }
            /* 接收请求已提交且日志文件可写，再发送 READY。
             * 提交前置 pending，防止快速完成回调清零后被覆盖。 */
            if (log_host_rx_armed && RecorderLogIsReady() && !g_log_host_ack_received &&
                !log_host_tx_pending && cdc_handle != NULL && cdc_handle->data_tx_state == CDC_IDLE &&
                (HAL_GetTick() - log_host_last_ready_tick >= 200U)) {
                USBH_StatusTypeDef tx_status;
                LogLinkBuild(log_host_ready_frame, LOG_LINK_READY, g_log_host_link_diag.session);
                log_host_tx_pending = 1U;
                log_host_last_ready_tick = HAL_GetTick();
                g_log_host_link_diag.ready_attempts++;
                tx_status = USBH_CDC_Transmit(&hUsbHostHS, log_host_ready_frame, sizeof(log_host_ready_frame));
                if (tx_status == USBH_OK) g_log_host_link_diag.ready_started++;
                else {
                    log_host_tx_pending = 0U;
                    g_log_host_link_diag.tx_failed++;
                }
            }
        }
        else
        {
            log_host_session_active = 0U;
            log_host_rx_armed = 0U;
            log_host_tx_pending = 0U;
            g_log_host_ack_received = 0U;
            log_host_parser.used = 0U;
            usb_rx_pending = 0U;''',1)
save('receiver',name,s)
(stage/'manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
print('Prepared READY/ACK protocol and both firmware changes')
