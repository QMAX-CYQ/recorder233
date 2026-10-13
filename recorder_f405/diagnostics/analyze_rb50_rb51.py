from pathlib import Path
import re, json
from collections import Counter
from datetime import datetime

out = Path(__file__).resolve().parent
source = Path('D:/RM/rm_main_gimbal/User/Task/debug_task.c').read_bytes().decode('latin1')
patterns = {}
for fmt in re.findall(r'DEBUG_INFO\("(\[[^"\r\n]+)\\r\\n"', source):
    tag = re.match(r'\[([^]]+)\]', fmt)[1]
    pat = re.escape(fmt)
    for spec, replacement in [('%08lX', r'[0-9A-F]{8}'), ('%lu', r'\d+'),
                              ('%ld', r'-?\d+'), ('%u', r'\d+'), ('%d', r'-?\d+')]:
        pat = pat.replace(re.escape(spec), replacement)
    patterns[tag] = re.compile(r'^\d{4}-\d{2}:\d{2}:\d{2} INFO: ' + pat + '$')
prefix = re.compile(r'^(\d{4}-\d{2}:\d{2}:\d{2}) INFO: \[([^]]+)\] (?:tick|t)=(\d+) ')
files = []
parts = []
for name in ('Rb50.txt', 'Rb51.txt'):
    data = Path('E:/'+name).read_bytes()
    header, body = data[:352], data[352:]
    assert header.startswith(b'+---') and b'| MAGIC   : 0x52454344' in header
    text = body.decode('ascii')
    rows = []
    anomalies = []
    for number, line in enumerate(text.splitlines(keepends=True), 9):
        match = prefix.match(line)
        if match:
            rows.append({'line': number, 'stamp': match[1], 'tag': match[2], 'tick': int(match[3])})
        if not line.endswith('\r\n') or not match or not patterns[match[2]].fullmatch(line[:-2]):
            anomalies.append({'line': number, 'text': line.rstrip('\r\n'), 'ends_crlf':line.endswith('\r\n')})
    gaps = []
    for a,b in zip(rows,rows[1:]):
        delta=b['tick']-a['tick']
        if delta!=10:
            gaps.append({'file':name,'line':b['line'],'stamp':b['stamp'],'before':a['tick'],
                         'after':b['tick'],'delta':delta,'missing':delta//10-1 if delta>10 and delta%10==0 else None})
    files.append({'name':name,'bytes':len(data),'body_bytes':len(body),'prefix_rows':len(rows),
                  'first':rows[0],'last':rows[-1],'edge_or_shape_anomalies':anomalies,'gaps':gaps})
    parts.append(body)

joined = b''.join(parts).decode('ascii')
rows=[]
bad=[]
clock_bad=[]
phase_bad=[]
gaps=[]
previous=None
slots=['sys','task','fdcan','uart123','uart610','nvic','motor','motor']
for number,line in enumerate(joined.splitlines(keepends=True),1):
    match=prefix.match(line)
    if not line.endswith('\r\n') or not match or not patterns[match[2]].fullmatch(line[:-2]):
        bad.append({'joined_line':number,'text':line.rstrip('\r\n'),'ends_crlf':line.endswith('\r\n')})
    if not match:
        continue
    row={'joined_line':number,'stamp':match[1],'tag':match[2],'tick':int(match[3])}
    clock=datetime.strptime('2026'+match[1],'%Y%m%d-%H:%M:%S')
    if (row['tick']-1100)%10 or slots[(5+(row['tick']-1100)//10)%8]!=row['tag']:
        phase_bad.append(row)
    if previous:
        delta=row['tick']-previous['tick']
        seconds=(clock-previous_clock).total_seconds()
        if seconds<0 or seconds>1:
            clock_bad.append({'joined_line':number,'previous_stamp':previous['stamp'],'stamp':row['stamp'],'seconds':seconds})
        if delta!=10:
            gaps.append({'stamp':row['stamp'],'before':previous['tick'],'after':row['tick'],
                         'delta':delta,'missing':delta//10-1 if delta>10 and delta%10==0 else None})
    rows.append(row)
    previous=row
    previous_clock=clock
first=rows[0]['tick']; last=rows[-1]['tick']
expected=(last-first)//10+1
boundary_line=(parts[0].split(b'\r\n')[-1]+parts[1].split(b'\r\n')[0]).decode('ascii')
boundary_match=prefix.match(boundary_line)
boundary_ok=bool(boundary_match and patterns[boundary_match[2]].fullmatch(boundary_line))
known_ticks=[10120,301960,318300,325990,328610,346170,358880,412120,412130]
known_match={str(t):[g for g in gaps if g['before']<t<g['after']] for t in known_ticks}
summary={'files':files,'combined':{'complete_rows':sum(1 for line in joined.splitlines(keepends=True) if line.endswith('\r\n')),
    'parsed_tick_rows':len(rows),'first_tick':first,'last_tick':last,'span_seconds':(last-first)/1000,
    'expected_rows':expected,'missing_rows':expected-len(rows),'gap_events':len(gaps),
    'gap_histogram':dict(Counter(g['delta'] for g in gaps)),'frame_anomalies':bad,
    'clock_anomalies':clock_bad,'phase_anomalies':phase_bad,'gaps':gaps,
    'boundary_reconstructed_ok':boundary_ok,'boundary_line':boundary_line,'known_failure_matches':known_match}}
(out/'rb50_rb51_analysis.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2),encoding='utf8')
report=['# Rb50/Rb51 连续性检查\n',f'合并正文后完整记录 {len(rows)} 条，tick {first}～{last}，跨度 {(last-first)/1000:.2f} 秒。',
        f'范围内理论记录 {expected} 条，缺失 {expected-len(rows)} 条，共 {len(gaps)} 处 tick 缺口。',
        f'文件交界处重组完整：{boundary_ok}。时间戳异常 {len(clock_bad)} 处，日志组相位异常 {len(phase_bad)} 处。',
        '\n| 文件 | 行号（含8行文件头） | 时间戳 | 前 tick | 后 tick | 间隔 ms | 推算缺失 |',
        '|---|---:|---|---:|---:|---:|---:|']
for f in files:
    for g in f['gaps']:
        report.append(f"| {g['file']} | {g['line']} | {g['stamp']} | {g['before']} | {g['after']} | {g['delta']} | {g['missing']} |")
report += ['\n拼接后的格式异常（不分析业务字段值）：','```json',json.dumps(bad,ensure_ascii=False,indent=2),'```']
(out/'Rb50_Rb51_review.md').write_text('\n'.join(report)+'\n',encoding='utf8')
compact={'files':[{k:v for k,v in f.items() if k!='gaps'} for f in files],
         'combined':{k:v for k,v in summary['combined'].items() if k not in ('gaps','known_failure_matches')},
         'known_failure_matches':known_match,
         'gaps':gaps}
print(json.dumps(compact,ensure_ascii=False,indent=2))
