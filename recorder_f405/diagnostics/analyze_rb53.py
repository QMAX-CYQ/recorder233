from pathlib import Path
from datetime import datetime
import re,json

root=Path(__file__).resolve().parent
source=Path('D:/RM/rm_main_gimbal/User/Task/debug_task.c').read_bytes().decode('latin1')
patterns={}
for fmt in re.findall(r'DEBUG_INFO\("(\[[^"\r\n]+)\\r\\n"',source):
    tag=re.match(r'\[([^]]+)\]',fmt)[1]
    pat=re.escape(fmt)
    for spec,value in [('%08lX',r'[0-9A-F]{8}'),('%lu',r'\d+'),('%ld',r'-?\d+'),('%u',r'\d+'),('%d',r'-?\d+')]:
        pat=pat.replace(re.escape(spec),value)
    patterns[tag]=re.compile(r'^\d{4}-\d{2}:\d{2}:\d{2} INFO: '+pat+'$')
data=Path('E:/Rb53.txt').read_bytes()
prefix=re.compile(r'^(\d{4}-\d{2}:\d{2}:\d{2}) INFO: \[([^]]+)\] (?:tick|t)=(\d+) ')
rows=[];bad=[];gaps=[];resets=[];clock_events=[];segments=[]
for number,line in enumerate(data[352:].decode('ascii').splitlines(keepends=True),9):
    m=prefix.match(line)
    if not line.endswith('\r\n') or not m or not patterns[m[2]].fullmatch(line[:-2]):
        bad.append({'line':number,'text':line.rstrip('\r\n'),'ends_crlf':line.endswith('\r\n')})
    if not m or not line.endswith('\r\n'):continue
    row={'line':number,'stamp':m[1],'tag':m[2],'tick':int(m[3])}
    clock=datetime.strptime('2026'+m[1],'%Y%m%d-%H:%M:%S')
    if rows:
        old=rows[-1];delta=row['tick']-old['tick'];seconds=(clock-old_clock).total_seconds()
        if seconds<0 or seconds>1:
            clock_events.append({'before':old,'after':row,'rtc_seconds':seconds})
        if delta<0:
            resets.append({'before':old,'after':row,'rtc_seconds':seconds})
            segments.append(segment)
            segment={'first':row,'last':row,'rows':0,'missing':0,'gaps':0}
        elif delta!=10:
            missing=delta//10-1 if delta>10 and delta%10==0 else None
            gaps.append({'line':number,'stamp':row['stamp'],'before':old['tick'],'after':row['tick'],
                         'delta':delta,'missing':missing,'rtc_seconds':seconds})
            segment['gaps']+=1
            if missing is not None:segment['missing']+=missing
    else:segment={'first':row,'last':row,'rows':0,'missing':0,'gaps':0}
    segment['rows']+=1;segment['last']=row
    rows.append(row);old_clock=clock
segments.append(segment)
result={'bytes':len(data),'body_bytes':len(data)-352,'blocks16k':(len(data)-352)/16384,
        'complete_tick_rows':len(rows),'segments':segments,'reset_boundaries':resets,
        'rtc_events':clock_events,'frame_anomalies':bad,'gaps':gaps,
        'total_missing_inside_segments':sum(s['missing'] for s in segments)}
(root/'rb53_analysis.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
report=['# Rb53 日志连续性检查\n',f"完整 tick 记录 {len(rows)} 条，发现 {len(resets)} 处 tick 回退（复位/新会话边界），各分段内部推算缺失 {result['total_missing_inside_segments']} 条。",
        '\n| 分段 | 起始 tick | 结束 tick | 完整记录 | 缺口数 | 推算缺失 |','|---|---:|---:|---:|---:|---:|']
for i,s in enumerate(segments,1):report.append(f"| {i} | {s['first']['tick']} | {s['last']['tick']} | {s['rows']} | {s['gaps']} | {s['missing']} |")
report+=['\n| 文件行号 | RTC | 前 tick | 后 tick | 间隔 ms | 推算缺失 |','|---:|---|---:|---:|---:|---:|']
for g in gaps:report.append(f"| {g['line']} | {g['stamp']} | {g['before']} | {g['after']} | {g['delta']} | {g['missing']} |")
report+=['\n复位/会话边界：','```json',json.dumps(resets,ensure_ascii=False,indent=2),'```','\n帧格式异常：','```json',json.dumps(bad,ensure_ascii=False,indent=2),'```']
(root/'Rb53_review.md').write_text('\n'.join(report)+'\n',encoding='utf8')
print(json.dumps(result,ensure_ascii=False,indent=2))
