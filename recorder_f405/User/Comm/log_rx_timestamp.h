#ifndef LOG_RX_TIMESTAMP_H
#define LOG_RX_TIMESTAMP_H

#include <stddef.h>
#include <stdint.h>

/* RTC 前缀为 MMDD-HH:MM:SS 加空格，共 14 字节；毫秒计数前缀不超过此长度。
 * 最密集的非空行是单字符加换行，额外计入最后一个未换行的字符。 */
#define LOG_RX_TIMESTAMP_MAX_PREFIX 14U
#define LOG_RX_TIMESTAMP_CAPACITY(n) \
    ((n) + (((n) + 1U) / 2U) * LOG_RX_TIMESTAMP_MAX_PREFIX)

/* 使用调用方已提交的行首状态格式化，不修改该状态。
 * 成功后返回输出长度及下一行首状态；只有整块写入总环成功后才提交新状态。
 * 失败时输出缓冲区可能已有部分内容，长度和下一状态保持不变，不得提交。 */
uint8_t LogRxFormatTimestamp(const uint8_t *input, size_t input_len,
    uint8_t line_start, uint8_t *output, size_t capacity,
    size_t *output_len, uint8_t *next_line_start);

#endif
