from pathlib import Path
from datetime import datetime
import re, json

root=Path(__file__).resolve().parent
data=Path('E:/Rb162.txt').read_bytes();body=data[352:]
pattern=re.compile(rb'^(\d{4}-\d{2}:\d{2}:\d{2}) INFO: \[([^]]+)\] tick=(\d+) seq=(\d+) ch=(\d+) n=(\d+) w=([0-9A-F/]+)\r\n$')
tags=['sys','task','fdcan1','fdcan23','uart123','uart6910','nvic','dji','dm','imu']
counts={tag:0 for tag in tags};rows=[];errors=[];seq_gaps=[];tick_gaps=[];clock_events=[];channel_errors=[];channel_gaps=[]
last_channel={};task_samples=[];partial=b'';complete_bytes=0;raw_lengths=[]
for line_no,line in enumerate(body.splitlines(keepends=True),9):
    if not line.endswith(b'\n'):
        partial=line;continue
    m=pattern.fullmatch(line)
    if not m:
        errors.append({'line':line_no,'text':line[:160].decode('ascii',errors='replace')});continue
    stamp,tag,tick,seq,ch,n,words=m.groups()
    tag=tag.decode();tick=int(tick);seq=int(seq);ch=int(ch);n=int(n);fields=words.split(b'/')
    row={'line':line_no,'stamp':stamp.decode(),'tag':tag,'tick':tick,'seq':seq,'ch':ch}
    if n!=48 or len(fields)!=48 or any(len(f)!=8 for f in fields):errors.append({'line':line_no,'reason':'data word count/width'})
    if ch>=10 or tag!=tags[ch] or ch!=seq%10:channel_errors.append(row)
    if tag in counts:counts[tag]+=1
    if rows:
        old=rows[-1];sd=seq-old['seq'];td=tick-old['tick']
        if sd!=1:seq_gaps.append({'before':old,'after':row,'delta':sd,'missing':max(sd-1,0)})
        if td!=10:tick_gaps.append({'before':old,'after':row,'delta_ms':td})
        seconds=(datetime.strptime('2026'+row['stamp'],'%Y%m%d-%H:%M:%S')-datetime.strptime('2026'+old['stamp'],'%Y%m%d-%H:%M:%S')).total_seconds()
        if seconds<0 or seconds>1:clock_events.append({'before':old,'after':row,'seconds':seconds})
    if ch in last_channel and tick-last_channel[ch]['tick']!=100:
        channel_gaps.append({'before':last_channel[ch],'after':row})
    last_channel[ch]=row
    if tag=='task':task_samples.append([int(f,16) for f in fields])
    rows.append(row);complete_bytes+=len(line);raw_lengths.append(len(line)-14)
expected=rows[-1]['seq']-rows[0]['seq']+1;missing=sum(g['missing'] for g in seq_gaps)
span=(rows[-1]['tick']-rows[0]['tick'])/1000
# 每条对应 10ms 生成槽位，用记录数量对应的时间计算平均吞吐量。
sample_time=expected*0.01
last=task_samples[-1] if task_samples else None
diagnostics=None if last is None else {'debug_stack_min_words':min(s[7] for s in task_samples),
    'last_debug_stack_words':last[7], 'max_gap_ms':last[13],'late_count':last[14],
    'catchup_count':last[15],'max_run_ms':last[16],'queue_overflow_count':last[19],
    'queue_high_water':last[26],'start_failed':last[27],'format_truncated':last[28],
    'format_error':last[29],'last_line_bytes':last[30],'max_line_bytes':last[31]}
result={'file_bytes':len(data),'body_bytes':len(body),'blocks16k':len(body)/16384,
    'complete_rows':len(rows),'first':rows[0],'last':rows[-1],'tick_span_seconds':span,
    'expected_records':expected,'missing_records':missing,'missing_rate_pct':missing/expected*100,
    'complete_rate_pct':len(rows)/expected*100,'seq_gaps':seq_gaps,'tick_gaps':tick_gaps,
    'rtc_events':clock_events,'channel_errors':channel_errors,'channel_period_errors':channel_gaps,
    'format_errors':errors,'channel_counts':counts,'raw_line_bytes_min':min(raw_lengths),
    'raw_line_bytes_max':max(raw_lengths),'raw_line_bytes_mean':sum(raw_lengths)/len(raw_lengths),
    'raw_bytes_per_second':sum(raw_lengths)/sample_time,
    'timestamped_bytes_per_second':complete_bytes/sample_time,
    'partial_bytes':len(partial),'partial_text':partial.decode('ascii',errors='replace'),
    'observed_task_diagnostics':diagnostics}
(root/'rb162_analysis.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
report=['# Rb162：启动握手后日志审核','',
    f'完整记录 {len(rows)} 条，源 tick 跨度 {span:.2f} 秒；seq 推算缺失 {missing} 条，缺失率 {missing/expected*100:.4f}%，完整率 {len(rows)/expected*100:.4f}%。',
    f'首末 tick={rows[0]["tick"]}～{rows[-1]["tick"]}，seq={rows[0]["seq"]}～{rows[-1]["seq"]}。',
    f'tick 间断 {len(tick_gaps)}，seq 间断 {len(seq_gaps)}，RTC 异常 {len(clock_events)}；通道映射异常 {len(channel_errors)}，100ms 通道周期异常 {len(channel_gaps)}，完整行格式异常 {len(errors)}。','',
    f'原始行长度 {min(raw_lengths)}～{max(raw_lengths)} 字节，平均 {sum(raw_lengths)/len(raw_lengths):.2f} 字节。按记录覆盖的 10ms 槽位计算，原始吞吐约 {result["raw_bytes_per_second"]/1000:.2f}KB/s，加 RTC 时间戳后的吞吐约 {result["timestamped_bytes_per_second"]/1000:.2f}KB/s（十进制 KB）。',
    f'正文为 {len(body)/16384:g} 个完整 16KB 块；末尾残行 {len(partial)} 字节，不计入首末完整 seq 区间的缺失率。','',
    '| 通道 | 完整行数 |','|---|---:|']
for tag,count in counts.items():report.append(f'| {tag} | {count} |')
report+=['','## 日志携带的诊断统计','', '```json',json.dumps(diagnostics,ensure_ascii=False,indent=2),'```','',
    '日志中诊断字段为对应采样时刻的值，栈水位以 32 位字为单位；不是测试结束后的同步 Watch 快照。仅说明本次短时间测试表现，不保证长期稳定性。','',
    '缺失率指生成日志记录缺失率，不是 USB 底层数据包丢包率。','', '## 文件末尾','', '```text',partial.decode('ascii',errors='replace'),'```']
(root/'Rb162_review.md').write_text('\n'.join(report)+'\n',encoding='utf-8')
print(json.dumps(result,ensure_ascii=False,indent=2))
