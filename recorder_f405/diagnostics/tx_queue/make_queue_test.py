from pathlib import Path
root = Path(__file__).resolve().parent
text = (root/'USB_DEVICE/App/usbd_cdc_if.c').read_text()
header = (root/'USB_DEVICE/App/usbd_cdc_if.h').read_text()
diag = header[header.index('typedef struct {'):header.index('} cdc_tx_diag_t;')+len('} cdc_tx_diag_t;')]
storage = text[text.index('#define CDC_TX_QUEUE_CAPACITY'):text.index('/* USER CODE END PV */')]
service = text[text.index('static void CDC_TxServiceLocked'):text.index('\n/**', text.index('static void CDC_TxServiceLocked'))]
def function(name):
    start = text.index('static int8_t '+name+'(' , text.index('static int8_t '+name+'(')+1)
    opening = text.index('{', start)
    depth = 1; end = opening+1
    while depth:
        depth += (text[end] == '{')-(text[end] == '}'); end += 1
    return text[start:end]
prelude = r'''
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
'''
tests = r'''
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
'''
(root/'queue_test.c').write_text(prelude+diag+'\nvolatile cdc_tx_diag_t g_cdc_tx_diag;\n#define CDC_DIAG_ADD(field,value) ((void)(g_cdc_tx_diag.field+=(value)))\n'+storage+service+function('CDC_DeInit_HS')+function('CDC_TransmitCplt_HS')+tests)
