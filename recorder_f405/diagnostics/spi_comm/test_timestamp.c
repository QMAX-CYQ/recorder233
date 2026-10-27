#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "calendar.h"
#include "recorder_inter.h"
#include "log_rx_timestamp.h"

static uint32_t fake_tick = UINT32_MAX;
uint32_t HAL_GetTick(void) { return fake_tick; }
uint8_t Calendar_ReadTime(time_t *value)
{
    value->year = 2026U; value->month = 10U; value->day = 25U;
    value->hour = 12U; value->min = 34U; value->second = 56U;
    return 1U;
}
#if USE_RTC
#define PREFIX "1025-12:34:56 "
#else
#define PREFIX "4294967295 "
#endif

int main(void)
{
    const char input[] = "\r\nINFO: abc\r\n\r\nWARN: z\nlast";
    const char expected[] = "\r\n" PREFIX "INFO: abc\r\n\r\n" PREFIX "WARN: z\n" PREFIX "last";
    uint8_t output[LOG_RX_TIMESTAMP_CAPACITY(512U)];
    uint8_t dense[512], line, next;
    size_t cut, n, m, total, i;

    /* 穷举分包位置，覆盖空包、半行、CR/LF 分离及连续空行。 */
    for (cut = 0U; cut <= strlen(input); cut++) {
        assert(LogRxFormatTimestamp((const uint8_t *)input, cut, 1U,
            output, sizeof(output), &n, &next));
        line = next;
        assert(LogRxFormatTimestamp((const uint8_t *)input + cut, strlen(input) - cut,
            line, output + n, sizeof(output) - n, &m, &next));
        assert(n + m == strlen(expected));
        assert(memcmp(output, expected, n + m) == 0);
        assert(next == 0U);
    }
    /* 每次只提交一个字节，行首状态跨任意数量的数据块保留。 */
    total = 0U; line = 1U;
    for (i = 0U; i < strlen(input); i++) {
        assert(LogRxFormatTimestamp((const uint8_t *)input + i, 1U, line,
            output + total, sizeof(output) - total, &n, &next));
        total += n; line = next;
    }
    assert(total == strlen(expected) && memcmp(output, expected, total) == 0);

    for (i = 0U; i < sizeof(dense); i++) dense[i] = i % 2U ? '\n' : 'a';
    assert(LogRxFormatTimestamp(dense, sizeof(dense), 1U, output, sizeof(output), &n, &next));
    assert(n == 256U * (strlen(PREFIX) + 2U) && next == 1U);
    for (i = 0U; i < 256U; i++) {
        size_t offset = i * (strlen(PREFIX) + 2U);
        assert(memcmp(output + offset, PREFIX "a\n", strlen(PREFIX) + 2U) == 0);
    }
    /* 容量不足时不发布长度或下一状态，调用者可保留原状态重新处理。 */
    n = 123U; next = 7U;
    assert(!LogRxFormatTimestamp((const uint8_t *)"x", 1U, 1U, output, 1U, &n, &next));
    assert(n == 123U && next == 7U);
    assert(!LogRxFormatTimestamp(NULL, 1U, 1U, output, sizeof(output), &n, &next));
    assert(LogRxFormatTimestamp(NULL, 0U, 0U, output, sizeof(output), &n, &next));
    assert(n == 0U && next == 0U);
#if !USE_RTC
    fake_tick = 0U;
    assert(LogRxFormatTimestamp((const uint8_t *)"x", 1U, 1U, output, sizeof(output), &n, &next));
    assert(n == 3U && memcmp(output, "0 x", n) == 0);
#endif
    puts("PASS: RTC/tick prefix, every packet split, blank lines, dense lines, capacity failure");
    return 0;
}
