"""Stage sender edits; apply receiver instrumentation. Preserve source bytes/CRLF."""
from pathlib import Path
import hashlib, json, difflib

ROOT = Path(__file__).resolve().parents[1]
SENDER = Path('D:/RM/rm_main_gimbal')
STAGE = ROOT / 'diagnostics' / 'sender'
manifest = []

def edit(root, relative, replacements, staged=False):
    path = root / relative
    original = path.read_bytes()
    text = original.decode('latin1').replace('\r\n', '\n')
    before = text
    for old, new in replacements:
        if text.count(old) != 1:
            raise RuntimeError(f'{relative}: expected one match: {old[:70]!r}')
        text = text.replace(old, new, 1)
    result = text.replace('\n', '\r\n').encode('latin1')
    if staged:
        dest = STAGE / relative
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(result)
        manifest.append({'source': str(path), 'staged': str(dest),
                         'sha256': hashlib.sha256(original).hexdigest()})
        with (ROOT / 'diagnostics' / 'sender.patch').open('a', encoding='utf8') as out:
            out.writelines(difflib.unified_diff(before.splitlines(True), text.splitlines(True),
                                                fromfile=str(path), tofile=str(dest)))
    else:
        backup = ROOT / 'diagnostics' / 'original_receiver' / relative
        backup.parent.mkdir(parents=True, exist_ok=True)
        backup.write_bytes(original)
        path.write_bytes(result)

(ROOT / 'diagnostics' / 'sender.patch').write_text('', encoding='utf8')
edit(SENDER, 'User/Support/log.h', [('void sys_printf', '''/* Debugger-only counters; no extra log traffic. Counts reset on MCU reset. */
typedef struct {
    uint32_t attempts, accepted, busy, failed;
    uint32_t attempted_bytes, accepted_bytes;
    uint32_t format_error, format_truncated;
    uint32_t last_status, last_failure_tick;
    uint32_t failure_streak, max_failure_streak;
    uint32_t last_accepted_tick, max_accepted_gap_ms;
} log_tx_diag_t;
extern volatile log_tx_diag_t g_log_tx_diag;

void sys_printf''')], True)
edit(SENDER, 'User/Support/log.c', [
    ('void sys_printf', 'volatile log_tx_diag_t g_log_tx_diag;\n\nvoid sys_printf'),
    ('    int off = 0;', '    int off = 0;\n    int format_len;\n    uint8_t tx_status;\n    uint32_t tx_tick;'),
    ('    vsnprintf(buf + off, sizeof(buf) - off, fmt, args);',
     '    format_len = vsnprintf(buf + off, sizeof(buf) - off, fmt, args);'),
    ('\tif(CDC_Transmit_HS((uint8_t*)buf,len) != USBD_OK)\n\t\tosDelay(1);', '''    /* Observe the original single send attempt; do not retry or change payload. */
    if (format_len < 0) g_log_tx_diag.format_error++;
    else if ((size_t)format_len >= sizeof(buf) - (size_t)off)
        g_log_tx_diag.format_truncated++;
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
        g_log_tx_diag.last_failure_tick = tx_tick;
        g_log_tx_diag.failure_streak++;
        if (g_log_tx_diag.failure_streak > g_log_tx_diag.max_failure_streak)
            g_log_tx_diag.max_failure_streak = g_log_tx_diag.failure_streak;
        osDelay(1);
    }''')], True)
edit(SENDER, 'User/Task/debug_task.h', [('void debug_task', '''#include "stdint.h"
typedef struct {
    uint32_t loops, last_tick, max_gap_ms, late_count, catchup_count;
    uint32_t group_count[8];
} debug_task_diag_t;
extern volatile debug_task_diag_t g_debug_task_diag;

void debug_task''')], True)
edit(SENDER, 'User/Task/debug_task.c', [
    ('void debug_task(void const *argument)', 'volatile debug_task_diag_t g_debug_task_diag;\n\nvoid debug_task(void const *argument)'),
    ('        switch (log_group)', '''        if (g_debug_task_diag.loops != 0U) {
            uint32_t gap = tick - g_debug_task_diag.last_tick;
            if (gap > g_debug_task_diag.max_gap_ms) g_debug_task_diag.max_gap_ms = gap;
            if (gap > 10U) g_debug_task_diag.late_count++;
            if (gap < 10U) g_debug_task_diag.catchup_count++;
        }
        g_debug_task_diag.loops++;
        g_debug_task_diag.last_tick = tick;
        g_debug_task_diag.group_count[log_group]++;

        switch (log_group)''')], True)
edit(SENDER, 'USB_DEVICE/App/usbd_cdc_if.h', [
    ('/* USER CODE BEGIN EXPORTED_VARIABLES */', '''/* USER CODE BEGIN EXPORTED_VARIABLES */
/* All CDC callers (including vision), not just debug logs. */
typedef struct {
    uint32_t attempts, accepted, busy, invalid, start_failed;
    uint32_t accepted_bytes, completed, completed_bytes;
    uint32_t init_count, deinit_count;
} cdc_tx_diag_t;
extern volatile cdc_tx_diag_t g_cdc_tx_diag;''')], True)
edit(SENDER, 'USB_DEVICE/App/usbd_cdc_if.c', [
    ('/* Private variables ---------------------------------------------------------*/',
     '/* Private variables ---------------------------------------------------------*/\nvolatile cdc_tx_diag_t g_cdc_tx_diag;'),
    ('  /* USER CODE BEGIN 8 */', '  /* USER CODE BEGIN 8 */\n  g_cdc_tx_diag.init_count++;'),
    ('  /* USER CODE BEGIN 9 */', '  /* USER CODE BEGIN 9 */\n  g_cdc_tx_diag.deinit_count++;'),
    ('  /* USER CODE BEGIN 12 */', '  /* USER CODE BEGIN 12 */\n  g_cdc_tx_diag.attempts++;'),
    ('    return USBD_FAIL;\n  }\n  if (hcdc->TxState != 0){\n    return USBD_BUSY;',
     '    g_cdc_tx_diag.invalid++;\n    return USBD_FAIL;\n  }\n  if (hcdc->TxState != 0){\n    g_cdc_tx_diag.busy++;\n    return USBD_BUSY;'),
    ('  result = USBD_CDC_TransmitPacket(&hUsbDeviceHS);', '''  result = USBD_CDC_TransmitPacket(&hUsbDeviceHS);
  if (result == USBD_OK) {
    g_cdc_tx_diag.accepted++;
    g_cdc_tx_diag.accepted_bytes += Len;
  } else {
    g_cdc_tx_diag.start_failed++;
  }'''),
    ('  /* USER CODE BEGIN 14 */', '''  /* USER CODE BEGIN 14 */
  g_cdc_tx_diag.completed++;
  if (Len != NULL) g_cdc_tx_diag.completed_bytes += *Len;''')], True)
(ROOT / 'diagnostics' / 'sender_manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf8')

edit(ROOT, 'User/Recorder/recorder.h', [('void RecorderData(', '''/* Log-data writes only; excludes file headers and index metadata. */
typedef struct {
    uint32_t ring_attempts, ring_accepted_bytes, ring_rejected, ring_rejected_bytes;
    uint32_t dequeued_bytes, write_attempts, write_error, write_short;
    uint32_t write_reported_bytes, unconfirmed_bytes;
    uint32_t last_write_result, last_write_bytes, write_active;
    uint32_t last_write_ms, max_write_ms;
    uint32_t sync_attempts, sync_error, last_sync_result, max_sync_ms;
    uint32_t last_write_tick, last_sync_tick, last_write_error_tick;
    uint32_t mount_result, card_not_ready;
} recorder_diag_t;
extern volatile recorder_diag_t g_recorder_diag;

void RecorderData(''')])
edit(ROOT, 'User/Recorder/recorder.c', [
    ('#if USE_RECORDER', 'volatile recorder_diag_t g_recorder_diag;\n\n#if USE_RECORDER'),
    ('    return isSdCardInit;', '    g_recorder_diag.mount_result = (uint32_t)isSdCardInit;\n    return isSdCardInit;'),
    ('    if (++sd_not_ready_count >= 3)', '    g_recorder_diag.card_not_ready++;\n    if (++sd_not_ready_count >= 3)'),
    ('    uint32_t current_write_size = 0;', '    uint32_t current_write_size = 0;\n    uint32_t diagnostic_start;\n    uint32_t diagnostic_elapsed;'),
    ('            check_res = f_write(temp, pcfile_write_buffer, PCFILE_WRITE_BUFFER_SIZE, &write_number);', '''            g_recorder_diag.dequeued_bytes += PCFILE_WRITE_BUFFER_SIZE;
            g_recorder_diag.write_attempts++;
            diagnostic_start = HAL_GetTick();
            g_recorder_diag.last_write_tick = diagnostic_start;
            g_recorder_diag.write_active = 1U;
            check_res = f_write(temp, pcfile_write_buffer, PCFILE_WRITE_BUFFER_SIZE, &write_number);
            g_recorder_diag.write_active = 0U;
            diagnostic_elapsed = HAL_GetTick() - diagnostic_start;
            g_recorder_diag.last_write_ms = diagnostic_elapsed;
            if (diagnostic_elapsed > g_recorder_diag.max_write_ms)
                g_recorder_diag.max_write_ms = diagnostic_elapsed;
            g_recorder_diag.last_write_result = (uint32_t)check_res;
            g_recorder_diag.last_write_bytes = write_number;
            g_recorder_diag.write_reported_bytes += write_number;
            if (check_res != FR_OK) g_recorder_diag.write_error++;
            if (write_number != PCFILE_WRITE_BUFFER_SIZE) g_recorder_diag.write_short++;
            if (write_number < PCFILE_WRITE_BUFFER_SIZE)
                g_recorder_diag.unconfirmed_bytes += PCFILE_WRITE_BUFFER_SIZE - write_number;
            if ((check_res != FR_OK) || (write_number != PCFILE_WRITE_BUFFER_SIZE))
                g_recorder_diag.last_write_error_tick = HAL_GetTick();'''),
    ('        check_sync_res = f_sync(temp);', '''        g_recorder_diag.sync_attempts++;
        diagnostic_start = HAL_GetTick();
        g_recorder_diag.last_sync_tick = diagnostic_start;
        check_sync_res = f_sync(temp);
        diagnostic_elapsed = HAL_GetTick() - diagnostic_start;
        if (diagnostic_elapsed > g_recorder_diag.max_sync_ms)
            g_recorder_diag.max_sync_ms = diagnostic_elapsed;
        g_recorder_diag.last_sync_result = (uint32_t)check_sync_res;
        if (check_sync_res != FR_OK) g_recorder_diag.sync_error++;'''),
    ('    written = lwrb_write_ex(&pcfile_buffer_lwrb, buf, (lwrb_sz_t)len, NULL, LWRB_FLAG_WRITE_ALL);', '''    g_recorder_diag.ring_attempts++;
    written = lwrb_write_ex(&pcfile_buffer_lwrb, buf, (lwrb_sz_t)len, NULL, LWRB_FLAG_WRITE_ALL);
    if (written) g_recorder_diag.ring_accepted_bytes += (uint32_t)len;
    else {
        g_recorder_diag.ring_rejected++;
        g_recorder_diag.ring_rejected_bytes += (uint32_t)len;
    }''')])

print('Sender staged:', len(manifest), 'files; receiver recorder instrumentation applied.')
