"""Validate archived Tuya measurements without printing private readings.

Pass a private outage JSON produced by the investigation as the first argument.
This checks the actual records used to size recovery windows; it does not test
firmware execution, SD writes, power cycling or the Tuya service itself.
"""
import base64
import collections
import json
import sys
from pathlib import Path

def check(events):
    assert events, "no cloud records"
    counts = collections.Counter()
    counters = []
    times = []
    for event in events:
        code, timestamp, value = event["code"], int(event["event_time"]), event["value"]
        assert timestamp > 1600000000000
        times.append(timestamp)
        if code == "phase_a":
            raw = base64.b64decode(value, validate=True)
            assert len(raw) >= 8
            voltage = int.from_bytes(raw[:2], "big")
            power = int.from_bytes(raw[6:8], "big")
            assert 800 <= voltage <= 4000 and 0 <= power <= 20000
        elif code == "temp_current":
            assert -400 <= float(value) <= 1000
        elif code == "total_forward_energy":
            counters.append((timestamp, int(value)))
        counts[code] += 1
    counters.sort()
    assert all(x[1] <= y[1] for x, y in zip(counters, counters[1:])), "counter regressed"
    # Count every actual 10-minute sliding window, not just aligned buckets.
    times.sort()
    start = 0
    largest = 0
    for end, value in enumerate(times):
        while value - times[start] > 600000:
            start += 1
        largest = max(largest, end - start + 1)
    return counts, largest

def main():
    phase = base64.b64encode(bytes.fromhex("092e000000000012")).decode()
    sample = [{"code":"phase_a","event_time":1700000000000,"value":phase},
              {"code":"temp_current","event_time":1700000001000,"value":"198"},
              {"code":"total_forward_energy","event_time":1700000002000,"value":"3493"}]
    assert check(sample)[1] == 3
    try:
        check(sample + [{"code":"total_forward_energy","event_time":1700000003000,"value":"3492"}])
        raise AssertionError("counter regression accepted")
    except AssertionError as e:
        assert str(e) == "counter regressed"
    print("PASS synthetic: phase, temperature, counter and counter-regression rejection")
    if len(sys.argv) > 1:
        data=json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
        assert data.get("complete") is True
        events=[event for window in data["windows"] for event in window["logs"]]
        counts, largest=check(events)
        print("PASS private archive:",len(events),"events; counts",dict(counts),"; max 10-min window",largest)
        assert largest <= 100, "these records require shorter windows or pagination"

if __name__ == "__main__":
    main()
