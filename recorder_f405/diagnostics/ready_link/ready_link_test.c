
#include <assert.h>
#include <stdio.h>
#include "receiver/User/Task/log_link_protocol.h"
#define USBD_OK 0
#define USBD_BUSY 1
#define USBD_STATE_CONFIGURED 3
#define USBD_STATE_SUSPENDED 4
static struct { uint32_t dev_state; } hUsbDeviceHS;
volatile uint8_t g_log_link_ready;
static struct { uint32_t ready_rx,ack_queued,ack_failed,session,waiting_loops; } g_log_link_diag;
static volatile uint8_t log_link_ack_needed;
static volatile uint8_t log_link_session_accepted;
static unsigned irq_state_mock,tx_result,callback_count;
static uint8_t captured[16];
static uint32_t __get_PRIMASK(void) { return irq_state_mock; }
static void __disable_irq(void) { irq_state_mock=1; }
static void __set_PRIMASK(uint32_t x) { irq_state_mock=x; }
static uint8_t CDC_Transmit_HS(uint8_t *data,uint16_t len) {
    assert(len==16 && irq_state_mock==1);memcpy(captured,data,16);return (uint8_t)tx_result;
}
static volatile uint8_t allow_payload;
static void host_on_frame(uint8_t type,uint32_t session) {
    callback_count++;
    if(type==LOG_LINK_ACK && session==12345) allow_payload=1;
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
}void LogLinkService_HS(void)
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
int main(void) {
    uint8_t frame[16],mixed[100],out[128],bad[16];log_link_parser_t p;
    size_t split,n,a,b;const char *payload="INFO: [sys] tick=10 seq=0 ch=0 n=48\r\n";
    LogLinkBuild(frame,LOG_LINK_ACK,12345);
    for(split=0;split<=16;split++) {
        memset(&p,0,sizeof(p));allow_payload=0;callback_count=0;
        a=LogLinkFilter(&p,frame,split,out,&allow_payload,host_on_frame);
        b=LogLinkFilter(&p,frame+split,16-split,out+a,&allow_payload,host_on_frame);
        assert(a+b==0 && allow_payload==1 && callback_count==1 && p.used==0);
    }
    puts("PASS: ACK fragmented at every byte boundary");
    memcpy(mixed,"discard",7);memcpy(mixed+7,frame,16);memcpy(mixed+23,payload,strlen(payload));
    memset(&p,0,sizeof(p));allow_payload=0;
    n=LogLinkFilter(&p,mixed,23+strlen(payload),out,&allow_payload,host_on_frame);
    assert(n==strlen(payload) && memcmp(out,payload,n)==0);
    puts("PASS: pre-ready discard, ACK+log coalescing, first log preserved");
    memset(&p,0,sizeof(p));memcpy(bad,frame,16);bad[15]^=1;
    n=LogLinkFilter(&p,bad,16,out,NULL,host_on_frame);
    assert(n==16 && memcmp(bad,out,16)==0);
    puts("PASS: checksum failure cannot enable readiness or consume non-control data");
    memset(&p,0,sizeof(p));callback_count=0;allow_payload=0;
    LogLinkBuild(frame,LOG_LINK_ACK,54321);
    n=LogLinkFilter(&p,frame,16,out,&allow_payload,host_on_frame);
    assert(n==0 && allow_payload==0);
    puts("PASS: stale session ACK does not enable payload");
    memset(&p,0,sizeof(p));
    a=LogLinkFilter(&p,(const uint8_t*)"R",1,out,NULL,host_on_frame);
    b=LogLinkFilter(&p,(const uint8_t*)"Xabc",4,out+a,NULL,host_on_frame);
    assert(a+b==5 && memcmp(out,"RXabc",5)==0);
    puts("PASS: partial magic is returned unchanged on mismatch");

    hUsbDeviceHS.dev_state=USBD_STATE_CONFIGURED;tx_result=USBD_BUSY;
    LogLinkOnFrame(LOG_LINK_READY,6789);LogLinkService_HS();
    assert(!g_log_link_ready && log_link_ack_needed && g_log_link_diag.ack_failed==1);
    tx_result=USBD_OK;LogLinkService_HS();
    assert(g_log_link_ready && !log_link_ack_needed && captured[4]==LOG_LINK_ACK);
    assert(LogLinkRead32(captured+8)==6789 && LogLinkRead32(captured+12)==LogLinkChecksum(captured));
    assert(irq_state_mock==0);
    LogLinkOnFrame(LOG_LINK_READY,6789);LogLinkService_HS();
    assert(g_log_link_ready && g_log_link_diag.ack_queued==2);
    LogLinkOnFrame(LOG_LINK_READY,6790);assert(!g_log_link_ready);LogLinkService_HS();
    assert(g_log_link_ready && LogLinkRead32(captured+8)==6790);
    hUsbDeviceHS.dev_state=USBD_STATE_SUSPENDED;LogLinkService_HS();assert(!g_log_link_ready);
    hUsbDeviceHS.dev_state=USBD_STATE_CONFIGURED;LogLinkService_HS();assert(g_log_link_ready);
    hUsbDeviceHS.dev_state=0;LogLinkService_HS();assert(!g_log_link_ready);
    puts("PASS: actual sender service, queue-full retry, duplicate READY, new session, disconnect");
    return 0;
}
