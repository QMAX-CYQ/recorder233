/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : usbd_cdc_if.c
  * @version        : v1.0_Cube
  * @brief          : Usb device for Virtual Com Port.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2024 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "usbd_cdc_if.h"

/* USER CODE BEGIN INCLUDE */
#include "log_link_protocol.h"
#include "string.h"
/* USER CODE END INCLUDE */

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
/* Private macro -------------------------------------------------------------*/

/* USER CODE BEGIN PV */
volatile uint8_t g_log_link_ready;
volatile log_link_diag_t g_log_link_diag;
static volatile uint8_t log_link_ack_needed;
static volatile uint8_t log_link_session_accepted;
static log_link_parser_t log_link_parser;
static uint8_t log_link_filtered[APP_RX_DATA_SIZE+16U];
/* Private variables ---------------------------------------------------------*/
volatile cdc_tx_diag_t g_cdc_tx_diag;
/* 多任务调用时，诊断计数使用原子操作更新。 */
#define CDC_DIAG_ADD(field, value) \
  ((void)__atomic_fetch_add(&g_cdc_tx_diag.field, (uint32_t)(value), __ATOMIC_RELAXED))

/* 队列与 USB 在途发送缓冲分离，在传输完成前不覆盖发送缓冲。
 * 使用短 PRIMASK 临界区保护任务和回调之间的共享状态。 */
#define CDC_TX_QUEUE_CAPACITY 32U
typedef struct {
  uint16_t len;
  uint8_t data[APP_TX_DATA_SIZE];
} cdc_tx_slot_t;
static cdc_tx_slot_t cdc_tx_queue[CDC_TX_QUEUE_CAPACITY];
static uint32_t cdc_tx_head;
static uint32_t cdc_tx_tail;
static uint16_t cdc_tx_inflight_bytes;
volatile uint32_t cdc_tx_queued_count;
volatile uint32_t cdc_tx_started_count;
volatile uint32_t cdc_tx_complete_count;
volatile uint32_t cdc_tx_pending_count;
volatile uint32_t cdc_tx_inflight;
volatile uint32_t cdc_tx_queue_overflow_count;
volatile uint32_t cdc_tx_start_error_count;

/* USER CODE END PV */

/** @addtogroup STM32_USB_OTG_DEVICE_LIBRARY
  * @brief Usb device library.
  * @{
  */

/** @addtogroup USBD_CDC_IF
  * @{
  */

/** @defgroup USBD_CDC_IF_Private_TypesDefinitions USBD_CDC_IF_Private_TypesDefinitions
  * @brief Private types.
  * @{
  */

/* USER CODE BEGIN PRIVATE_TYPES */

/* USER CODE END PRIVATE_TYPES */

/**
  * @}
  */

/** @defgroup USBD_CDC_IF_Private_Defines USBD_CDC_IF_Private_Defines
  * @brief Private defines.
  * @{
  */

/* USER CODE BEGIN PRIVATE_DEFINES */
/* USER CODE END PRIVATE_DEFINES */

/**
  * @}
  */

/** @defgroup USBD_CDC_IF_Private_Macros USBD_CDC_IF_Private_Macros
  * @brief Private macros.
  * @{
  */

/* USER CODE BEGIN PRIVATE_MACRO */

/* USER CODE END PRIVATE_MACRO */

/**
  * @}
  */

/** @defgroup USBD_CDC_IF_Private_Variables USBD_CDC_IF_Private_Variables
  * @brief Private variables.
  * @{
  */

/* Create buffer for reception and transmission           */
/* It's up to user to redefine and/or remove those define */
/** Received data over USB are stored in this buffer      */
uint8_t UserRxBufferHS[APP_RX_DATA_SIZE];

/** Data to send over USB CDC are stored in this buffer   */
uint8_t UserTxBufferHS[APP_TX_DATA_SIZE];

/* USER CODE BEGIN PRIVATE_VARIABLES */

/* USER CODE END PRIVATE_VARIABLES */

/**
  * @}
  */

/** @defgroup USBD_CDC_IF_Exported_Variables USBD_CDC_IF_Exported_Variables
  * @brief Public variables.
  * @{
  */

extern USBD_HandleTypeDef hUsbDeviceHS;

/* USER CODE BEGIN EXPORTED_VARIABLES */

/* USER CODE END EXPORTED_VARIABLES */

/**
  * @}
  */

/** @defgroup USBD_CDC_IF_Private_FunctionPrototypes USBD_CDC_IF_Private_FunctionPrototypes
  * @brief Private functions declaration.
  * @{
  */

static int8_t CDC_Init_HS(void);
static int8_t CDC_DeInit_HS(void);
static int8_t CDC_Control_HS(uint8_t cmd, uint8_t* pbuf, uint16_t length);
static int8_t CDC_Receive_HS(uint8_t* pbuf, uint32_t *Len);
static int8_t CDC_TransmitCplt_HS(uint8_t *pbuf, uint32_t *Len, uint8_t epnum);

/* USER CODE BEGIN PRIVATE_FUNCTIONS_DECLARATION */
static void LogLinkOnFrame(uint8_t type, uint32_t session);
static void CDC_TxServiceLocked(void);
static void CDC_CleanDCache(uint8_t *buf, uint32_t len);

/* USER CODE END PRIVATE_FUNCTIONS_DECLARATION */

/**
  * @}
  */

USBD_CDC_ItfTypeDef USBD_Interface_fops_HS =
{
  CDC_Init_HS,
  CDC_DeInit_HS,
  CDC_Control_HS,
  CDC_Receive_HS,
  CDC_TransmitCplt_HS
};

/* Private functions ---------------------------------------------------------*/

/**
  * @brief  Initializes the CDC media low layer over the USB HS IP
  * @retval USBD_OK if all operations are OK else USBD_FAIL
  */
static int8_t CDC_Init_HS(void)
{
  /* USER CODE BEGIN 8 */
  g_log_link_ready = 0U;
  log_link_ack_needed = 0U;
  log_link_session_accepted = 0U;
  log_link_parser.used = 0U;
  CDC_DIAG_ADD(init_count, 1U);
  /* Set Application Buffers */
  USBD_CDC_SetTxBuffer(&hUsbDeviceHS, UserTxBufferHS, 0);
  USBD_CDC_SetRxBuffer(&hUsbDeviceHS, UserRxBufferHS);
  return (USBD_OK);
  /* USER CODE END 8 */
}

/**
  * @brief  DeInitializes the CDC media low layer
  * @param  None
  * @retval USBD_OK if all operations are OK else USBD_FAIL
  */
static int8_t CDC_DeInit_HS(void)
{
  /* USER CODE BEGIN 9 */
  uint32_t irq_state = __get_PRIMASK();
  __disable_irq();
  g_log_link_ready = 0U;
  log_link_ack_needed = 0U;
  log_link_session_accepted = 0U;
  log_link_parser.used = 0U;
  CDC_DIAG_ADD(deinit_count, 1U);
  /* 断开时保留尚未启动的队列数据；在途传输可能已部分送达，
   * 只记录未完成数量，不自动重发，以免产生重复数据。 */
  if (cdc_tx_inflight != 0U) {
    CDC_DIAG_ADD(disconnect_lost, 1U);
    CDC_DIAG_ADD(disconnect_lost_bytes, cdc_tx_inflight_bytes);
  }
  cdc_tx_inflight = 0U;
  cdc_tx_inflight_bytes = 0U;
  __set_PRIMASK(irq_state);
  return (USBD_OK);
  /* USER CODE END 9 */
}

/**
  * @brief  Manage the CDC class requests
  * @param  cmd: Command code
  * @param  pbuf: Buffer containing command data (request parameters)
  * @param  length: Number of data to be sent (in bytes)
  * @retval Result of the operation: USBD_OK if all operations are OK else USBD_FAIL
  */
static int8_t CDC_Control_HS(uint8_t cmd, uint8_t* pbuf, uint16_t length)
{
  /* USER CODE BEGIN 10 */
  switch(cmd)
  {
  case CDC_SEND_ENCAPSULATED_COMMAND:

    break;

  case CDC_GET_ENCAPSULATED_RESPONSE:

    break;

  case CDC_SET_COMM_FEATURE:

    break;

  case CDC_GET_COMM_FEATURE:

    break;

  case CDC_CLEAR_COMM_FEATURE:

    break;

  /*******************************************************************************/
  /* Line Coding Structure                                                       */
  /*-----------------------------------------------------------------------------*/
  /* Offset | Field       | Size | Value  | Description                          */
  /* 0      | dwDTERate   |   4  | Number |Data terminal rate, in bits per second*/
  /* 4      | bCharFormat |   1  | Number | Stop bits                            */
  /*                                        0 - 1 Stop bit                       */
  /*                                        1 - 1.5 Stop bits                    */
  /*                                        2 - 2 Stop bits                      */
  /* 5      | bParityType |  1   | Number | Parity                               */
  /*                                        0 - None                             */
  /*                                        1 - Odd                              */
  /*                                        2 - Even                             */
  /*                                        3 - Mark                             */
  /*                                        4 - Space                            */
  /* 6      | bDataBits  |   1   | Number Data bits (5, 6, 7, 8 or 16).          */
  /*******************************************************************************/
  case CDC_SET_LINE_CODING:

    break;

  case CDC_GET_LINE_CODING:

    break;

  case CDC_SET_CONTROL_LINE_STATE:

    break;

  case CDC_SEND_BREAK:

    break;

  default:
    break;
  }

  return (USBD_OK);
  /* USER CODE END 10 */
}

/**
  * @brief Data received over USB OUT endpoint are sent over CDC interface
  *         through this function.
  *
  *         @note
  *         This function will issue a NAK packet on any OUT packet received on
  *         USB endpoint until exiting this function. If you exit this function
  *         before transfer is complete on CDC interface (ie. using DMA controller)
  *         it will result in receiving more data while previous ones are still
  *         not sent.
  *
  * @param  Buf: Buffer of data to be received
  * @param  Len: Number of data received (in bytes)
  * @retval Result of the operation: USBD_OK if all operations are OK else USBD_FAILL
  */
static int8_t CDC_Receive_HS(uint8_t* Buf, uint32_t *Len)
{
  /* USER CODE BEGIN 11 */
  size_t payload_len;
  payload_len = LogLinkFilter(&log_link_parser, Buf,
      (*Len > APP_RX_DATA_SIZE) ? APP_RX_DATA_SIZE : *Len,
      log_link_filtered, NULL, LogLinkOnFrame);
  /* READY 不交给视觉解析器，其他数据保持原顺序。 */
  if (payload_len != 0U) LogLinkPayloadReceived(log_link_filtered, (uint16_t)payload_len);
  USBD_CDC_SetRxBuffer(&hUsbDeviceHS, &Buf[0]);
  USBD_CDC_ReceivePacket(&hUsbDeviceHS);
  return (USBD_OK);
  /* USER CODE END 11 */
}

/**
  * @brief  Data to send over USB IN endpoint are sent over CDC interface
  *         through this function.
  * @param  Buf: Buffer of data to be sent
  * @param  Len: Number of data to be sent (in bytes)
  * @retval Result of the operation: USBD_OK if all operations are OK else USBD_FAIL or USBD_BUSY
  */
uint8_t CDC_Transmit_HS(uint8_t* Buf, uint16_t Len)
{
  uint8_t result = USBD_OK;
  /* USER CODE BEGIN 12 */
  /* 返回 OK 表示已复制到队列，不代表已经发送完成。
   * 队列满返回 BUSY，参数错误返回 FAIL。 */
  uint32_t irq_state;
  USBD_CDC_HandleTypeDef *hcdc;
  CDC_DIAG_ADD(attempts, 1U);
  if ((Buf == NULL) || (Len == 0U) || (Len > APP_TX_DATA_SIZE)) {
    CDC_DIAG_ADD(invalid, 1U);
    return USBD_FAIL;
  }
  irq_state = __get_PRIMASK();
  __disable_irq();
  /* 先尝试启动队首，使空闲 USB 能为满队列腾出位置。 */
  CDC_TxServiceLocked();
  if (cdc_tx_pending_count >= CDC_TX_QUEUE_CAPACITY) {
    cdc_tx_queue_overflow_count++;
    CDC_DIAG_ADD(busy, 1U);
    __set_PRIMASK(irq_state);
    return USBD_BUSY;
  }
  hcdc = (USBD_CDC_HandleTypeDef*)hUsbDeviceHS.pClassData;
  if ((hcdc == NULL) || (hcdc->TxState != 0U) || (cdc_tx_pending_count != 0U) ||
      (hUsbDeviceHS.dev_state != USBD_STATE_CONFIGURED))
    CDC_DIAG_ADD(buffered, 1U);
  memcpy(cdc_tx_queue[cdc_tx_tail].data, Buf, Len);
  cdc_tx_queue[cdc_tx_tail].len = Len;
  cdc_tx_tail = (cdc_tx_tail + 1U) % CDC_TX_QUEUE_CAPACITY;
  cdc_tx_pending_count++;
  cdc_tx_queued_count++;
  if (cdc_tx_pending_count > g_cdc_tx_diag.queue_high_water)
    g_cdc_tx_diag.queue_high_water = cdc_tx_pending_count;
  CDC_DIAG_ADD(accepted, 1U);
  CDC_DIAG_ADD(accepted_bytes, Len);
  CDC_TxServiceLocked();
  __set_PRIMASK(irq_state);
  /* USER CODE END 12 */
  return result;
}

/**
  * @brief  CDC_TransmitCplt_HS
  *         Data transmitted callback
  *
  *         @note
  *         This function is IN transfer complete callback used to inform user that
  *         the submitted Data is successfully sent over USB.
  *
  * @param  Buf: Buffer of data to be received
  * @param  Len: Number of data received (in bytes)
  * @retval Result of the operation: USBD_OK if all operations are OK else USBD_FAIL
  */
static int8_t CDC_TransmitCplt_HS(uint8_t *Buf, uint32_t *Len, uint8_t epnum)
{
  uint8_t result = USBD_OK;
  /* USER CODE BEGIN 14 */
  uint32_t irq_state = __get_PRIMASK();
  __disable_irq();
  CDC_DIAG_ADD(completed, 1U);
  if (Len != NULL) CDC_DIAG_ADD(completed_bytes, *Len);
  cdc_tx_complete_count++;
  cdc_tx_inflight = 0U;
  cdc_tx_inflight_bytes = 0U;
  CDC_TxServiceLocked();
  __set_PRIMASK(irq_state);
  UNUSED(Buf);
  UNUSED(Len);
  UNUSED(epnum);
  /* USER CODE END 14 */
  return result;
}

/* USER CODE BEGIN PRIVATE_FUNCTIONS_IMPLEMENTATION */
/* Override in a user file to consume non-control USB payload. */
__attribute__((weak)) void LogLinkPayloadReceived(const uint8_t *data, uint16_t length)
{
    (void)data;
    (void)length;
}


static void LogLinkOnFrame(uint8_t type, uint32_t session)
{
  if (type != LOG_LINK_READY) return;
  if (g_log_link_diag.session != session) {
    g_log_link_ready = 0U;
    log_link_session_accepted = 0U;
  }
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
    /* 挂起仅暂停发送，恢复后沿用已确认的会话；真正断开需重新握手。 */
    if (hUsbDeviceHS.dev_state != USBD_STATE_SUSPENDED) log_link_session_accepted = 0U;
  } else if (log_link_ack_needed) {
    LogLinkBuild(ack, LOG_LINK_ACK, g_log_link_diag.session);
    /* 同一 FIFO 中 ACK 排在新日志之前，接收端先确认握手再接收日志。 */
    result = CDC_Transmit_HS(ack, sizeof(ack));
    if (result == USBD_OK) {
      log_link_ack_needed = 0U;
      log_link_session_accepted = 1U;
      g_log_link_ready = 1U;
      g_log_link_diag.ack_queued++;
    } else g_log_link_diag.ack_failed++;
  } else if (log_link_session_accepted) {
    g_log_link_ready = 1U;
  }
  __set_PRIMASK(irq_state);
}

/* 仅在屏蔽中断的临界区内调用，也供 USB 完成回调使用。 */
static void CDC_TxServiceLocked(void)
{
  USBD_CDC_HandleTypeDef *hcdc = (USBD_CDC_HandleTypeDef*)hUsbDeviceHS.pClassData;
  uint8_t result;
  cdc_tx_slot_t *slot;
  if ((hcdc == NULL) || (hUsbDeviceHS.dev_state != USBD_STATE_CONFIGURED) ||
      (cdc_tx_pending_count == 0U) || (cdc_tx_inflight != 0U) || (hcdc->TxState != 0U))
    return;

  slot = &cdc_tx_queue[cdc_tx_head];
  memcpy(UserTxBufferHS, slot->data, slot->len);
  USBD_CDC_SetTxBuffer(&hUsbDeviceHS, UserTxBufferHS, slot->len);
  result = USBD_CDC_TransmitPacket(&hUsbDeviceHS);
  if (result == USBD_OK) {
    cdc_tx_inflight = 1U;
    cdc_tx_inflight_bytes = slot->len;
    cdc_tx_started_count++;
    CDC_DIAG_ADD(started, 1U);
    CDC_DIAG_ADD(started_bytes, slot->len);
    cdc_tx_head = (cdc_tx_head + 1U) % CDC_TX_QUEUE_CAPACITY;
    cdc_tx_pending_count--;
  } else {
    /* 启动失败时保留队首，下次继续尝试。 */
    cdc_tx_start_error_count++;
    CDC_DIAG_ADD(start_failed, 1U);
  }
}

void CDC_TxService_HS(void)
{
  uint32_t irq_state = __get_PRIMASK();
  __disable_irq();
  CDC_TxServiceLocked();
  __set_PRIMASK(irq_state);
}


static void CDC_CleanDCache(uint8_t *buf, uint32_t len)
{
  uint32_t start_addr = (uint32_t)buf & ~31U;
  uint32_t end_addr = ((uint32_t)buf + len + 31U) & ~31U;

  SCB_CleanDCache_by_Addr((uint32_t *)start_addr, (int32_t)(end_addr - start_addr));
}

/* USER CODE END PRIVATE_FUNCTIONS_IMPLEMENTATION */

/**
  * @}
  */

/**
  * @}
  */
