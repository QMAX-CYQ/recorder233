
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#define APP_TX_DATA_SIZE 2048
#define USBD_OK 0
#define USBD_BUSY 1
#define USBD_FAIL 2
#define USBD_STATE_CONFIGURED 3
#define UNUSED(x) ((void)(x))
typedef struct { uint32_t TxState; } USBD_CDC_HandleTypeDef;
typedef struct { void *pClassData; uint32_t dev_state; } Device;
static Device hUsbDeviceHS;
static USBD_CDC_HandleTypeDef hc;
static uint8_t UserTxBufferHS[APP_TX_DATA_SIZE];
static uint32_t irq_mask, mock_start_result;
static uint16_t active_len;
static uint32_t __get_PRIMASK(void) { return irq_mask; }
static void __disable_irq(void) { irq_mask=1; }
static void __set_PRIMASK(uint32_t value) { irq_mask=value; }
static void USBD_CDC_SetTxBuffer(Device *d, uint8_t *buf, uint16_t len) {
  (void)d; assert(buf == UserTxBufferHS); active_len=len;
}
static uint8_t USBD_CDC_TransmitPacket(Device *d) {
  assert(irq_mask); (void)d;
  if(mock_start_result != USBD_OK) return (uint8_t)mock_start_result;
  hc.TxState=1; return USBD_OK;
}
typedef struct {
    uint32_t attempts, accepted, busy, invalid, start_failed;
    uint32_t accepted_bytes, completed, completed_bytes;
    uint32_t init_count, deinit_count;
    uint32_t buffered, queue_high_water, started, started_bytes;
    uint32_t disconnect_lost, disconnect_lost_bytes;
} cdc_tx_diag_t;
volatile cdc_tx_diag_t g_cdc_tx_diag;
#define CDC_DIAG_ADD(field,value) ((void)(g_cdc_tx_diag.field+=(value)))
#define CDC_TX_QUEUE_CAPACITY 8U
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
    /* Keep the queue head intact for the next service attempt. */
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

/* USBD_OK now means copied into the FIFO, not necessarily sent yet.
 * USBD_BUSY means FIFO full; USBD_FAIL means invalid arguments. */
uint8_t CDC_Transmit_HS(uint8_t* Buf, uint16_t Len)
{
  uint32_t irq_state;
  USBD_CDC_HandleTypeDef *hcdc;
  CDC_DIAG_ADD(attempts, 1U);
  if ((Buf == NULL) || (Len == 0U) || (Len > APP_TX_DATA_SIZE)) {
    CDC_DIAG_ADD(invalid, 1U);
    return USBD_FAIL;
  }
  irq_state = __get_PRIMASK();
  __disable_irq();
  /* Try draining first, so an idle USB can make space in a full queue. */
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
  return USBD_OK;
}
static int8_t CDC_DeInit_HS(void)
{
  /* USER CODE BEGIN 9 */
  uint32_t irq_state = __get_PRIMASK();
  __disable_irq();
  CDC_DIAG_ADD(deinit_count, 1U);
  /* USB aborts an active transfer on disconnect. Preserve unsent FIFO entries;
   * do not replay a possibly partially delivered transfer. */
  if (cdc_tx_inflight != 0U) {
    CDC_DIAG_ADD(disconnect_lost, 1U);
    CDC_DIAG_ADD(disconnect_lost_bytes, cdc_tx_inflight_bytes);
  }
  cdc_tx_inflight = 0U;
  cdc_tx_inflight_bytes = 0U;
  __set_PRIMASK(irq_state);
  return (USBD_OK);
  /* USER CODE END 9 */
}static int8_t CDC_TransmitCplt_HS(uint8_t *Buf, uint32_t *Len, uint8_t epnum)
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
static void reset_test(void) {
  memset((void*)&g_cdc_tx_diag, 0, sizeof(g_cdc_tx_diag));
  memset(cdc_tx_queue, 0, sizeof(cdc_tx_queue));
  cdc_tx_head=cdc_tx_tail=cdc_tx_pending_count=cdc_tx_inflight=0;
  cdc_tx_inflight_bytes=0; irq_mask=mock_start_result=0; hc.TxState=0;
  hUsbDeviceHS.pClassData=&hc; hUsbDeviceHS.dev_state=USBD_STATE_CONFIGURED;
}
static void complete(void) {
  uint32_t len=active_len;
  hc.TxState=0;
  CDC_TransmitCplt_HS(UserTxBufferHS, &len, 1);
  assert(irq_mask==0);
}
int main(void) {
  uint8_t value; uint32_t i;
  reset_test();
  value=10; assert(CDC_Transmit_HS(&value,1)==USBD_OK);
  for(i=0;i<8;i++) {
    value=(uint8_t)(20+i); assert(CDC_Transmit_HS(&value,1)==USBD_OK);
  }
  value=99; assert(CDC_Transmit_HS(&value,1)==USBD_BUSY);
  assert(cdc_tx_pending_count==8 && g_cdc_tx_diag.busy==1);
  assert(UserTxBufferHS[0]==10); /* pending copies never overwrite active USB */
  for(i=0;i<8;i++) { complete(); assert(UserTxBufferHS[0]==20+i); }
  complete(); assert(cdc_tx_pending_count==0 && cdc_tx_inflight==0);
  assert(g_cdc_tx_diag.accepted==9 && g_cdc_tx_diag.completed==9);
  puts("PASS: BUSY buffering, payload copies, FIFO, overflow, completion drain");

  reset_test(); mock_start_result=USBD_BUSY;
  value=42; assert(CDC_Transmit_HS(&value,1)==USBD_OK); value=0;
  assert(cdc_tx_pending_count==1 && cdc_tx_inflight==0);
  mock_start_result=USBD_OK; CDC_TxService_HS();
  assert(cdc_tx_pending_count==0 && UserTxBufferHS[0]==42); complete();
  puts("PASS: start failure preserves queue head and service retries");

  reset_test(); hUsbDeviceHS.pClassData=NULL; hUsbDeviceHS.dev_state=0;
  value=55; assert(CDC_Transmit_HS(&value,1)==USBD_OK);
  CDC_TxService_HS(); assert(cdc_tx_pending_count==1);
  hUsbDeviceHS.pClassData=&hc; hUsbDeviceHS.dev_state=USBD_STATE_CONFIGURED;
  CDC_TxService_HS(); assert(UserTxBufferHS[0]==55);
  value=56; assert(CDC_Transmit_HS(&value,1)==USBD_OK);
  CDC_DeInit_HS(); assert(g_cdc_tx_diag.disconnect_lost==1);
  assert(cdc_tx_pending_count==1 && cdc_tx_inflight==0);
  hc.TxState=0; CDC_TxService_HS(); assert(UserTxBufferHS[0]==56); complete();
  puts("PASS: startup buffering, disconnect accounting, pending FIFO retention");

  reset_test(); irq_mask=1; value=1;
  assert(CDC_Transmit_HS(&value,1)==USBD_OK && irq_mask==1);
  assert(CDC_Transmit_HS(NULL,1)==USBD_FAIL);
  assert(CDC_Transmit_HS(&value,0)==USBD_FAIL);
  assert(CDC_Transmit_HS(&value,2049)==USBD_FAIL);
  puts("PASS: IRQ state restoration and invalid input rejection");
  return 0;
}
