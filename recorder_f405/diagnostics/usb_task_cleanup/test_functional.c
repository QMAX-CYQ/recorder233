
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include "usb_task.h"
#include "log_link_protocol.h"
#include <setjmp.h>
#define USBH_OK 0U
#define USBH_FAIL 2U
#define USBH_BUSY 1U
#define USBH_PORT_EVENT 1U
#define HOST_IDLE 0U
#define ENUM_IDLE 0U
#define CMD_SEND 0U
#define CTRL_SETUP 0U
#define USBH_MPS_DEFAULT 64U
#define USBH_ADDRESS_DEFAULT 0U
#define USBH_SPEED_FULL 1U
#define USBH_MAX_PIPES_NBR 16U
#define USBH_MAX_DATA_BUFFER 64U
#define APPLICATION_READY 2U
#define APPLICATION_START 1U
#define USB_OTG_HOST_PORT_BASE 0x200U
#define USB_OTG_HOST_CHANNEL_BASE 0x300U
#define USB_OTG_HOST_CHANNEL_SIZE 0x40U
#define USB_OTG_HCCHAR_CHENA (1U << 31)
#define USB_OTG_HCCHAR_CHDIS (1U << 30)
#define USB_OTG_HPRT_PCSTS 1U
#define OTG_HS_IRQn 0
#define OTG_HS_EP1_IN_IRQn 1
#define OTG_HS_EP1_OUT_IRQn 2
#define USBH_memset memset
typedef uint32_t USBH_StatusTypeDef;
typedef uint32_t HAL_StatusTypeDef;
#define HAL_OK 0U
#define HAL_ERROR 1U
#define HAL_TIMEOUT 3U
#define URB_ERROR 4U
#define URB_STALL 5U
typedef struct { uint32_t unused; } USB_OTG_GlobalTypeDef;
typedef struct { uint32_t HCCHAR; } USB_OTG_HostChannelTypeDef;
typedef struct { uint32_t Host_channels; } HCD_InitTypeDef;
typedef struct { uint32_t urb_state; } HC;
typedef struct { USB_OTG_GlobalTypeDef *Instance; HCD_InitTypeDef Init; HC hc[12]; void *pData; } HCD_HandleTypeDef;
typedef struct { struct {uint32_t InPipe,InEpSize;} DataItf; uint32_t data_tx_state; } CDC_HandleTypeDef;
typedef struct USBH_HandleTypeDef USBH_HandleTypeDef;
typedef struct { void *pData; USBH_StatusTypeDef (*DeInit)(USBH_HandleTypeDef *); } USBH_ClassTypeDef;
struct USBH_HandleTypeDef {
    uint32_t Pipes[16], gState, EnumState, RequestState, Timer;
    struct { uint32_t PortEnabled,is_connected,is_disconnected,is_ReEnumerated,RstCnt,EnumCnt,address,speed;
        uint8_t Data[64],CfgDesc_Raw[8],DevDesc[8],CfgDesc[8]; } device;
    struct { uint32_t state,pipe_size,errorcount,pipe_in,pipe_out; } Control;
    USBH_ClassTypeDef *pActiveClass;
    void *pData, *thread, *os_event, *pClass[1]; uint32_t ClassNumber;
};
static uint64_t hw[0x1000/8];
#define USB_OTG_HS ((USB_OTG_GlobalTypeDef *)hw)
static USBH_HandleTypeDef hUsbHostHS;
static HCD_HandleTypeDef hhcd_USB_OTG_HS;
static USBH_ClassTypeDef cdc_class;
typedef enum {
    USB_HOST_RECOVERY_IDLE = 0,
    USB_HOST_RECOVERY_REQUESTED,
    USB_HOST_RECOVERY_RUNNING,
    USB_HOST_RECOVERY_WAIT_LINK,
    USB_HOST_RECOVERY_FAILED
} usb_host_recovery_state_t;

static volatile struct {
    usb_host_recovery_state_t state;
    uint32_t start_tick, restart_tick, callback_baseline;
    uint8_t attempted;
} usb_recovery;


static uint8_t usb_rx_pending,usb_rx_ready,usb_rx_waiting_for_ring;
static uint16_t usb_rx_len;
static uint32_t usb_rx_callback_count,usb_rx_request_tick,Appli_state;
static uint8_t log_host_session_active,log_host_rx_armed,log_host_tx_pending;
volatile uint8_t log_host_ack_received;
static log_link_parser_t log_host_parser;
static uint8_t usb_rx_formatted_ready,usb_rx_line_start;
static size_t usb_rx_timestamp_len;
static uint32_t mock_tick, irq_mask, ipsr, complete_on_lock;
static uint32_t irq_on[3], reset_held, clock_on, resets, deinit_calls, init_calls, starts, vbus_calls, wakeups;
static uint32_t allocations,frees,init_result,start_result,soft_result,vbus_result;
static uint32_t first_failure_sentinel;
static uint32_t HAL_GetTick(void) {return mock_tick;}
static uint32_t __get_IPSR(void) {return ipsr;}
static uint32_t __get_PRIMASK(void) {return irq_mask;}
static void __disable_irq(void) {if(complete_on_lock){usb_rx_pending=0;complete_on_lock=0;} irq_mask=1;}
static void __set_PRIMASK(uint32_t v) {irq_mask=v;}
static void __DMB(void) {}
static void __DSB(void) {}
static void __ISB(void) {}
static void osDelay(uint32_t ms) {assert(!irq_mask);mock_tick+=ms;}
static void HAL_NVIC_DisableIRQ(uint32_t n) {irq_on[n]=0;}
static void HAL_NVIC_ClearPendingIRQ(uint32_t n) {(void)n;assert(!irq_on[n]);}
static void enable_clock(void) {clock_on=1;}
static void force_reset(void) {
    assert(!irq_on[0]&&!irq_on[1]&&!irq_on[2]&&clock_on);
    reset_held=1;resets++;memset(hw,0,sizeof(hw));
}
static void release_reset(void) {assert(clock_on);reset_held=0;}
#define __HAL_RCC_USB_OTG_HS_CLK_ENABLE() enable_clock()
#define __HAL_RCC_USB_OTG_HS_FORCE_RESET() force_reset()
#define __HAL_RCC_USB_OTG_HS_RELEASE_RESET() release_reset()
static HAL_StatusTypeDef HAL_HCD_DeInit(HCD_HandleTypeDef *h) {assert(reset_held);(void)h;deinit_calls++;clock_on=0;return HAL_OK;}
static HAL_StatusTypeDef HAL_HCD_Init(HCD_HandleTypeDef *h) {
    assert(!reset_held&&clock_on&&h->Init.Host_channels==12&&h->pData==&hUsbHostHS);
    init_calls++;irq_on[0]=irq_on[1]=irq_on[2]=1;return init_result;
}
static HAL_StatusTypeDef HAL_HCD_Start(HCD_HandleTypeDef *h) {(void)h;starts++;return start_result;}
static uint32_t HAL_HCD_GetCurrentFrame(HCD_HandleTypeDef *h) {(void)h;return 0;}
static void USBH_LL_SetTimer(USBH_HandleTypeDef *p,uint32_t t) {p->Timer=t;}
static USBH_StatusTypeDef USBH_LL_DriverVBUS(USBH_HandleTypeDef *p,uint8_t state) {
    (void)p;assert(state==1);vbus_calls++;mock_tick+=200;return vbus_result;
}
static void USBH_OS_PutMessage(USBH_HandleTypeDef *p,uint32_t e,uint32_t t,uint32_t priority) {
    (void)e;(void)t;(void)priority;assert(p->os_event==(void*)0x2222&&!irq_mask);wakeups++;
}
static USBH_StatusTypeDef mock_class_deinit(USBH_HandleTypeDef *p) {
    assert(reset_held&&!irq_on[0]&&!irq_mask);
    if(soft_result!=USBH_OK)return soft_result;
    assert(p->pActiveClass->pData);free(p->pActiveClass->pData);p->pActiveClass->pData=NULL;frees++;return USBH_OK;
}
static void attach_class(void) {
    assert(!cdc_class.pData);cdc_class.pData=malloc(sizeof(CDC_HandleTypeDef));assert(cdc_class.pData);
    allocations++;((CDC_HandleTypeDef*)cdc_class.pData)->DataItf.InPipe=4;
    cdc_class.DeInit=mock_class_deinit;hUsbHostHS.pActiveClass=&cdc_class;
}
static void setup(void) {
    if(cdc_class.pData)free(cdc_class.pData);
    memset(&cdc_class,0,sizeof(cdc_class));memset(&hUsbHostHS,0,sizeof(hUsbHostHS));
    memset(&hhcd_USB_OTG_HS,0,sizeof(hhcd_USB_OTG_HS));memset(hw,0,sizeof(hw));
    memset((void*)&usb_recovery,0,sizeof(usb_recovery));
    hhcd_USB_OTG_HS.Instance=USB_OTG_HS;hhcd_USB_OTG_HS.Init.Host_channels=12;
    hhcd_USB_OTG_HS.pData=&hUsbHostHS;hUsbHostHS.pData=&hhcd_USB_OTG_HS;
    hUsbHostHS.thread=(void*)0x1111;hUsbHostHS.os_event=(void*)0x2222;
    hUsbHostHS.ClassNumber=1;hUsbHostHS.pClass[0]=&cdc_class;hUsbHostHS.gState=11;
    allocations=frees=resets=deinit_calls=init_calls=starts=vbus_calls=wakeups=0;
    init_result=start_result=soft_result=vbus_result=0;
    irq_mask=ipsr=complete_on_lock=0;irq_on[0]=irq_on[1]=irq_on[2]=1;
    clock_on=1;reset_held=0;mock_tick=0;first_failure_sentinel=0xAABBCCDD;
    usb_rx_pending=1;usb_rx_ready=usb_rx_waiting_for_ring=0;usb_rx_len=0;
    usb_rx_callback_count=7;usb_rx_request_tick=0;Appli_state=APPLICATION_READY;
    log_host_ack_received=1;
    log_host_session_active=log_host_rx_armed=log_host_tx_pending=1;
    log_host_parser.used=5;usb_rx_formatted_ready=1;usb_rx_timestamp_len=99;
    attach_class();
}
static USB_OTG_HostChannelTypeDef *regs(void) {return(void*)((uint8_t*)hw+USB_OTG_HOST_CHANNEL_BASE+4*USB_OTG_HOST_CHANNEL_SIZE);}
static USBH_StatusTypeDef DeInitStateMachine(USBH_HandleTypeDef *p);
static USBH_StatusTypeDef DeInitStateMachine(USBH_HandleTypeDef *phost)
{
  uint32_t i;

  /* Clear Pipes flags*/
  for (i = 0U; i < USBH_MAX_PIPES_NBR; i++)
  {
    phost->Pipes[i] = 0U;
  }

  for (i = 0U; i < USBH_MAX_DATA_BUFFER; i++)
  {
    phost->device.Data[i] = 0U;
  }

  phost->gState = HOST_IDLE;
  phost->EnumState = ENUM_IDLE;
  phost->RequestState = CMD_SEND;
  phost->Timer = 0U;

  phost->Control.state = CTRL_SETUP;
  phost->Control.pipe_size = USBH_MPS_DEFAULT;
  phost->Control.errorcount = 0U;

  phost->device.address = USBH_ADDRESS_DEFAULT;
  phost->device.speed = (uint8_t)USBH_SPEED_FULL;
  phost->device.RstCnt = 0U;
  phost->device.EnumCnt = 0U;

  /* Reset the device struct */
  USBH_memset(&phost->device.CfgDesc_Raw, 0, sizeof(phost->device.CfgDesc_Raw));
  USBH_memset(&phost->device.Data, 0, sizeof(phost->device.Data));
  USBH_memset(&phost->device.DevDesc, 0, sizeof(phost->device.DevDesc));
  USBH_memset(&phost->device.CfgDesc, 0, sizeof(phost->device.CfgDesc));

  return USBH_OK;
}
USBH_StatusTypeDef USBH_ResetStateForRecovery(USBH_HandleTypeDef *phost)
{
  USBH_StatusTypeDef status;
  if (phost == NULL) return USBH_FAIL;
  /* 调用者已停止应用提交并复位硬件，旧 DMA 不会再访问缓冲区。
   * 不调用 USBH_DeInit/Init，避免销毁自身线程或重复创建队列。 */
  if (phost->pActiveClass != NULL && phost->pActiveClass->pData != NULL)
  {
    if (phost->pActiveClass->DeInit == NULL) return USBH_FAIL;
    status = phost->pActiveClass->DeInit(phost);
    if (status != USBH_OK) return status;
  }
  phost->pActiveClass = NULL;
  USBH_memset(&phost->device, 0, sizeof(phost->device));
  USBH_memset(&phost->Control, 0, sizeof(phost->Control));
  return DeInitStateMachine(phost);
}
static void usb_recovery_reset_session(void)
{
    log_host_session_active = 0U;
    log_host_rx_armed = 0U;
    log_host_tx_pending = 0U;
    log_host_ack_received = 0U;
    log_host_parser.used = 0U;
    usb_rx_pending = 0U;
    usb_rx_ready = 0U;
    usb_rx_len = 0U;
    usb_rx_formatted_ready = 0U;
    usb_rx_timestamp_len = 0U;
    usb_rx_line_start = 1U;
    usb_rx_waiting_for_ring = 0U;
    Appli_state = APPLICATION_START;
}
uint8_t USBH_UserRecoveryService(USBH_HandleTypeDef *phost)
{
    HCD_InitTypeDef saved_init;
    HAL_StatusTypeDef hal_status;
    USBH_StatusTypeDef status;
    uint32_t failure = 0U;

    if (phost != &hUsbHostHS || __get_IPSR() != 0U) return 0U;
    if (usb_recovery.state == USB_HOST_RECOVERY_FAILED) return 1U;
    if (usb_recovery.state != USB_HOST_RECOVERY_REQUESTED) return 0U;
    usb_recovery.state = USB_HOST_RECOVERY_RUNNING;
    usb_recovery.start_tick = HAL_GetTick();
    usb_recovery.attempted = 1U;
    usb_recovery.callback_baseline = usb_rx_callback_count;
    saved_init = hhcd_USB_OTG_HS.Init;

    /* 先阻断 USB 中断并通过 RCC 复位终止旧 DMA，再释放旧 CDC 类。
     * 其他中断和任务仍运行，不在全局关中断期间延时或操作堆。 */
    HAL_NVIC_DisableIRQ(OTG_HS_IRQn);
    HAL_NVIC_DisableIRQ(OTG_HS_EP1_IN_IRQn);
    HAL_NVIC_DisableIRQ(OTG_HS_EP1_OUT_IRQn);
    __HAL_RCC_USB_OTG_HS_CLK_ENABLE();
    __HAL_RCC_USB_OTG_HS_FORCE_RESET();
    __DSB();
    status = USBH_ResetStateForRecovery(phost);
    if (status != USBH_OK) failure = 1U;
    /* 当前 ClosePipe 是无硬件操作；类 DeInit 释放 CDC 的单次堆分配。
     * 不调用 USBH_DeInit/Init，不销毁当前线程，也不增加消息队列。 */
    hal_status = HAL_HCD_DeInit(&hhcd_USB_OTG_HS);
    if (hal_status != HAL_OK && failure == 0U) failure = 1U;
    memset(&hhcd_USB_OTG_HS, 0, sizeof(hhcd_USB_OTG_HS));
    hhcd_USB_OTG_HS.Instance = USB_OTG_HS;
    hhcd_USB_OTG_HS.Init = saved_init;
    hhcd_USB_OTG_HS.pData = phost;
    phost->pData = &hhcd_USB_OTG_HS;
    usb_recovery_reset_session();
    __HAL_RCC_USB_OTG_HS_CLK_ENABLE();
    __HAL_RCC_USB_OTG_HS_RELEASE_RESET();
    __DSB();
    __ISB();
    HAL_NVIC_ClearPendingIRQ(OTG_HS_IRQn);
    HAL_NVIC_ClearPendingIRQ(OTG_HS_EP1_IN_IRQn);
    HAL_NVIC_ClearPendingIRQ(OTG_HS_EP1_OUT_IRQn);
    osDelay(20U);
    if (failure != 0U) goto recovery_failed;

    /* 单独检查 HAL 返回值，避免进入原 LL_Init 的 Error_Handler。 */
    hal_status = HAL_HCD_Init(&hhcd_USB_OTG_HS);
    if (hal_status != HAL_OK) {
        goto recovery_failed;
    }
    USBH_LL_SetTimer(phost, HAL_HCD_GetCurrentFrame(&hhcd_USB_OTG_HS));
    hal_status = HAL_HCD_Start(&hhcd_USB_OTG_HS);
    if (hal_status != HAL_OK) {
        goto recovery_failed;
    }
    status = USBH_LL_DriverVBUS(phost, 1U);
    if (status != USBH_OK) {
        goto recovery_failed;
    }
    usb_recovery.restart_tick = HAL_GetTick();

    __DMB();
    usb_recovery.state = USB_HOST_RECOVERY_WAIT_LINK;
    USBH_OS_PutMessage(phost, USBH_PORT_EVENT, 0U, 0U);
    return 1U;

recovery_failed:
    HAL_NVIC_DisableIRQ(OTG_HS_IRQn);
    HAL_NVIC_DisableIRQ(OTG_HS_EP1_IN_IRQn);
    HAL_NVIC_DisableIRQ(OTG_HS_EP1_OUT_IRQn);

    __DMB();
    usb_recovery.state = USB_HOST_RECOVERY_FAILED;
    return 1U;
}
static void usb_recovery_request(void)
{
    __DMB();
    usb_recovery.state = USB_HOST_RECOVERY_REQUESTED;
    /* 队列满时已有消息可唤醒 Host；任一消息都会先检查恢复请求。 */
    USBH_OS_PutMessage(&hUsbHostHS, USBH_PORT_EVENT, 0U, 0U);
}
static uint8_t usb_recovery_poll(void)
{
    CDC_HandleTypeDef *cdc;
    const USB_OTG_HostChannelTypeDef *regs;
    uint32_t now = HAL_GetTick(), primask, pipe, hcchar, urb_state;
    uint32_t state = usb_recovery.state;

    if (state == USB_HOST_RECOVERY_REQUESTED || state == USB_HOST_RECOVERY_RUNNING) return 1U;
    if (state == USB_HOST_RECOVERY_FAILED) {
        if (now - usb_recovery.start_tick >= USB_HOST_RECOVERY_COOLDOWN_MS)
            usb_recovery_request();
        return 1U;
    }
    if (hhcd_USB_OTG_HS.Instance == NULL) return 0U;
    if (state == USB_HOST_RECOVERY_WAIT_LINK) {
        if (Appli_state == APPLICATION_READY && log_host_ack_received &&
            usb_rx_callback_count != usb_recovery.callback_baseline) {
            usb_recovery.state = USB_HOST_RECOVERY_IDLE;
        } else if (now - usb_recovery.restart_tick >= USB_HOST_RECOVERY_ENUM_TIMEOUT_MS &&
                   (*(volatile uint32_t *)((uintptr_t)hhcd_USB_OTG_HS.Instance + USB_OTG_HOST_PORT_BASE) &
                    USB_OTG_HPRT_PCSTS) != 0U) {
            usb_recovery_request();
            return 1U;
        }
    }
    if (usb_recovery.attempted &&
        now - usb_recovery.start_tick < USB_HOST_RECOVERY_COOLDOWN_MS) return 0U;
    if (Appli_state != APPLICATION_READY || !usb_rx_pending || usb_rx_ready ||
        usb_rx_waiting_for_ring || now - usb_rx_request_tick < USB_HOST_RECOVERY_TIMEOUT_MS) return 0U;

    primask = __get_PRIMASK();
    __disable_irq();
    /* 临界区仅取指针和状态，不调用 RTOS 或阻塞函数。 */
    if (!usb_rx_pending || hUsbHostHS.pActiveClass == NULL || hUsbHostHS.pActiveClass->pData == NULL) {
        __set_PRIMASK(primask);
        return 0U;
    }
    cdc = (CDC_HandleTypeDef *)hUsbHostHS.pActiveClass->pData;
    pipe = cdc->DataItf.InPipe;
    if (pipe >= hhcd_USB_OTG_HS.Init.Host_channels ||
        pipe >= sizeof(hhcd_USB_OTG_HS.hc) / sizeof(hhcd_USB_OTG_HS.hc[0])) {
        __set_PRIMASK(primask);
        return 0U;
    }
    regs = (const USB_OTG_HostChannelTypeDef *)((uintptr_t)hhcd_USB_OTG_HS.Instance +
        USB_OTG_HOST_CHANNEL_BASE + pipe * USB_OTG_HOST_CHANNEL_SIZE);
    hcchar = regs->HCCHAR;
    urb_state = (uint32_t)hhcd_USB_OTG_HS.hc[pipe].urb_state;
    __set_PRIMASK(primask);
    if ((hcchar & (USB_OTG_HCCHAR_CHENA | USB_OTG_HCCHAR_CHDIS)) ==
        (USB_OTG_HCCHAR_CHENA | USB_OTG_HCCHAR_CHDIS)) {
        usb_recovery_request();
        return 1U;
    }
    if (urb_state == (uint32_t)URB_ERROR || urb_state == (uint32_t)URB_STALL) {
        usb_recovery_request();
        return 1U;
    }
    return 0U;
}
int recovery_regression_main(void) {
    uint32_t n;
    setup();regs()->HCCHAR=0x80588840;mock_tick=6000;
    assert(!usb_recovery_poll()&&usb_recovery.state==USB_HOST_RECOVERY_IDLE);
    regs()->HCCHAR=0xC0588840;mock_tick=1999;
    assert(!usb_recovery_poll());mock_tick=2000;
    assert(usb_recovery_poll());
    assert(usb_recovery.state==USB_HOST_RECOVERY_REQUESTED&&resets==0);
    assert(usb_recovery_poll()&&wakeups==1);
    ipsr=93;assert(!USBH_UserRecoveryService(&hUsbHostHS)&&!resets);ipsr=0;
    usb_rx_ready=1;usb_rx_len=64; /* 请求发出后可能赶上最后一个完成回调。 */
    assert(USBH_UserRecoveryService(&hUsbHostHS));
    assert(usb_recovery.state==USB_HOST_RECOVERY_WAIT_LINK);
    assert(resets==1&&init_calls==1&&starts==1&&vbus_calls==1&&allocations==frees);
    assert(hUsbHostHS.thread==(void*)0x1111&&hUsbHostHS.os_event==(void*)0x2222);
    assert(hUsbHostHS.ClassNumber==1&&hUsbHostHS.pClass[0]==&cdc_class&&!hUsbHostHS.pActiveClass);
    assert(hUsbHostHS.gState==HOST_IDLE&&!hUsbHostHS.device.is_connected);
    assert(!usb_rx_pending&&!usb_rx_ready&&!log_host_ack_received&&!log_host_session_active);
    assert(!usb_rx_ready && !usb_rx_formatted_ready && !usb_rx_timestamp_len);
    assert(usb_recovery.state==USB_HOST_RECOVERY_WAIT_LINK&&first_failure_sentinel==0xAABBCCDD);
    Appli_state=APPLICATION_READY;log_host_ack_received=1;
    assert(!usb_recovery_poll()&&usb_recovery.state==USB_HOST_RECOVERY_WAIT_LINK);
    usb_rx_callback_count++;assert(!usb_recovery_poll()&&usb_recovery.state==USB_HOST_RECOVERY_IDLE);
    puts("PASS: normal idle ignored, exact trigger, Host-thread recovery, DMA reset before free, resource identity retained, handshake confirms restoration");

    setup();regs()->HCCHAR=0xC0588840;mock_tick=2000;complete_on_lock=1;
    assert(!usb_recovery_poll()&&usb_recovery.state==USB_HOST_RECOVERY_IDLE);
    setup();regs()->HCCHAR=0xC0588840;mock_tick=3000;usb_rx_waiting_for_ring=1;
    assert(!usb_recovery_poll());usb_rx_waiting_for_ring=0;usb_rx_ready=1;
    assert(!usb_recovery_poll());
    puts("PASS: completion race, buffered data and ring backpressure cannot trigger recovery");

    setup();regs()->HCCHAR=0x80588840;hhcd_USB_OTG_HS.hc[4].urb_state=URB_ERROR;mock_tick=2000;
    assert(usb_recovery_poll());
    init_result=HAL_TIMEOUT;assert(USBH_UserRecoveryService(&hUsbHostHS));
    assert(usb_recovery.state==USB_HOST_RECOVERY_FAILED);
    assert(usb_recovery.state==USB_HOST_RECOVERY_FAILED&&!irq_on[0]&&!irq_on[1]&&!irq_on[2]);
    assert(!starts&&allocations==frees&&!reset_held);
    mock_tick=usb_recovery.start_tick+4999;assert(usb_recovery_poll());
    assert(wakeups==1);
    mock_tick++;assert(usb_recovery_poll());
    init_result=HAL_OK;assert(USBH_UserRecoveryService(&hUsbHostHS));
    assert(usb_recovery.state==USB_HOST_RECOVERY_WAIT_LINK&&starts==1);
    puts("PASS: init failure safely gates IRQs and retries after cooldown without leaking class data");

    setup();regs()->HCCHAR=0xC0588840;mock_tick=2000;
    assert(usb_recovery_poll());soft_result=USBH_FAIL;assert(USBH_UserRecoveryService(&hUsbHostHS));
    assert(usb_recovery.state==USB_HOST_RECOVERY_FAILED&&!reset_held&&cdc_class.pData);
    mock_tick+=5000;soft_result=USBH_OK;assert(usb_recovery_poll());
    assert(USBH_UserRecoveryService(&hUsbHostHS)&&allocations==frees);
    puts("PASS: class cleanup failure retains ownership for a later successful retry");

    setup();regs()->HCCHAR=0xC0588840;mock_tick=2000;assert(usb_recovery_poll());
    assert(USBH_UserRecoveryService(&hUsbHostHS));
    mock_tick=usb_recovery.restart_tick+10000;
    assert(!usb_recovery_poll()); /* 没有设备时不持续重建。 */
    *(uint32_t*)((uint8_t*)hw+USB_OTG_HOST_PORT_BASE)=1;
    assert(usb_recovery_poll());
    puts("PASS: enumeration timeout retries only when a device is physically connected");

    setup();regs()->HCCHAR=0xC0588840;mock_tick=500;usb_rx_request_tick=UINT32_MAX-1499;
    assert(usb_recovery_poll());
    start_result=HAL_ERROR;assert(USBH_UserRecoveryService(&hUsbHostHS));
    assert(usb_recovery.state==USB_HOST_RECOVERY_FAILED&&!irq_on[0]&&!reset_held);
    setup();regs()->HCCHAR=0xC0588840;mock_tick=2000;assert(usb_recovery_poll());
    vbus_result=USBH_FAIL;assert(USBH_UserRecoveryService(&hUsbHostHS));
    assert(usb_recovery.state==USB_HOST_RECOVERY_FAILED&&!irq_on[0]&&!reset_held);
    puts("PASS: trigger tick wrap, HCD start and VBUS failure paths remain quiesced and keep the failure state");

    setup();
    for(n=0;n<100;n++) {
        mock_tick+=6000;usb_rx_request_tick=mock_tick-2000;
        usb_rx_pending=1;usb_rx_ready=usb_rx_waiting_for_ring=0;Appli_state=APPLICATION_READY;
        regs()->HCCHAR=0xC0588840;
        assert(usb_recovery_poll());assert(USBH_UserRecoveryService(&hUsbHostHS));
        assert(allocations==frees);
        assert(hUsbHostHS.thread==(void*)0x1111&&hUsbHostHS.os_event==(void*)0x2222);
        attach_class();Appli_state=APPLICATION_READY;log_host_ack_received=1;usb_rx_callback_count++;
        assert(!usb_recovery_poll());
    }
    assert(resets==100&&usb_recovery.state==USB_HOST_RECOVERY_IDLE);
    assert(first_failure_sentinel==0xAABBCCDD);
    free(cdc_class.pData);cdc_class.pData=NULL;frees++;assert(allocations==frees);
    puts("PASS: 100 recovery/enumeration cycles retain original queue/thread and balance every CDC allocation");
    return 0;
}

#define USE_RTC 1
#define CDC_IDLE 0U
#define time_t calendar_time_t
typedef struct {uint32_t month,day,hour,min,second;} calendar_time_t;
static uint32_t log_host_session,log_host_session_counter,log_host_last_ready_tick;
static uint8_t usb_buffer[512],usb_rx_timestamp_buffer[5120],log_host_filtered[528],log_host_ready_frame[16];
static uint8_t usb_rx_formatted_line_start;
static uint32_t fake_rx_length,receive_calls,transmit_calls,write_calls;
static uint32_t transmit_result,receive_result;
static uint8_t mock_ring_ready=1,write_ok=1,recorder_ready=1,sync_receive;
static size_t mock_ring_free=32768,last_write_len;
static char last_write[5120];
static jmp_buf iteration_exit;
void USBH_CDC_ReceiveCallback(USBH_HandleTypeDef *phost);
static uint32_t osKernelSysTick(void){return mock_tick;}
static void osDelayUntil(uint32_t *tick,uint32_t delay){(void)tick;(void)delay;longjmp(iteration_exit,1);}
static void Calendar_ReadTime(calendar_time_t *t){t->month=10;t->day=16;t->hour=12;t->min=34;t->second=56;}
static uint8_t RecorderDataGetBufferStatus(size_t *free_bytes){*free_bytes=mock_ring_free;return mock_ring_ready;}
static uint8_t RecorderLogIsReady(void){return recorder_ready;}
static uint8_t RecorderDataWrite(const char *p,size_t n){write_calls++;last_write_len=n;memcpy(last_write,p,n);last_write[n]=0;return write_ok;}
static uint16_t USBH_CDC_GetLastReceivedDataSize(USBH_HandleTypeDef *p){(void)p;return (uint16_t)fake_rx_length;}
static USBH_StatusTypeDef USBH_CDC_Receive(USBH_HandleTypeDef *p,uint8_t *b,uint32_t n){(void)b;(void)n;receive_calls++;if(sync_receive){fake_rx_length=0;USBH_CDC_ReceiveCallback(p);}return receive_result;}
static USBH_StatusTypeDef USBH_CDC_Transmit(USBH_HandleTypeDef *p,uint8_t *b,size_t n){(void)p;assert(n==16&&b[4]==LOG_LINK_READY);transmit_calls++;return transmit_result;}
static void log_host_on_frame(uint8_t type, uint32_t session)
{
    if (type == LOG_LINK_ACK && log_host_session_active &&
        session == log_host_session) {
        log_host_ack_received = 1U;
    }
}
void USBH_CDC_TransmitCallback(USBH_HandleTypeDef *phost)
{
    if (phost == &hUsbHostHS) {
        log_host_tx_pending = 0U;
    }
}
static uint8_t usb_format_packet_with_timestamp(uint16_t input_len, size_t *output_len,
                                                uint8_t *line_start)
{
    size_t output_index = 0U;
    uint16_t input_index;
    char timestamp[32];
    int timestamp_len;
    uint8_t at_line_start = usb_rx_line_start;

    for (input_index = 0U; input_index < input_len; input_index++)
    {
        uint8_t data = usb_buffer[input_index];

        if (at_line_start && (data != '\r') && (data != '\n'))
        {
#if USE_RTC
            time_t snapshot;
            /* 格式化只读取本次局部快照，避免全局日历被其他任务更新。 */
            (void)Calendar_ReadTime(&snapshot);
            timestamp_len = snprintf(timestamp, sizeof(timestamp), "%02u%02u-%02u:%02u:%02u ",
                                     (unsigned int)snapshot.month,
                                     (unsigned int)snapshot.day,
                                     (unsigned int)snapshot.hour,
                                     (unsigned int)snapshot.min,
                                     (unsigned int)snapshot.second);
#else
            timestamp_len = snprintf(timestamp, sizeof(timestamp), "%lu ",
                                     (unsigned long)HAL_GetTick());
#endif
            if ((timestamp_len <= 0) ||
                (output_index + (size_t)timestamp_len + 1U > sizeof(usb_rx_timestamp_buffer)))
            {
                return 0U;
            }

            memcpy(&usb_rx_timestamp_buffer[output_index], timestamp, (size_t)timestamp_len);
            output_index += (size_t)timestamp_len;
            at_line_start = 0U;
        }

        if (output_index >= sizeof(usb_rx_timestamp_buffer))
        {
            return 0U;
        }
        usb_rx_timestamp_buffer[output_index++] = data;

        if (data == '\n')
        {
            at_line_start = 1U;
        }
    }

    *output_len = output_index;
    *line_start = at_line_start;
    return 1U;
}
void USBH_CDC_ReceiveCallback(USBH_HandleTypeDef *phost)
{
    uint16_t rx_len;

    if (phost != &hUsbHostHS)
    {
        return;
    }

    usb_rx_callback_count++;
    rx_len = USBH_CDC_GetLastReceivedDataSize(phost);
    {
        size_t filtered;
        uint16_t input_len = rx_len > sizeof(usb_buffer) ? sizeof(usb_buffer) : rx_len;
        filtered = LogLinkFilter(&log_host_parser, usb_buffer, input_len,
            log_host_filtered, &log_host_ack_received, log_host_on_frame);
        /* 当前接收请求为 64 字节，暂存前缀最多追加 15 字节，512 字节足够。 */
        if (filtered <= sizeof(usb_buffer)) {
            memcpy(usb_buffer, log_host_filtered, filtered);
            rx_len = (uint16_t)filtered;
        } else rx_len = 0U;
    }
    if (rx_len > 0U)
    {
        usb_rx_len = rx_len;
        usb_rx_ready = 1U;
    }

    usb_rx_pending = 0U;
}
void usb_task(void const *argument)
{
    uint32_t thread_wake_time = osKernelSysTick();
    uint32_t rx_request_len;
    size_t ring_free;
    uint8_t ring_ready;
    CDC_HandleTypeDef *cdc_handle;
    USBH_StatusTypeDef receive_status;
    for (;;)
    {
        thread_wake_time = osKernelSysTick();
        if (usb_recovery_poll()) {
            osDelay(1U);
            continue;
        }
        if (Appli_state == APPLICATION_READY)
        {
            if (!log_host_session_active) {
                log_host_session_counter++;
                log_host_session = HAL_GetTick() ^ (log_host_session_counter * 2654435761UL);
                log_host_session_active = 1U;
                log_host_ack_received = 0U;
                log_host_rx_armed = 0U;
                log_host_parser.used = 0U;
                log_host_last_ready_tick = HAL_GetTick() - 200U;
            }
            ring_ready = RecorderDataGetBufferStatus(&ring_free);
            if (usb_rx_ready)
            {
                if (!usb_rx_formatted_ready)
                {
                    if (usb_format_packet_with_timestamp(usb_rx_len, &usb_rx_timestamp_len,
                                                          &usb_rx_formatted_line_start))
                    {
                        usb_rx_formatted_ready = 1U;
                    }
                }

                if (usb_rx_formatted_ready &&
                    (!ring_ready || (ring_free < usb_rx_timestamp_len)))
                {
                    usb_rx_waiting_for_ring = 1U;
                }
                else if (usb_rx_formatted_ready &&
                         RecorderDataWrite((const char *)usb_rx_timestamp_buffer, usb_rx_timestamp_len))
                {
                    usb_rx_ready = 0U;
                    usb_rx_formatted_ready = 0U;
                    usb_rx_line_start = usb_rx_formatted_line_start;
                    usb_rx_waiting_for_ring = 0U;
                }
                else if (usb_rx_formatted_ready)
                {
                    usb_rx_waiting_for_ring = 1U;
                }
            }

            cdc_handle = NULL;
            rx_request_len = 0U;
            if (hUsbHostHS.pActiveClass != NULL)
            {
                cdc_handle = (CDC_HandleTypeDef *)hUsbHostHS.pActiveClass->pData;
                if (cdc_handle != NULL)
                {
                    rx_request_len = cdc_handle->DataItf.InEpSize;
                }
            }

            if (!usb_rx_pending && !usb_rx_ready &&
                (rx_request_len > 0U) && (rx_request_len <= sizeof(usb_buffer)))
            {
                if (!ring_ready || (ring_free < rx_request_len))
                {
                    usb_rx_waiting_for_ring = 1U;
                }
                else
                {
                    usb_rx_waiting_for_ring = 0U;
                    usb_rx_request_tick = HAL_GetTick();

                    /* 提交前先标记，避免完成回调清零后又被覆盖。 */
                    usb_rx_pending = 1U;
                    receive_status = USBH_CDC_Receive(&hUsbHostHS, usb_buffer, rx_request_len);

                    if (receive_status == USBH_OK) log_host_rx_armed = 1U;
                    if (receive_status != USBH_OK)
                    {
                        /* 提交失败，没有等待中的接收请求。 */
                        usb_rx_pending = 0U;
                    }
                }
            }
            /* 接收请求已提交且日志文件可写，再发送 READY。
             * 提交前置 pending，防止快速完成回调清零后被覆盖。 */
            if (log_host_rx_armed && RecorderLogIsReady() && !log_host_ack_received &&
                !log_host_tx_pending && cdc_handle != NULL && cdc_handle->data_tx_state == CDC_IDLE &&
                (HAL_GetTick() - log_host_last_ready_tick >= 200U)) {
                USBH_StatusTypeDef tx_status;
                LogLinkBuild(log_host_ready_frame, LOG_LINK_READY, log_host_session);
                log_host_tx_pending = 1U;
                log_host_last_ready_tick = HAL_GetTick();
                tx_status = USBH_CDC_Transmit(&hUsbHostHS, log_host_ready_frame, sizeof(log_host_ready_frame));
                if (tx_status != USBH_OK) {
                    log_host_tx_pending = 0U;
                }
            }
        }
        else
        {
            log_host_session_active = 0U;
            log_host_rx_armed = 0U;
            log_host_tx_pending = 0U;
            log_host_ack_received = 0U;
            log_host_parser.used = 0U;
            usb_rx_pending = 0U;
            usb_rx_ready = 0U;
            usb_rx_formatted_ready = 0U;
            usb_rx_line_start = 1U;
            usb_rx_waiting_for_ring = 0U;
        }
        osDelayUntil(&thread_wake_time, 1);
    }
}
static void task_once(void){if(!setjmp(iteration_exit))usb_task(NULL);}
static void feed(const void *p,size_t n){memcpy(usb_buffer,p,n);fake_rx_length=(uint32_t)n;USBH_CDC_ReceiveCallback(&hUsbHostHS);}
static void ready_setup(void){setup();((CDC_HandleTypeDef*)cdc_class.pData)->DataItf.InEpSize=64;((CDC_HandleTypeDef*)cdc_class.pData)->data_tx_state=CDC_IDLE;usb_rx_pending=usb_rx_ready=usb_rx_formatted_ready=0;log_host_session_active=log_host_tx_pending=log_host_ack_received=log_host_rx_armed=0;usb_rx_line_start=1;usb_rx_waiting_for_ring=0;mock_ring_ready=write_ok=recorder_ready=1;mock_ring_free=32768;receive_calls=transmit_calls=write_calls=0;transmit_result=receive_result=0;sync_receive=0;mock_tick=500;}
int main(void){
 uint8_t ack[16];uint32_t before;
 assert(!recovery_regression_main());
 ready_setup();recorder_ready=0;task_once();assert(receive_calls==1&&!transmit_calls&&usb_rx_pending);recorder_ready=1;transmit_result=USBH_BUSY;task_once();assert(transmit_calls==1&&!log_host_tx_pending);mock_tick+=200;transmit_result=USBH_OK;task_once();assert(transmit_calls==2&&log_host_tx_pending);USBH_CDC_TransmitCallback(&hUsbHostHS);assert(!log_host_tx_pending);
 LogLinkBuild(ack,LOG_LINK_ACK,log_host_session+1);feed(ack,16);assert(!log_host_ack_received&&!usb_rx_ready);
 LogLinkBuild(ack,LOG_LINK_ACK,log_host_session);feed(ack,7);assert(!log_host_ack_received);feed(ack+7,9);assert(log_host_ack_received&&!usb_rx_ready);
 feed("hello",5);mock_ring_free=0;task_once();assert(usb_rx_ready&&usb_rx_formatted_ready&&usb_rx_waiting_for_ring&&!write_calls);mock_ring_free=32768;task_once();assert(write_calls==1&&!usb_rx_ready&&!strcmp(last_write,"1016-12:34:56 hello"));
 feed("\r\nworld\n",8);write_ok=0;task_once();assert(usb_rx_ready&&usb_rx_formatted_ready&&write_calls==2);write_ok=1;task_once();assert(!usb_rx_ready&&write_calls==3&&!strcmp(last_write,"\r\n1016-12:34:56 world\n"));
 Appli_state=APPLICATION_START;task_once();assert(!log_host_session_active&&!log_host_ack_received&&!usb_rx_pending&&!usb_rx_ready);
 puts("PASS: READY gated by recorder readiness, BUSY retry, fragmented/session-specific ACK, timestamps across packets, ring backpressure and write retry, disconnect reset");
 ready_setup();((CDC_HandleTypeDef*)cdc_class.pData)->DataItf.InEpSize=1024;task_once();assert(!receive_calls&&!usb_rx_pending);
 ready_setup();receive_result=USBH_FAIL;task_once();assert(receive_calls==1&&!usb_rx_pending&&!log_host_rx_armed&&!transmit_calls);
 ready_setup();sync_receive=1;task_once();assert(receive_calls==1&&!usb_rx_pending&&log_host_rx_armed);before=usb_rx_callback_count;USBH_CDC_ReceiveCallback(NULL);assert(usb_rx_callback_count==before);
 free(cdc_class.pData);cdc_class.pData=NULL;
 puts("PASS: invalid endpoint size, failed receive submission, synchronous completion and foreign callback preserve receive state");return 0;
}
