"""移除接收端时间戳，仅分析原始串行数据的位偏移，不改写原日志。"""
from pathlib import Path
import sys
import json
import re
import numpy as np

root = Path(__file__).resolve().parent
stem = sys.argv[1] if len(sys.argv) > 1 else "Rb32"
data = (root / f"{stem}_snapshot.bin").read_bytes()
body = data[data.find(b"\r\n\r\n") + 4:]
raw, prefixes = re.subn(rb"\d{4}-\d{2}:\d{2}:\d{2} ", b"", body)
arr = np.frombuffer(raw, dtype=np.uint8).astype(np.uint16)
results = []
for shift in range(8):
    out = raw if shift == 0 else (((arr[:-1] << shift) | (arr[1:] >> (8 - shift))) & 255).astype(np.uint8).tobytes()
    headers = list(re.finditer(rb"INFO: \[([a-zA-Z0-9_]+)\] tick=(\d+) seq=(\d+) ch=(\d+) n=48 w=", out))
    results.append({
        "shift": shift,
        "tags": {tag.decode(): out.count(tag) for tag in (b"INFO:", b"WARN:", b"tick=", b"seq=")},
        "valid_headers": len(headers),
        "header_samples": [{"offset": m.start(), "text": m.group().decode()} for m in headers[:4]],
        "first_bytes": out[:96].hex(" "),
        "first_text": repr(out[:96]),
    })
report = {"file": stem, "timestamp_prefixes_removed": prefixes, "raw_bytes": len(raw), "shifts": results}
(root / f"{stem}_bit_alignment.json").write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
print(json.dumps(report, indent=2, ensure_ascii=False))
