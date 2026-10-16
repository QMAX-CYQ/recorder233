"""校验 Keil Intel HEX 并按本版实际结构解码首次停止失败现场。"""
from pathlib import Path
import json
import re
import struct
import sys

here = Path(__file__).resolve().parent
root = here.parents[1]
capture = Path(sys.argv[1]) if len(sys.argv) > 1 else here / 'capture_first_stop.hex'
start = int(sys.argv[2], 0) if len(sys.argv) > 2 else 0x2001C6A8
memory = {}
base = 0
eof = False
for lineno, line in enumerate(capture.read_text(encoding='ascii').splitlines(), 1):
    if not line.strip():
        continue
    assert not eof and line.startswith(':'), (lineno, 'invalid/trailing record')
    record = bytes.fromhex(line[1:])
    assert len(record) == record[0] + 5 and sum(record) % 256 == 0, (lineno, 'checksum/length')
    address = int.from_bytes(record[1:3], 'big')
    kind, payload = record[3], record[4:-1]
    if kind == 0:
        for n, byte in enumerate(payload):
            location = base + address + n
            assert location not in memory or memory[location] == byte, (lineno, 'overlap')
            memory[location] = byte
    elif kind == 1:
        eof = True
    elif kind == 2:
        base = int.from_bytes(payload, 'big') << 4
    elif kind == 4:
        base = int.from_bytes(payload, 'big') << 16
    else:
        assert kind in (3, 5), (lineno, 'unsupported record')
assert eof

header = (root / 'User/Task/usb_rx_trace.h').read_text(encoding='utf-8')
header = re.sub(r'/\*.*?\*/', '', header, flags=re.S)
stage_count = 7

def decode_type(name, offset):
    body = re.search(r'typedef struct\s*\{([^{}]*)\}\s*' + name + r'\s*;', header).group(1)
    result = {}
    for declaration in body.split(';'):
        declaration = declaration.strip()
        if not declaration:
            continue
        type_name, fields = declaration.split(None, 1)
        for field in fields.split(','):
            match = re.fullmatch(r'\s*(\w+)(?:\[(\w+)\])?\s*', field)
            field_name, count_text = match.groups()
            count = (stage_count if count_text == 'USB_STOP_STAGE_COUNT' else int(count_text)) if count_text else 1
            values = []
            for _ in range(count):
                if type_name == 'uint32_t':
                    values.append(struct.unpack('<I', bytes(memory[start + offset + n] for n in range(4)))[0])
                    offset += 4
                else:
                    item, offset = decode_type(type_name, offset)
                    values.append(item)
            result[field_name] = values if count_text else values[0]
    return result, offset

data, size = decode_type('usb_stop_first_failure_t', 0)
assert size == 1320 and len(memory) == size and min(memory) == start and max(memory) == start + size - 1
clock = data['core_clock_hz']
computed = {'start_address': f'0x{start:08X}', 'bytes': size, 'checksum_valid': True,
            'channels': [], 'stages': []}
for n in range(16):
    computed['channels'].append({
        'channel': n, 'configured': n < 12,
        'start_count': data['wait_start_count'][n], 'end_count': data['wait_end_count'][n],
        'iterations': data['wait_iterations'][n], 'cycles': data['wait_elapsed_cycles'][n],
        'cycle_valid': bool(data['wait_cycle_valid_mask'] & (1 << n)),
        'us': data['wait_elapsed_cycles'][n] * 1e6 / clock if clock else None,
        'enabled_at_exit': bool(data['enabled_mask'] & (1 << n)),
        'exhausted': bool(data['exhausted_mask'] & (1 << n)),
    })
names = ['ENTER', 'FLUSH_BEFORE', 'FLUSH_AFTER', 'HALT_BEFORE', 'HALT_AFTER', 'WAIT_EXIT', 'EXIT']
for n, s in enumerate(data['stages']):
    computed['stages'].append({
        'stage': n, 'name': names[n], 'tick': s['tick'], 'cycles': s['cycles'],
        'since_enter_us': ((s['cycles'] - data['stages'][0]['cycles']) & 0xFFFFFFFF) * 1e6 / clock,
        'hcchar': f"0x{s['hcchar']:08X}", 'hcint': f"0x{s['hcint']:08X}",
        'chena': (s['hcchar'] >> 31) & 1, 'chdis': (s['hcchar'] >> 30) & 1,
        'direction_in': (s['hcchar'] >> 15) & 1,
        'hprt': f"0x{s['hprt']:08X}", 'connected': s['hprt'] & 1,
        'port_enabled': (s['hprt'] >> 2) & 1,
        'grstctl': f"0x{s['grstctl']:08X}", 'ahb_idle': (s['grstctl'] >> 31) & 1,
        'gahbcfg': f"0x{s['gahbcfg']:08X}", 'gintsts': f"0x{s['gintsts']:08X}",
        'hfnum': f"0x{s['hfnum']:08X}", 'frame_number': s['hfnum'] & 0xFFFF,
        'hnptxsts': f"0x{s['hnptxsts']:08X}",
        'hc_state': s['hc_state'], 'urb_state': s['urb_state'], 'error_count': s['error_count'],
    })
out = here / (capture.stem + '_analysis.json')
out.write_text(json.dumps({'capture': data, 'computed': computed}, ensure_ascii=False, indent=2), encoding='utf-8')
print('HEX: checksum/coverage valid, bytes=', size)
print('Metadata:', json.dumps({k: v for k, v in data.items() if not isinstance(v, list)}, ensure_ascii=False))
print('Channels:', json.dumps(computed['channels'], ensure_ascii=False, indent=2))
print('Stages:', json.dumps(computed['stages'], ensure_ascii=False, indent=2))
print('Saved:', out)
