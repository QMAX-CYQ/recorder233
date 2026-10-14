from pathlib import Path
import re, json, subprocess

root=Path(__file__).resolve().parent
header=(root/'User/Support/log.h').read_text(encoding='utf-8')
log=(root/'User/Support/log.c').read_text(encoding='utf-8')
debug=(root/'User/Task/debug_task.c').read_text(encoding='utf-8')
diag=header[header.index('typedef struct {'):header.index('} log_tx_diag_t;')+len('} log_tx_diag_t;')]
start=debug.index('static void debug_send_snapshot(')
end=debug.index('\nvoid debug_task(',start)
emit=debug[start:end]
log=re.sub(r'^#include[^\n]*\n','',log,flags=re.M)
prelude=r'''
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <assert.h>
#define LOG_LEVEL_NUM 4
#define LEVEL_SET 3
#define LOG_INFO_LEVEL 2
#define USBD_OK 0
#define USBD_BUSY 1
#define LOG_FORMAT_BUFFER_SIZE 768U
#define DEBUG_LOG_CHANNEL_COUNT 10U
#define DEBUG_INFO(fmt,...) sys_printf(LOG_INFO_LEVEL,"INFO: ",fmt,__VA_ARGS__)
static char sent[2048];
static uint32_t sent_len;
static uint32_t osKernelSysTick(void) { return 100; }
static void osDelay(uint32_t value) { (void)value; }
static uint8_t CDC_Transmit_HS(uint8_t *buf, uint16_t len) {
    assert(len < sizeof(sent)); memcpy(sent,buf,len); sent[len]=0; sent_len=len; return USBD_OK;
}
static void debug_snapshot(uint8_t channel, uint32_t *words) {
    unsigned i; (void)channel;
    for(i=0;i<48;i++) words[i]=UINT32_MAX;
}
'''
tests=r'''
int main(void) {
    unsigned ch, separators, i;
    for(ch=0;ch<10;ch++) {
        debug_send_snapshot((uint8_t)ch, UINT32_MAX, UINT32_MAX);
        assert(sent_len <= 493);
        assert(sent[sent_len-2]=='\r' && sent[sent_len-1]=='\n');
        separators=0; for(i=0;i<sent_len;i++) if(sent[i]=='/') separators++;
        assert(separators==47);
        assert(strstr(sent," tick=4294967295 seq=4294967295")!=NULL);
        assert(strstr(sent," n=48 w=FFFFFFFF/")!=NULL);
        printf("PASS channel=%u bytes=%u\n",ch,sent_len);
    }
    assert(g_log_tx_diag.format_error==0 && g_log_tx_diag.format_truncated==0);
    assert(g_log_tx_diag.attempts==10 && g_log_tx_diag.accepted==10);
    assert(g_log_tx_diag.max_line_bytes==493);
    puts("PASS: actual sys_printf + stress format, all 10 channels retain 48 words and CRLF");
    return 0;
}
'''
(root/'stress_format_test.c').write_text(prelude+diag+'\n'+log+emit+tests,encoding='utf-8')
