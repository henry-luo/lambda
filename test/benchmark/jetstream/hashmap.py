#!/usr/bin/env python3
"""JetStream hash-map workload, matching hash-map.js's timed run()."""

import time


class Entry:
    __slots__ = ("key", "value", "next")

    def __init__(self, key, value, next_entry):
        self.key = key
        self.value = value
        self.next = next_entry


class HashMap:
    __slots__ = ("buckets", "size", "threshold")

    def __init__(self):
        # hash-map.js uses `new Array(this.capacity)` while only `_capacity`
        # exists. Its actual initial table has one bucket and threshold zero.
        self.buckets = [None]
        self.size = 0
        self.threshold = 0

    def put(self, key, value):
        index = key & (len(self.buckets) - 1)
        entry = self.buckets[index]
        while entry is not None:
            if entry.key == key:
                entry.value = value
                return
            entry = entry.next
        self.buckets[index] = Entry(key, value, self.buckets[index])
        self.size += 1
        if self.size > self.threshold:
            grown = [None] * (len(self.buckets) * 2)
            for head in self.buckets:
                entry = head
                while entry is not None:
                    next_entry = entry.next
                    index = entry.key & (len(grown) - 1)
                    entry.next = grown[index]
                    grown[index] = entry
                    entry = next_entry
            self.buckets = grown
            self.threshold = len(grown) * 3 // 4

    def get(self, key):
        index = key & (len(self.buckets) - 1)
        entry = self.buckets[index]
        while entry is not None:
            if entry.key == key:
                return entry.value
            entry = entry.next
        raise KeyError(key)

    def entries(self):
        for head in self.buckets:
            entry = head
            while entry is not None:
                yield entry
                entry = entry.next


def run():
    count = 90000
    table = HashMap()
    for key in range(count):
        table.put(key, 42)

    total = 0
    for _ in range(5):
        for key in range(count):
            total += table.get(key)

    key_total = 0
    value_total = 0
    for entry in table.entries():
        key_total += entry.key
        value_total += entry.value

    return (table.size == count and total == 42 * count * 5
            and key_total == count * (count - 1) // 2
            and value_total == 42 * count)


def main():
    start = time.perf_counter_ns()
    ok = run()
    elapsed_ms = (time.perf_counter_ns() - start) / 1_000_000
    print("hash-map: " + ("PASS" if ok else "FAIL"))
    print(f"__TIMING__:{elapsed_ms:.3f}")
    if not ok:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
