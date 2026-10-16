from pathlib import Path
from collections import Counter
from datetime import datetime
import json
import re

base = Path(__file__).resolve().parent
data = (base / 'rb3_snapshot.txt').read_bytes()
body = data[352:]
info_pattern = re.compile(rb'^(\d{4}-\d{2}:\d{2}:\d{2}) INFO: \[([^]]+)\] tick=(\d+) seq=(\d+) ch=(\d+) n=48 w=([0-9A-F]{8}(?:/[0-9A-F]{8}){47})\r\n$')
warn_pattern = re.compile(rb'^(\d{4}-\d{2}:\d{2}:\d{2}) WARN: \[offline\] module=(\w+) source_tick=(\d+) send_tick=(\d+) last_rx_tick=(\d+) can_index=(\d+) can_id=0x([0-9A-F]+)\r\n$')
tags = ['sys','task','fdcan1','fdcan23','uart123','uart6910','nvic','dji','dm','imu']
rows, warnings, malformed, tick_errors, seq_errors, channel_errors, channel_period_errors, rtc_errors, task_samples = [], [], [], [], [], [], [], [], []
last_channel = {}
counts = Counter()
rtc_previous = None
partial = b''
for line_no, line in enumerate(body.splitlines(keepends=True), 9):
    if not line.endswith(b'\n'):
        partial = line
        continue
    m = info_pattern.fullmatch(line)
    warning = warn_pattern.fullmatch(line)
    if m:
        stamp, tag, tick, seq, channel, words = m.groups()
        row = dict(line=line_no, stamp=stamp.decode(), tag=tag.decode(), tick=int(tick), seq=int(seq), ch=int(channel))
        if rows:
            prev = rows[-1]
            sd, td = row['seq']-prev['seq'], row['tick']-prev['tick']
            if sd != 1: seq_errors.append(dict(before=prev, after=row, delta=sd, missing=max(0, sd-1)))
            if td != 10: tick_errors.append(dict(before=prev, after=row, delta_ms=td))
        ch = row['ch']
        if ch >= 10 or row['tag'] != tags[ch] or ch != row['seq']%10: channel_errors.append(row)
        if ch in last_channel and row['tick']-last_channel[ch]['tick'] != 100:
            channel_period_errors.append(dict(before=last_channel[ch],after=row))
        last_channel[ch] = row
        counts[row['tag']] += 1
        if row['tag']=='task': task_samples.append([int(w,16) for w in words.split(b'/')])
        rows.append(row)
    elif warning:
        stamp, module, source, send, rx, can, ident = warning.groups()
        warnings.append(dict(line=line_no,stamp=stamp.decode(),module=module.decode(),source_tick=int(source),send_tick=int(send),last_rx_tick=int(rx),can_index=int(can),can_id=hex(int(ident,16)),mixed_clock_difference_ms=int(send)-int(source)))
    else:
        malformed.append(dict(line=line_no,bytes=len(line),text=line[:170].decode('ascii',errors='replace')))
        continue
    clock = datetime.strptime('2026'+stamp.decode(), '%Y%m%d-%H:%M:%S')
    if rtc_previous:
        delta = (clock-rtc_previous).total_seconds()
        if delta<0 or delta>1: rtc_errors.append(dict(line=line_no,delta_seconds=delta))
    rtc_previous=clock

indices={'max_gap_ms':13,'late_count':14,'catchup_count':15,'max_run_ms':16,'usb_pending_count':17,'queue_overflow_count':19,'queue_high_water':26,'start_failed':27,'format_truncated':28,'format_error':29,'max_line_bytes':31}
expected=rows[-1]['seq']-rows[0]['seq']+1
missing=sum(x['missing'] for x in seq_errors)
diag={name:dict(first=task_samples[0][idx],last=task_samples[-1][idx],minimum=min(s[idx] for s in task_samples),maximum=max(s[idx] for s in task_samples)) for name,idx in indices.items()}
diag['stack_min_words']={name:min(s[i] for s in task_samples) for i,name in enumerate(['comm','mode','status','chassis','gimbal','shoot','ui','debug','board'])}
diag['heap_free_min_bytes']=min(s[10] for s in task_samples)
diag['heap_ever_min_bytes']=min(s[11] for s in task_samples)
result=dict(file_bytes=len(data),body_bytes=len(body),body_blocks16k=len(body)/16384,header=data[:352].decode('ascii',errors='replace'),complete_info=len(rows),first=rows[0],last=rows[-1],span_seconds=(rows[-1]['tick']-rows[0]['tick'])/1000,expected_info=expected,missing_info=missing,missing_rate_pct=missing/expected*100,seq_errors=seq_errors,tick_errors=tick_errors,channel_errors=channel_errors,channel_period_errors=channel_period_errors,rtc_errors=rtc_errors,channel_counts=counts,warnings=warnings,warning_count=len(warnings),malformed=malformed,partial_bytes=len(partial),partial_text=partial.decode('ascii',errors='replace'),zero_bytes=data.count(b'\0'),diagnostics=diag)
(base/'rb3_analysis.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(result,ensure_ascii=False,indent=2))
