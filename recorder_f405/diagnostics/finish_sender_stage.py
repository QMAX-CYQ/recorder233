from pathlib import Path
import json, difflib
root = Path(__file__).resolve().parent
stage = root / 'sender'
def update(relative, old, new):
    path = stage / relative
    data = path.read_bytes().decode('latin1').replace('\r\n', '\n')
    assert data.count(old) == 1, (relative, old)
    path.write_bytes(data.replace(old, new).replace('\n', '\r\n').encode('latin1'))
update('User/Support/log.h', '    uint32_t failure_streak, max_failure_streak;',
       '    uint32_t failure_streak, max_failure_streak;\n    uint32_t failure_history_next;\n    uint32_t failure_tick[8], failure_status[8];')
update('User/Support/log.c', '        g_log_tx_diag.last_failure_tick = tx_tick;', '''        uint32_t failure_slot = g_log_tx_diag.failure_history_next % 8U;
        g_log_tx_diag.failure_tick[failure_slot] = tx_tick;
        g_log_tx_diag.failure_status[failure_slot] = tx_status;
        g_log_tx_diag.failure_history_next++;
        g_log_tx_diag.last_failure_tick = tx_tick;''')
path = stage / 'USB_DEVICE/App/usbd_cdc_if.c'
text = path.read_bytes().decode('latin1').replace('\r\n','\n')
text = text.replace('volatile cdc_tx_diag_t g_cdc_tx_diag;', '''volatile cdc_tx_diag_t g_cdc_tx_diag;
/* Multiple task callers: update only diagnostic counters atomically. */
#define CDC_DIAG_ADD(field, value) \\
  ((void)__atomic_fetch_add(&g_cdc_tx_diag.field, (uint32_t)(value), __ATOMIC_RELAXED))''')
for field in ('attempts','accepted','busy','invalid','start_failed','completed','init_count','deinit_count'):
    text = text.replace(f'g_cdc_tx_diag.{field}++;', f'CDC_DIAG_ADD({field}, 1U);')
text = text.replace('g_cdc_tx_diag.accepted_bytes += Len;', 'CDC_DIAG_ADD(accepted_bytes, Len);')
text = text.replace('g_cdc_tx_diag.completed_bytes += *Len;', 'CDC_DIAG_ADD(completed_bytes, *Len);')
path.write_bytes(text.replace('\n','\r\n').encode('latin1'))
with (root / 'sender.patch').open('w', encoding='utf8') as out:
    for item in json.loads((root / 'sender_manifest.json').read_text(encoding='utf8')):
        original = Path(item['source']).read_bytes().decode('latin1').replace('\r\n','\n')
        edited = Path(item['staged']).read_bytes().decode('latin1').replace('\r\n','\n')
        out.writelines(difflib.unified_diff(original.splitlines(True), edited.splitlines(True),
                                           fromfile=item['source'], tofile=item['staged']))
print('Sender stage finalized: atomic all-caller counts and last eight log failures.')
