from pathlib import Path
import hashlib, json

source = Path('D:/RM/rm_main_gimbal')
stage = Path(__file__).resolve().parent/'tx_queue'
stage.mkdir(exist_ok=True)
changes = {}
def read(name):
    data = (source/name).read_bytes()
    changes[name] = hashlib.sha256(data).hexdigest()
    return data.decode('latin1').replace('\r\n', '\n')
def save(name, text):
    target = stage/name
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(text.replace('\n', '\r\n').encode('latin1'))

name = 'USB_DEVICE/App/usbd_cdc_if.c'
text = read(name)
text = text.replace('/* USER CODE END PV */', '''/* Queue storage is separate from UserTxBufferHS, which stays owned by USB
 * until completion. All access is serialized with a short PRIMASK section. */
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

/* USER CODE END PV */''', 1)
begin = text.index('uint8_t CDC_Transmit_HS(uint8_t* Buf, uint16_t Len)\n{')
end = text.index('\n/**', begin)
text = text[:begin]+'''/* Called only while interrupts are masked; also used from the USB callback. */
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
''' + text[end:]
text = text.replace('  CDC_DIAG_ADD(deinit_count, 1U);', '''  uint32_t irq_state = __get_PRIMASK();
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
  __set_PRIMASK(irq_state);''', 1)
text = text.replace('  CDC_DIAG_ADD(completed, 1U);\n  if (Len != NULL) CDC_DIAG_ADD(completed_bytes, *Len);', '''  uint32_t irq_state = __get_PRIMASK();
  __disable_irq();
  CDC_DIAG_ADD(completed, 1U);
  if (Len != NULL) CDC_DIAG_ADD(completed_bytes, *Len);
  cdc_tx_complete_count++;
  cdc_tx_inflight = 0U;
  cdc_tx_inflight_bytes = 0U;
  CDC_TxServiceLocked();
  __set_PRIMASK(irq_state);''', 1)
save(name, text)

name = 'USB_DEVICE/App/usbd_cdc_if.h'
text = read(name).replace('    uint32_t init_count, deinit_count;', '''    uint32_t init_count, deinit_count;
    uint32_t buffered, queue_high_water, started, started_bytes;
    uint32_t disconnect_lost, disconnect_lost_bytes;''')
text = text.replace('uint8_t CDC_Transmit_HS(uint8_t* Buf, uint16_t Len);', '''/* OK: copied to FIFO; BUSY: FIFO full; FAIL: invalid buffer/length.
 * FIFO accepts data before enumeration and retains pending entries on disconnect. */
uint8_t CDC_Transmit_HS(uint8_t* Buf, uint16_t Len);
void CDC_TxService_HS(void);''')
save(name, text)

name = 'User/Task/debug_task.c'
text = read(name).replace('        uint32_t tick = osKernelSysTick();', '''        uint32_t tick = osKernelSysTick();
        CDC_TxService_HS();''', 1)
save(name, text)
name = 'User/Support/log.c'
text = read(name).replace('/* Observe the original single send attempt; do not retry or change payload. */',
                         '/* CDC copies the payload into its FIFO; OK means queued successfully. */')
save(name, text)
(stage/'original_hashes.json').write_text(json.dumps(changes, indent=2), encoding='utf8')
print('Prepared four sender files in '+str(stage))
