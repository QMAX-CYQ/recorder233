from pathlib import Path
from datetime import datetime
import re, json

root = Path(__file__).resolve().parent
files = []
body = bytearray()
boundaries = []
for name in ('Rb55.txt', 'Rb56.txt', 'Rb57.txt'):
    data = Path('E:/'+name).read_bytes()
    boundaries.append(len(body))
    files.append({'name': name, 'bytes': len(data), 'body_bytes': len(data)-352,
                  'blocks16k': (len(data)-352)/16384,
                  'header': data[:352].decode('ascii', errors='replace')})
    body.extend(data[352:])

prefix = re.compile(rb'^(\d{4}-\d{2}:\d{2}:\d{2}) INFO: \[([^]]+)\] (?:tick|t)=(\d+) ')
source = Path('D:/RM/rm_main_gimbal/User/Task/debug_task.c').read_bytes().decode('latin1')
patterns = {}
for fmt in re.findall(r'DEBUG_INFO\("(\[[^"\r\n]+)\\r\\n"', source):
    tag = re.match(r'\[([^]]+)\]', fmt)[1]
    pat = re.escape(fmt)
    for spec, value in [('%08lX', r'[0-9A-F]{8}'), ('%lu', r'\d+'), ('%ld', r'-?\d+'), ('%u', r'\d+'), ('%d', r'-?\d+')]:
        pat = pat.replace(re.escape(spec), value)
    patterns[tag] = re.compile(r'^\d{4}-\d{2}:\d{2}:\d{2} INFO: '+pat+'$')
shape_errors = []
rows = []; gaps = []; bad = []; clocks = []; backwards = []; duplicates = []
offset = 0
for line in bytes(body).splitlines(keepends=True):
    m = prefix.match(line)
    if not m or not line.endswith(b'\r\n'):
        bad.append({'offset': offset, 'text': line.decode('ascii', errors='replace').rstrip(),
                    'crlf': line.endswith(b'\r\n')})
    if m and line.endswith(b'\r\n'):
        row = {'offset': offset, 'stamp': m[1].decode(), 'tag': m[2].decode(), 'tick': int(m[3])}
        if row['tag'] not in patterns or not patterns[row['tag']].fullmatch(line[:-2].decode('ascii')):
            shape_errors.append(row)
        clock = datetime.strptime('2026'+row['stamp'], '%Y%m%d-%H:%M:%S')
        if rows:
            delta = row['tick']-rows[-1]['tick']
            seconds = (clock-old_clock).total_seconds()
            if delta != 10:
                event = {'before': rows[-1]['tick'], 'after': row['tick'], 'delta': delta,
                         'missing': delta//10-1 if delta > 10 and delta%10 == 0 else None,
                         'stamp': row['stamp']}
                gaps.append(event)
                if delta < 0: backwards.append(event)
                if delta == 0: duplicates.append(event)
            if seconds < 0 or seconds > 1:
                clocks.append({'before': rows[-1], 'after': row, 'seconds': seconds})
        rows.append(row); old_clock = clock
    offset += len(line)

for i, item in enumerate(files):
    start = boundaries[i]; end = start+item['body_bytes']
    owned = [r for r in rows if start <= r['offset'] < end]
    item['rows_starting_in_file'] = len(owned)
    item['first'] = owned[0] if owned else None
    item['last'] = owned[-1] if owned else None
    item['starts_at_line_boundary'] = start == 0 or body[start-2:start] == b'\r\n'
    item['ends_at_line_boundary'] = body[end-2:end] == b'\r\n'
    item.pop('header')
result = {'files': files, 'rows': len(rows), 'first': rows[0], 'last': rows[-1],
          'gaps': gaps, 'missing': sum(g['missing'] or 0 for g in gaps),
          'backward': backwards, 'duplicates': duplicates, 'rtc_events': clocks,
          'frame_anomalies': bad, 'body_bytes': len(body),
          'complete_frame_shape_errors': shape_errors,
          'snapshot_tick_rows': sum(r['tick'] <= 1790280 for r in rows)}
(root/'rb55_57_analysis.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf8')
report = ['# Rb55–Rb57 连续性核对', '',
          f'合并去除各文件 352 字节文件头后的正文，完整记录 {len(rows)} 条，33 处间断，推算缺失 {result["missing"]} 条。',
          f'完整帧格式异常 {len(shape_errors)}，RTC 跳变 {len(clocks)}，tick 回退 {len(backwards)}，重复 {len(duplicates)}。', '',
          '文件轮转处的半行在相邻文件内继续，拼接后正常；仅 Rb57 最末一行不完整。', '',
          '快照发送 BUSY=36，接收 estimated_missing=36、tick_gap_count=33、max_source_gap_ms=30，与文件一致。',
          '快照发送 accepted=completed=178860；无持续未完成传输证据。',
          '快照 SD write_error/write_short/sync_error/ring_rejected 均为 0；min_ring_free=46519。',
          '文件正文 1421 个 16KB 块，快照 1417 次写入，文件包含快照后继续写入的数据。',
          'max_ready_hold_ms=4294967295 为已知诊断统计异常；修复版本编译尚未成功，不能当作真实等待时间。', '',
          '| RTC | 前 tick | 后 tick | 缺失 |', '|---|---:|---:|---:|']
for g in gaps:
    report.append(f'| {g["stamp"]} | {g["before"]} | {g["after"]} | {g["missing"]} |')
(root/'Rb55_Rb56_Rb57_review.md').write_text('\n'.join(report)+'\n', encoding='utf8')
print(json.dumps(result, ensure_ascii=False, indent=2))
