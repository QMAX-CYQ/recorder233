from pathlib import Path
from datetime import datetime
import re, json

root=Path(__file__).resolve().parent
pattern=re.compile(rb'^(\d{4}-\d{2}:\d{2}:\d{2}) INFO: \[([^]]+)\] tick=(\d+) seq=(\d+) ch=(\d+) n=48 w=([0-9A-F]{8}(?:/[0-9A-F]{8}){47})\r\n$')
tags=['sys','task','fdcan1','fdcan23','uart123','uart6910','nvic','dji','dm','imu']
counts={t:0 for t in tags};files=[];boundaries=[];errors=[];seq_gaps=[];tick_errors=[];channel_errors=[];channel_period_errors=[];rtc_events=[]
first=None;previous=None;first_clock=None;previous_clock=None;stamp_cache=None;clock_cache=None
last_channel={};pending=b'';origin=None;rows=0;raw_bytes=0;complete_bytes=0;raw_min=99999;raw_max=0
tick_error_count=0;channel_error_count=0;channel_period_error_count=0;missing=0;resets=0;duplicate_seq=0;bad_count=0
drift_min=0;drift_max=0;task_first=None;task_last=None;task_sample_count=0
diag_indices={'debug_stack_words':7,'free_heap_bytes':10,'minimum_ever_free_heap_bytes':11,'max_gap_ms':13,'late_count':14,'catchup_count':15,'max_run_ms':16,'queue_overflow_count':19,'queue_high_water':26,'start_failed':27,'format_truncated':28,'format_error':29,'max_line_bytes':31}
diag_min={};diag_max={};stack_min=[None]*9

for number in range(164,168):
    name=f'Rb{number}.txt';data=Path('E:/'+name).read_bytes();body=data[352:];header=data[:352]
    info={'file':name,'bytes':len(data),'body_bytes':len(body),'blocks16k':len(body)/16384,
          'header_valid':bool(re.search(rb'INDEX\s*:\s*'+f'{number:010d}'.encode(),header)),
          'rows_completed':0,'first':None,'last':None,'incoming_partial_bytes':len(pending),
          'outgoing_partial_bytes':0,'seq_missing':0,'tick_error_count':0}
    lines=body.splitlines(keepends=True)
    had_pending=bool(pending)
    if pending:lines[0]=pending+lines[0]
    for i,line in enumerate(lines,1):
        loc=origin if i==1 and had_pending else (name,i)
        if not line.endswith(b'\n'):
            pending=line;origin=loc;continue
        m=pattern.fullmatch(line)
        if i==1 and had_pending:
            boundaries.append({'from':origin[0],'to':name,'valid':m is not None,
                               'tick':int(m[3]) if m else None,'seq':int(m[4]) if m else None})
        if not m:
            bad_count+=1
            if len(errors)<30:errors.append({'location':loc,'bytes':len(line),'prefix':line[:120].decode('ascii',errors='replace')})
            continue
        stamp,tag,tick,seq,ch,words=m.groups();stamp=stamp.decode();tag=tag.decode();tick=int(tick);seq=int(seq);ch=int(ch)
        row={'file':loc[0],'body_line':loc[1],'stamp':stamp,'tag':tag,'tick':tick,'seq':seq,'ch':ch}
        if stamp!=stamp_cache:
            stamp_cache=stamp;clock_cache=datetime.strptime('2026'+stamp,'%Y%m%d-%H:%M:%S')
        if first is None:first=row;first_clock=clock_cache
        if ch>=10 or tag!=tags[ch] or ch!=seq%10:
            channel_error_count+=1
            if len(channel_errors)<30:channel_errors.append(row)
        if previous:
            sd=seq-previous['seq'];td=tick-previous['tick'];seconds=(clock_cache-previous_clock).total_seconds()
            if sd!=1:
                miss=max(sd-1,0);missing+=miss;info['seq_missing']+=miss
                seq_gaps.append({'before':previous,'after':row,'delta':sd,'missing':miss,'tick_delta_ms':td,'rtc_delta_seconds':seconds})
                if sd<0:resets+=1
                if sd==0:duplicate_seq+=1
            if td!=10:
                tick_error_count+=1;info['tick_error_count']+=1
                if len(tick_errors)<100:tick_errors.append({'before':previous,'after':row,'delta_ms':td})
            if seconds<0 or seconds>1:rtc_events.append({'before':previous,'after':row,'seconds':seconds})
        if ch in last_channel and tick-last_channel[ch]['tick']!=100:
            channel_period_error_count+=1
            if len(channel_period_errors)<30:channel_period_errors.append({'before':last_channel[ch],'after':row})
        last_channel[ch]=row
        if tag in counts:counts[tag]+=1
        if tag=='task':
            fields=[int(w,16) for w in words.split(b'/')];task_sample_count+=1
            if task_first is None:task_first={'row':row,'fields':fields}
            task_last={'row':row,'fields':fields}
            for key,index in diag_indices.items():
                value=fields[index];diag_min[key]=min(diag_min.get(key,value),value);diag_max[key]=max(diag_max.get(key,value),value)
            for j in range(9):
                if fields[j]>0:stack_min[j]=min(stack_min[j] if stack_min[j] is not None else fields[j],fields[j])
        drift=(clock_cache-first_clock).total_seconds()*1000-(tick-first['tick'])
        drift_min=min(drift_min,drift);drift_max=max(drift_max,drift)
        length=len(line)-14;raw_bytes+=length;complete_bytes+=len(line);raw_min=min(raw_min,length);raw_max=max(raw_max,length)
        rows+=1;info['rows_completed']+=1
        if info['first'] is None:info['first']=row
        info['last']=row;previous=row;previous_clock=clock_cache
    if body.endswith(b'\n'):pending=b'';origin=None
    info['outgoing_partial_bytes']=len(pending);files.append(info)
    if True:print(f'Progress: {number-163}/4 files, {rows} complete records',flush=True)

span=(previous['tick']-first['tick'])/1000
expected=previous['seq']-first['seq']+1 if resets==0 else rows+missing
diagnostics={'first_sample':task_first['row'],'last_sample':task_last['row'],'sample_count':task_sample_count,
             'min':diag_min,'max':diag_max,
             'last':{key:task_last['fields'][index] for key,index in diag_indices.items()},
             'task_stack_min_nonzero_words':dict(zip(['comm','mode','status','chassis','gimbal','shoot','ui','debug','board'],stack_min))}
result={'file_count':len(files),'total_file_bytes':sum(f['bytes'] for f in files),'total_body_bytes':sum(f['body_bytes'] for f in files),
        'files':files,'first':first,'last':previous,'tick_span_seconds':span,'complete_rows':rows,'expected_records':expected,
        'missing_records':missing,'missing_rate_pct':missing/expected*100,'complete_rate_pct':rows/expected*100,
        'seq_gaps':seq_gaps,'seq_reset_count':resets,'duplicate_seq_count':duplicate_seq,
        'tick_error_count':tick_error_count,'tick_errors_first100':tick_errors,
        'channel_error_count':channel_error_count,'channel_errors_first30':channel_errors,
        'channel_period_error_count':channel_period_error_count,'channel_period_errors_first30':channel_period_errors,
        'rtc_events':rtc_events,'rtc_minus_tick_drift_ms_range':[drift_min,drift_max],
        'malformed_complete_lines':bad_count,'malformed_samples':errors,'boundaries':boundaries,
        'channel_counts':counts,'raw_line_bytes_min':raw_min,'raw_line_bytes_max':raw_max,'raw_line_bytes_mean':raw_bytes/rows,
        'raw_bytes_per_second':raw_bytes/(expected*.01),'timestamped_bytes_per_second':complete_bytes/(expected*.01),
        'final_partial_bytes':len(pending),'final_partial_text':pending.decode('ascii',errors='replace'),'task_diagnostics':diagnostics}
(root/'rb164_167_analysis.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
report=['# Rb164–Rb167 长期压力测试审核','',
        f'完整记录 {rows:,} 条，应有 {expected:,} 条，缺失 {missing} 条；记录缺失率 {missing/expected*100:.6f}%，完整记录率 {rows/expected*100:.6f}%。',
        f'源 tick 跨度 {span:.2f} 秒，RTC 从 {first["stamp"]} 到 {previous["stamp"]}，seq={first["seq"]}～{previous["seq"]}。',
        f'tick 周期异常 {tick_error_count}，seq 间断 {len(seq_gaps)}，seq 回退 {resets}，重复 {duplicate_seq}；通道映射异常 {channel_error_count}，100ms 通道周期异常 {channel_period_error_count}，RTC 异常 {len(rtc_events)}，完整行格式异常 {bad_count}。',
        f'拼接成功边界 {sum(b["valid"] for b in boundaries)}/{len(boundaries)}。总文件字节 {result["total_file_bytes"]:,}，正文 {result["total_body_bytes"]:,} 字节。',
        f'平均原始行 {raw_bytes/rows:.2f} 字节，原始数据量 {result["raw_bytes_per_second"]/1000:.2f}KB/s，带 RTC 时间戳落盘数据量 {result["timestamped_bytes_per_second"]/1000:.2f}KB/s（十进制 KB，按生成槽位计算）。',
        f'最终残行 {len(pending)} 字节，单独统计，不计入首末完整 seq 区间缺失率。','',
        '## 诊断数据范围','', '| 指标 | 最小 | 最大 | 最后采样 |','|---|---:|---:|---:|']
for key in diag_indices:report.append(f'| {key} | {diag_min[key]} | {diag_max[key]} | {diagnostics["last"][key]} |')
report+=['','## 各通道记录数','', '| 通道 | 完整行 |','|---|---:|']
for tag,count in counts.items():report.append(f'| {tag} | {count:,} |')
report+=['','## 文件统计','', '| 文件 | 正文字节 | 完整行 | 缺失记录 | tick 周期异常 | 首 seq | 末 seq |','|---|---:|---:|---:|---:|---:|---:|']
for f in files:report.append(f'| {f["file"]} | {f["body_bytes"]} | {f["rows_completed"]} | {f["seq_missing"]} | {f["tick_error_count"]} | {f["first"]["seq"]} | {f["last"]["seq"]} |')
report+=['','## 异常与范围限制','', '```json',json.dumps({'seq_gaps':seq_gaps,'tick_errors_first100':tick_errors,'rtc_events':rtc_events,'stack_min_nonzero_words':diagnostics['task_stack_min_nonzero_words']},ensure_ascii=False,indent=2),'```','',
         '业务内容未审核。诊断值是 task 通道每 100ms 携带的采样，未覆盖结束后状态。记录缺失率并非 USB 底层丢包率；没有直接采集 F405 写卡错误/耗时 Watch，不能仅凭文件声称所有底层写卡操作无错误。','',
         '## 文件末尾','', '```text',pending.decode('ascii',errors='replace'),'```']
(root/'Rb164_Rb167_review.md').write_text('\n'.join(report)+'\n',encoding='utf-8')
for key in ('files','seq_gaps','boundaries','tick_errors_first100','channel_errors_first30','channel_period_errors_first30'):result.pop(key)
print(json.dumps(result,ensure_ascii=False,indent=2))
