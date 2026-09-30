#!/usr/bin/env python3
"""Recursive document diff workload from microdiff.js."""

import time

ROUNDS = 512
MODULUS = 1000000007


class RichValue:
    __slots__ = ("kind", "value")

    def __init__(self, kind, value):
        self.kind = kind
        self.value = value


def diff(old, new, stack=()):
    differences = []
    old_is_array = isinstance(old, list)
    old_items = enumerate(old) if old_is_array else old.items()
    for key, old_value in old_items:
        path_key = key
        if (key >= len(new) if old_is_array else key not in new):
            differences.append(dict(type="REMOVE", path=[path_key], oldValue=old_value))
            continue
        new_value = new[key]
        compatible = (isinstance(old_value, (dict, list, RichValue))
                      and isinstance(new_value, (dict, list, RichValue))
                      and isinstance(old_value, list) == isinstance(new_value, list))
        if (old_value is not None and new_value is not None and compatible
                and not isinstance(old_value, RichValue)
                and all(id(old_value) != id(parent) for parent in stack)):
            for difference in diff(old_value, new_value, stack + (old_value,)):
                difference["path"].insert(0, path_key)
                differences.append(difference)
        elif not values_equal(old_value, new_value):
            differences.append(dict(type="CHANGE", path=[path_key],
                                    value=new_value, oldValue=old_value))
    new_is_array = isinstance(new, list)
    new_items = enumerate(new) if new_is_array else new.items()
    for key, new_value in new_items:
        if (key >= len(old) if new_is_array else key not in old):
            differences.append(dict(type="CREATE", path=[key], value=new_value))
    return differences


def values_equal(left, right):
    if isinstance(left, RichValue) and isinstance(right, RichValue):
        return left.kind == right.kind and left.value == right.value
    return type(left) is type(right) and left == right


def make_snapshot(version):
    return {
        "document": {
            "title": "Text benchmark — revised" if version else "Text benchmark",
            "sections": [
                {"id": "intro", "blocks": [
                    {"type": "paragraph", "text": "A short paragraph of source text."},
                    {"type": "code", "language": "js", "lines": 18 if version else 12},
                ]},
                {"id": "body", "blocks": [
                    {"type": "heading", "level": 2 if version else 1, "text": "Algorithms"},
                    {"type": "list", "items": ["diff", "snapshot", "hyphen"] if version
                     else ["diff", "snapshot"]},
                ]},
            ],
        },
        "options": {
            "theme": "dark" if version else "light",
            "flags": {"trackChanges": bool(version), "preserveWhitespace": True},
        },
        "tags": ["text", "benchmark", "updated"] if version else ["text", "benchmark"],
        "updated": RichValue("Date", 1700000001000 if version else 1700000000000),
        "pattern": RichValue("RegExp", "source|text|diff" if version else "source|text"),
        "value": 42 if version else 41,
    }


def main():
    pairs = [(make_snapshot(index % 2 == 0), make_snapshot(index % 2 != 0))
             for index in range(4)]
    checksum = 0
    started = time.perf_counter_ns()
    for _ in range(ROUNDS):
        for old, new in pairs:
            differences = diff(old, new)
            checksum = (checksum + len(differences) * 19) % MODULUS
            for difference in differences:
                checksum = (checksum + len(difference["type"]) * 23
                            + len(difference["path"])) % MODULUS
    elapsed_ms = (time.perf_counter_ns() - started) / 1_000_000
    if checksum != 3278848:
        raise RuntimeError(f"microdiff checksum mismatch: {checksum}")
    print(f"CHECKSUM:{checksum}")
    print(f"__TIMING__:{elapsed_ms:.3f}")


if __name__ == "__main__":
    main()
