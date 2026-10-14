
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
typedef struct {
    uint32_t attempts, accepted, busy, failed;
    uint32_t attempted_bytes, accepted_bytes;
    uint32_t format_error, format_truncated;
    uint32_t last_status, last_failure_tick;
    uint32_t failure_streak, max_failure_streak;
    uint32_t failure_history_next;
    uint32_t failure_tick[8], failure_status[8];
    uint32_t last_accepted_tick, max_accepted_gap_ms;
    uint32_t last_line_bytes, max_line_bytes;
} log_tx_diag_t;

volatile log_tx_diag_t g_log_tx_diag;

void sys_printf(uint8_t level, const char *tag, const char *fmt, ...)
{
    if (level >= LOG_LEVEL_NUM || level >= LEVEL_SET || fmt == NULL)
        return;
    size_t len;
    char buf[LOG_FORMAT_BUFFER_SIZE];
    int off = 0;
    int format_len;
    uint8_t tx_status;
    uint32_t tx_tick;
    const char *log_tag = (tag != NULL) ? tag : "";

	off = snprintf(buf, sizeof(buf), "%s",log_tag);
    if (off < 0 || off >= (int)sizeof(buf))
        return;
    
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
}




static void debug_send_snapshot(uint8_t channel, uint32_t tick, uint32_t sequence)
{
    static const char *const tags[DEBUG_LOG_CHANNEL_COUNT] = {
        "sys", "task", "fdcan1", "fdcan23", "uart123", "uart6910", "nvic", "dji", "dm", "imu"
    };
    uint32_t words[48];
    debug_snapshot(channel, words);
    DEBUG_INFO("[%s] tick=%lu seq=%lu ch=%u n=48 w=%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX/%08lX\r\n",
        tags[channel], (unsigned long)tick, (unsigned long)sequence, (unsigned int)channel,
        (unsigned long)words[0], (unsigned long)words[1], (unsigned long)words[2], (unsigned long)words[3],
        (unsigned long)words[4], (unsigned long)words[5], (unsigned long)words[6], (unsigned long)words[7],
        (unsigned long)words[8], (unsigned long)words[9], (unsigned long)words[10], (unsigned long)words[11],
        (unsigned long)words[12], (unsigned long)words[13], (unsigned long)words[14], (unsigned long)words[15],
        (unsigned long)words[16], (unsigned long)words[17], (unsigned long)words[18], (unsigned long)words[19],
        (unsigned long)words[20], (unsigned long)words[21], (unsigned long)words[22], (unsigned long)words[23],
        (unsigned long)words[24], (unsigned long)words[25], (unsigned long)words[26], (unsigned long)words[27],
        (unsigned long)words[28], (unsigned long)words[29], (unsigned long)words[30], (unsigned long)words[31],
        (unsigned long)words[32], (unsigned long)words[33], (unsigned long)words[34], (unsigned long)words[35],
        (unsigned long)words[36], (unsigned long)words[37], (unsigned long)words[38], (unsigned long)words[39],
        (unsigned long)words[40], (unsigned long)words[41], (unsigned long)words[42], (unsigned long)words[43],
        (unsigned long)words[44], (unsigned long)words[45], (unsigned long)words[46], (unsigned long)words[47]);
}

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
