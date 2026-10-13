from pathlib import Path
import json

root = Path(__file__).resolve().parent
data = json.loads((root/'rb62_73_analysis.json').read_text(encoding='utf-8'))
actual = data['complete_rows']
missing = data['missing']
expected = (data['last']['tick']-data['first']['tick'])//10+1
assert actual+missing == expected
last_gap_tick = data['gaps'][-1]['after']['tick']
continuous_seconds = (data['last']['tick']-last_gap_tick)/1000
continuous_rows = (data['last']['tick']-last_gap_tick)//10+1
first_file_actual = data['files'][0]['rows_completed']
metrics = {
    'expected_records': expected,
    'record_missing_rate_pct': missing/expected*100,
    'complete_record_rate_pct': actual/expected*100,
    'missing_per_10000_expected': missing/expected*10000,
    'first_file_missing_rate_pct': missing/(first_file_actual+missing)*100,
    'continuous_seconds_after_last_gap': continuous_seconds,
    'continuous_span_pct': continuous_seconds/data['tick_span_seconds']*100,
    'continuous_records_after_last_gap': continuous_rows,
    'first_gap_share_of_missing_pct': data['gaps'][0]['missing']/missing*100,
    'second_gap_share_of_missing_pct': data['gaps'][1]['missing']/missing*100,
}
data['percentage_metrics'] = metrics
(root/'rb62_73_analysis.json').write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf-8')
section = f'''## 百分比与直观解读

以下“丢包率”指 **日志记录缺失率**：按相邻源 tick 的 10ms 周期推算应有记录，再与完整落盘记录比较。它不是 USB 数据包层面的丢包率，也不能单独区分发送拒收、任务暂停或接收丢失。

| 指标 | 结果 | 计算口径 |
|---|---:|---|
| 应有日志记录 | {expected:,} 条 | 首末完整 tick 的闭区间，间隔 10ms |
| 完整落盘记录 | {actual:,} 条 | 跨文件拼接后的完整行 |
| 推算缺失记录 | {missing:,} 条 | 两处 tick 缺口合计 |
| 整段日志记录缺失率（丢包率） | **{metrics['record_missing_rate_pct']:.4f}%** | {missing:,} ÷ {expected:,} |
| 整段完整记录率 | **{metrics['complete_record_rate_pct']:.4f}%** | {actual:,} ÷ {expected:,} |
| 每 10000 条应有记录的平均缺失 | **{metrics['missing_per_10000_expected']:.2f} 条** | 缺失率 × 10000 |
| Rb62 文件内记录缺失率 | {metrics['first_file_missing_rate_pct']:.4f}% | {missing:,} ÷ ({first_file_actual:,} + {missing:,}) |
| 最后缺口之后记录缺失率 | **0.0000%** | 连续 {continuous_rows:,} 条完整记录，无 tick 缺口 |
| 最后缺口之后连续时间占比 | **{metrics['continuous_span_pct']:.4f}%** | {continuous_seconds:.2f} 秒 ÷ {data['tick_span_seconds']:.2f} 秒；均使用源 tick |
| 完整行结构异常率 | 0.0000% | 0 ÷ {actual:,}；不包含最终残行 |
| 跨文件边界验证通过率 | 100% | 11 ÷ 11，均可拼接为完整记录 |

直观来看：**每 10000 条应有记录，平均缺少约 {metrics['missing_per_10000_expected']:.1f} 条，完整保留约 {10000-metrics['missing_per_10000_expected']:.1f} 条。** 缺失全部集中在 Rb62 前段，随后约 2 小时 1 分 6 秒连续正常，占本次源 tick 时间跨度的 {metrics['continuous_span_pct']:.2f}%。缺失不是均匀分布在整段运行中。

| 缺口 | 缺失条数 | 占全部缺失比例 | 占整段应有记录比例 |
|---|---:|---:|---:|
| tick 1240 → 16040 | 1479 | {metrics['first_gap_share_of_missing_pct']:.2f}% | {1479/expected*100:.4f}% |
| tick 63930 → 64530 | 59 | {metrics['second_gap_share_of_missing_pct']:.2f}% | {59/expected*100:.4f}% |
| 合计 | 1538 | 100% | {metrics['record_missing_rate_pct']:.4f}% |

范围限制：以上缺失率仅覆盖 tick=1170～7330340，不包含首条记录之前的数据。最后 t=7330350 的 108 字节残行单独列为文件尾截断，不计入上述缺失率；缺口前后 RTC 在同一秒内，源 tick 间隔不能直接当作实际 USB 停传时长。

'''
report_path = root/'Rb62_Rb73_review.md'
report = report_path.read_text(encoding='utf-8')
marker = '| 文件 | 正文字节 |'
position = report.index(marker)
report = report[:position]+section+report[position:]
report_path.write_text(report,encoding='utf-8')
print(json.dumps(metrics,ensure_ascii=False,indent=2))
