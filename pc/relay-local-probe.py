"""Read local Tuya status and request a measurement refresh; never switch relay."""
import base64
from pathlib import Path
import re
import time
import tinytuya

secrets = Path(r'C:\esp32\cydv4\secrets.h').read_text(encoding='utf-8-sig')
def setting(name):
    m = re.search(r'^\s*#define\s+' + re.escape(name) + r'\s+"([^"\r\n]+)"', secrets, re.M)
    if not m:
        raise ValueError('Missing required setting: ' + name)
    return m[1]

def report(stage, result):
    if not isinstance(result, dict):
        print(stage, 'no status response', flush=True)
        return
    dps = result.get('dps', {})
    print(stage, 'DP keys:', ','.join(sorted(dps)), 'error:', result.get('Err'), flush=True)
    for key in ['1', '16', '101']:
        if key in dps:
            print(' DP', key, '=', dps[key], flush=True)
    if '6' in dps:
        try:
            b = base64.b64decode(dps['6'], validate=True)
            print(' DP6 length:', len(b), 'voltage:', int.from_bytes(b[0:2], 'big') / 10,
                  'power:', int.from_bytes(b[6:8], 'big'), flush=True)
        except (ValueError, TypeError):
            print(' DP6 has unexpected encoding', flush=True)

d = tinytuya.Device(setting('TUYA_DEVICE_ID'), setting('RELAY_IP'),
                   setting('TUYA_LOCAL_KEY'), version=3.5, persist=True,
                   connection_timeout=3, connection_retry_limit=1)
try:
    report('status', d.status())
    for sample in range(3):
        result = d.updatedps([6])
        report('refresh DP6 #' + str(sample + 1), result)
        for i in range(5):
            if isinstance(result, dict) and '6' in result.get('dps', {}):
                break
            result = d.receive()
            report('receive #' + str(sample + 1), result)
        if sample < 2:
            time.sleep(6)
    report('status after refresh', d.status())
finally:
    d.close()
