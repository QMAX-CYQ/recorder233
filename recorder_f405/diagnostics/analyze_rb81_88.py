from pathlib import Path
from datetime import datetime
import re, json

root = Path(__file__).resolve().parent
prefix = re.compile(rb'^(\d{4}-\d{2}:\d{2}:\d{2}) INFO: \[([^]]+)\] (?:tick|t)=(\d+) ')
source = Path('D:/RM/rm_main_gimbal/User/Task/debug_task.c').read_bytes().decode('latin1')
patterns = {}
for fmt in re.findall(r'DEBUG_INFO\("(\[[^"\r\n]+)\\r\\n"', source):
    tag = re.match(r'\[([^]]+)\]', fmt)[1]
    pat = re.escape(fmt)
    for spec, value in [('%08lX', r'[0-9A-F]{8}'), ('%lu', r'\d+'), ('%ld', r'-?\d+'), ('%u', r'\d+'), ('%d', r'-?\d+')]:
        pat = pat.replace(re.escape(spec), value)
    patterns[tag] = re.compile(rb'^\d{4}-\d{2}:\d{2}:\d{2} INFO: '+pat.encode('ascii')+rb'$')

files=[]; gaps=[]; resets=[]; duplicates=[]; rtc_events=[]; bad=[]; shape=[]; phase_errors=[]
boundaries=[]; pending=b''; origin=None; first=None; previous=None; rows=0; missing=0
clock_stamp=None; clock_value=None; previous_clock=None; min_drift=0; max_drift=0; phase=None
groups=['sys','task','fdcan','uart123','uart610','nvic','motor','motor']

def consume(line, location, info):
    global first, previous, rows, missing, clock_stamp, clock_value, previous_clock, min_drift, max_drift, phase
    m=prefix.match(line)
    if not m or not line.endswith(b'\r\n'):
        bad.append({'location':location,'text':line[:220].decode('ascii',errors='replace'),'bytes':len(line)})
        return
    row={'file':location[0],'body_line':location[1],'stamp':m[1].decode(),'tag':m[2].decode(),'tick':int(m[3])}
    if row['tag'] not in patterns or not patterns[row['tag']].fullmatch(line[:-2]):
        shape.append({'row':row,'text':line[:240].decode('ascii',errors='replace')})
    if clock_stamp != row['stamp']:
        clock_stamp=row['stamp']; clock_value=datetime.strptime('2026'+clock_stamp,'%Y%m%d-%H:%M:%S')
    if first is None:
        first=row; first_clock=clock_value
        consume.first_clock=first_clock
        phase=(groups.index(row['tag'])-row['tick']//10)%8 if row['tag'] in groups else 0
    if row['tag'] != groups[(row['tick']//10+phase)%8]:
        phase_errors.append(row)
    if previous:
        delta=row['tick']-previous['tick']; seconds=(clock_value-previous_clock).total_seconds()
        if delta != 10:
            count=delta//10-1 if delta>10 and delta%10==0 else None
            event={'before':previous,'after':row,'delta_ms':delta,'missing':count,'rtc_seconds':seconds}
            gaps.append(event)
            if count is not None: missing+=count
            if delta<0: resets.append(event)
            if delta==0: duplicates.append(event)
        if seconds<0 or seconds>1:
            rtc_events.append({'before':previous,'after':row,'seconds':seconds})
    drift=(clock_value-consume.first_clock).total_seconds()*1000-(row['tick']-first['tick'])
    min_drift=min(min_drift,drift); max_drift=max(max_drift,drift)
    info['rows_completed']+=1
    if info['first'] is None: info['first']=row
    info['last']=row
    rows+=1; previous=row; previous_clock=clock_value

for index in range(81,89):
    name=f'Rb{index}.txt'; data=Path('E:/'+name).read_bytes(); header=data[:352]; body=data[352:]
    info={'file':name,'bytes':len(data),'body_bytes':len(body),'blocks16k':len(body)/16384,
          'header_valid':header.startswith(b'+---') and f'{index:010d}'.encode() in header,
          'rows_completed':0,'first':None,'last':None,'leading_continuation_bytes':len(pending),
          'trailing_partial_bytes':0,'nul_bytes':body.count(b'\x00'),
          'non_ascii_bytes':sum(c>127 for c in body)}
    lines=body.splitlines(keepends=True)
    if pending:
        lines[0]=pending+lines[0]
    for i,line in enumerate(lines,1):
        location=origin if i==1 and pending else (name,i)
        if line.endswith(b'\n'):
            consume(line,location,info)
            if i==1 and pending:
                boundaries.append({'from':origin[0],'to':name,'valid_stitched_frame':bool(prefix.match(line)) and line.endswith(b'\r\n'),'tick':int(prefix.match(line)[3]) if prefix.match(line) else None})
        else:
            pending=line; origin=location
    if body.endswith(b'\n'):
        pending=b''; origin=None
    info['trailing_partial_bytes']=len(pending)
    files.append(info)
result={'files':files,'complete_rows':rows,'first':first,'last':previous,'missing':missing,
        'gap_count':len(gaps),'gaps':gaps,'resets':resets,'duplicates':duplicates,
        'rtc_events':rtc_events,'malformed_frames_count':len(bad),'malformed_frames':bad[:30],
        'shape_errors_count':len(shape),'shape_errors':shape[:30],
        'phase_errors_count':len(phase_errors),'phase_errors':phase_errors[:30],
        'boundaries':boundaries,'final_partial_bytes':len(pending),
        'final_partial_text':pending.decode('ascii',errors='replace'),
        'rtc_minus_tick_drift_ms_range':[min_drift,max_drift],
        'tick_span_seconds':(previous['tick']-first['tick'])/1000,
        'total_body_bytes':sum(f['body_bytes'] for f in files)}
(root/'rb81_88_analysis.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
report=['# Rb81–Rb88 长时间日志连续性检查','',
        f'完整记录 {rows} 条，tick 跨度 {result["tick_span_seconds"]} 秒；间断 {len(gaps)} 处，推算缺失 {missing} 条。',
        f'起止：{first["stamp"]} / tick={first["tick"]} → {previous["stamp"]} / tick={previous["tick"]}。',
        f'RTC 异常 {len(rtc_events)}，tick 回退 {len(resets)}，重复 {len(duplicates)}；完整帧结构异常 {len(bad)}，源格式不匹配 {len(shape)}，组轮转异常 {len(phase_errors)}。',
        f'末尾残行 {len(pending)} 字节。RTC 与 tick 相对漂移范围 {min_drift}～{max_drift} ms（RTC 只有秒级精度）。','',
        '| 文件 | 正文字节 | 16KB 块数 | 完整行（按完成所在文件） | 首 tick | 末 tick | 尾部残行字节 |',
        '|---|---:|---:|---:|---:|---:|---:|']
for f in files:
    report.append(f'| {f["file"]} | {f["body_bytes"]} | {f["blocks16k"]} | {f["rows_completed"]} | {f["first"]["tick"]} | {f["last"]["tick"]} | {f["trailing_partial_bytes"]} |')
report+=['','## tick 间断','', '| 前 tick | 后 tick | 间隔 ms | 缺失 | RTC |','|---:|---:|---:|---:|---|']
for g in gaps:report.append(f'| {g["before"]["tick"]} | {g["after"]["tick"]} | {g["delta_ms"]} | {g["missing"]} | {g["after"]["stamp"]} |')
report+=['','## 边界与末尾','',json.dumps(boundaries,ensure_ascii=False,indent=2),'',pending.decode('ascii',errors='replace')]
(root/'Rb81_Rb88_review.md').write_text('\n'.join(report)+'\n',encoding='utf8')
result.pop('gaps'); result.pop('shape_errors'); result.pop('phase_errors'); result.pop('malformed_frames')
print(json.dumps(result,ensure_ascii=False,indent=2))
