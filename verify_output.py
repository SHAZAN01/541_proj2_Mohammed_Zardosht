#!/usr/bin/env python3
"""Replay actual output to check FIFO, slots, counts, IDs, and timestamps."""
import collections
import pathlib
import re
import sys


def verify(path):
    lines = path.read_text().splitlines()
    header = re.fullmatch(
        r"Bounded buffer: capacity=5, runtime=(\d+) s, producers=(\d+), consumers=(\d+)",
        lines[0])
    assert header, "Missing run header"
    _, np, nc = map(int, header.groups())
    created = collections.Counter()
    attempts = {}
    finished = set()
    queue = collections.deque()
    slots = [-1] * 5
    put = take = ptotal = ctotal = 0
    pending_snapshot = False
    stop_seen = False
    summary_seen = False
    max_wait = {"Producer": 0.0, "Consumer": 0.0}
    last_time = -1.0
    slot_index = 0
    maximum_count = 0
    for line in lines[1:]:
        if not line:
            continue
        if pending_snapshot:
            if slot_index < 5:
                assert line == f"slot {slot_index}: {slots[slot_index]}", (
                    "Snapshot differs from replayed circular buffer", line, slots)
                slot_index += 1
                continue
            assert line == f"Buffer count: {len(queue)}/5", "Wrong buffer count"
            pending_snapshot = False
            continue
        if line.startswith("Synchronization initialized:") or line.startswith("Times are elapsed"):
            continue
        match = re.fullmatch(r"Created (Producer|Consumer) (\d+)", line)
        if match:
            role, ident = match.groups()
            created[(role, int(ident))] += 1
            continue
        match = re.fullmatch(
            r"(Producer|Consumer) (\d+) tries to (?:insert (\d+)|consume) at time ([\d.]+) s \(operation (\d+)\)", line)
        if match:
            role, ident, item, timestamp, operation = match.groups()
            ident, operation, timestamp = int(ident), int(operation), float(timestamp)
            assert not stop_seen, "New attempt after stopping"
            assert 1 <= ident <= (np if role == "Producer" else nc), "ID outside configured range"
            assert timestamp >= last_time, "Time moved backward"
            last_time = timestamp
            key = role, ident, operation
            assert key not in attempts, "Duplicate attempt"
            assert operation == 1 or (role, ident, operation - 1) in finished, "Worker reused an unfinished operation"
            attempts[key] = (timestamp, int(item) if item is not None else None)
            continue
        match = re.fullmatch(
            r"(Producer|Consumer) (\d+) (produced|consumed) (\d+) at time ([\d.]+) s \(operation (\d+), attempted ([\d.]+) s, waited ([\d.]+) s\)", line)
        if match:
            role, ident, verb, item, done, operation, tried, waited = match.groups()
            key = role, int(ident), int(operation)
            item, done, tried, waited = int(item), float(done), float(tried), float(waited)
            assert not stop_seen, "Buffer mutation after stopping"
            assert key in attempts and key not in finished, "Completion lacks a unique attempt"
            assert abs(attempts[key][0] - tried) < 0.000001, "Mismatched attempt time"
            assert done >= tried and waited >= 0, "Negative wait"
            assert abs(done - tried - waited) <= 0.000002, "Wrong wait duration"
            assert done >= last_time, "Time moved backward"
            last_time = done
            assert 0 <= item <= 1000, "Item outside allowed range"
            max_wait[role] = max(max_wait[role], waited)
            if role == "Producer":
                assert verb == "produced" and len(queue) < 5, "Buffer overflow"
                assert attempts[key][1] == item, "Inserted item differs from attempt"
                assert slots[put] == -1, "Overwrote occupied slot"
                queue.append(item)
                slots[put] = item
                put = (put + 1) % 5
                ptotal += 1
            else:
                assert verb == "consumed" and queue, "Buffer underflow"
                assert queue.popleft() == item, "FIFO order violated"
                assert slots[take] == item, "Wrong removal slot"
                slots[take] = -1
                take = (take + 1) % 5
                ctotal += 1
            assert ptotal - ctotal == len(queue), "Conservation failed"
            maximum_count = max(maximum_count, len(queue))
            finished.add(key)
            pending_snapshot, slot_index = True, 0
            continue
        match = re.fullmatch(r"Stopping at time ([\d.]+) s; pending attempts will be stopped\.", line)
        if match:
            assert not stop_seen and float(match[1]) >= last_time, "Bad stop timestamp"
            last_time = float(match[1])
            stop_seen = True
            continue
        match = re.fullmatch(
            r"(Producer|Consumer) (\d+) operation (\d+) stopped before (insertion|removal) at time ([\d.]+) s", line)
        if match:
            role, ident, operation, action, timestamp = match.groups()
            key = role, int(ident), int(operation)
            assert stop_seen and key in attempts and key not in finished, "Bad stopped attempt"
            assert action == ("insertion" if role == "Producer" else "removal")
            assert float(timestamp) >= last_time, "Time moved backward"
            last_time = float(timestamp)
            finished.add(key)
            continue
        match = re.fullmatch(r"Summary: produced=(\d+) consumed=(\d+) remaining=(\d+)", line)
        if match:
            assert stop_seen and not summary_seen, "Bad summary location"
            assert tuple(map(int, match.groups())) == (ptotal, ctotal, len(queue)), "Incorrect summary"
            summary_seen = True
            pending_snapshot, slot_index = True, 0
            continue
        assert line == "All threads joined; synchronization resources released.", ("Unexpected output", line)
        assert summary_seen, "Missing summary before cleanup"

    assert not pending_snapshot and summary_seen and stop_seen, "Truncated output"
    assert lines[-1] == "All threads joined; synchronization resources released.", "Missing successful exit marker"
    expected = {(role, ident) for role, n in [("Producer", np), ("Consumer", nc)] for ident in range(1, n + 1)}
    assert set(created) == expected and all(n == 1 for n in created.values()), "Missing/duplicate thread IDs"
    assert finished == set(attempts), "Unresolved attempt at exit"
    assert ptotal > 0 and ctotal > 0, "Run did not exercise both operation types"
    return (f"PASS {path.name}: FIFO, slots, count 0..5, IDs, attempt/completion times, "
            f"shutdown; produced={ptotal}, consumed={ctotal}, remaining={len(queue)}, "
            f"peak={maximum_count}, max producer wait={max_wait['Producer']:.6f}s, "
            f"max consumer wait={max_wait['Consumer']:.6f}s")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit("Usage: python3 verify_output.py output.txt [more-output-files ...]")
    try:
        for filename in sys.argv[1:]:
            print(verify(pathlib.Path(filename)))
    except (AssertionError, ValueError, IndexError, OSError) as error:
        sys.exit(f"FAIL: {error}")
