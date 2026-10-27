"""只读分析接收日志的字节分布，不执行文件中的任何内容。"""
from pathlib import Path
from collections import Counter, deque
import json
import re
import sys

root = Path(__file__).resolve().parent
stem = sys.argv[1] if len(sys.argv) > 1 else "Rb24"
data = (root / f"{stem}_snapshot.bin").read_bytes()
header_end = data.find(b"\r\n\r\n") + 4
body = data[header_end:]
counts = Counter(body)
first_nonzero, last_nonzero = [], deque(maxlen=32)
for i, b in enumerate(body):
    if b:
        item = {"offset": i + header_end, "hex": f"{b:02X}"}
        if len(first_nonzero) < 32:
            first_nonzero.append(item)
        last_nonzero.append(item)
runs = []
for match in re.finditer(rb"[\x20-\x7e]{6,}", body):
    runs.append({"offset": match.start() + header_end,
                 "text": match.group()[:180].decode("ascii")})
report = {
    "file_bytes": len(data), "body_offset": header_end,
    "body_bytes": len(body), "zero_bytes": counts[0],
    "zero_percent": round(counts[0] / max(len(body), 1) * 100, 6),
    "ff_percent": round(counts[255] / max(len(body), 1) * 100, 6),
    "lf_count": counts[10], "cr_count": counts[13],
    "top_bytes": [{"hex": f"{b:02X}", "count": n} for b, n in counts.most_common(16)],
    "nonzero_count": len(body) - counts[0],
    "first_nonzero": first_nonzero,
    "last_nonzero": list(last_nonzero),
    "ascii_runs_count": len(runs), "ascii_runs_sample": runs[:15],
    "log_tags": {tag.decode(): body.count(tag) for tag in (b"INFO:", b"WARN:", b"ERROR:", b"tick=", b"seq=")},
}
(root / f"{stem}_analysis.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
print(json.dumps({key: report[key] for key in (
    "file_bytes", "body_offset", "body_bytes", "zero_bytes", "zero_percent", "ff_percent", "lf_count",
    "cr_count", "top_bytes", "ascii_runs_count", "ascii_runs_sample", "log_tags")}, ensure_ascii=False, indent=2))
