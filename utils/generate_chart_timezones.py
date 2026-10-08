#!/usr/bin/env python3
"""Generate the portable chart time-zone resource from unmodified zic output.

Compile the ten standard tzdata sources with zic first; pass that directory and
the original release archive so the resource records its exact provenance.
"""

import argparse
import hashlib
import json
from pathlib import Path
import re
import struct


def clock(text):
    sign = -1 if text.startswith("-") else 1
    parts = [int(part) for part in text.lstrip("+-").split(":")]
    return sign * sum(part * factor for part, factor in zip(parts, (3600, 60, 1)))


def rule(text):
    day, _, at = text.partition("/")
    basis = at[-1:] if at[-1:] in ("w", "s", "u", "g", "z") else "w"
    if at[-1:] in ("w", "s", "u", "g", "z"):
        at = at[:-1]
    values = [int(value) for value in day.lstrip("MJ").split(".")]
    return {"kind": day[0] if day[0] in "MJ" else "N", "values": values,
            "seconds": clock(at) if at else 7200, "basis": basis}


def future(text):
    abbreviation = r"(?:<[+-]?[A-Za-z0-9]+>|[A-Za-z]{3,})"
    offset = r"[+-]?\d+(?::\d+(?::\d+)?)?"
    match = re.fullmatch(f"({abbreviation})({offset})(?:({abbreviation})({offset})?)?(?:,([^,]+),([^,]+))?", text)
    if not match:
        raise ValueError(f"unsupported TZif footer: {text!r}")
    std, std_offset, dst, dst_offset, start, end = match.groups()
    standard = {"name": std.strip("<>"), "offset": -clock(std_offset), "dst": False}
    if dst is None:
        return {"standard": standard}
    if start is None or end is None:
        raise ValueError(f"daylight footer has no explicit rules: {text!r}")
    return {"standard": standard,
            "daylight": {"name": dst.strip("<>"), "offset": -clock(dst_offset) if dst_offset else standard["offset"] + 3600, "dst": True},
            "start": rule(start), "end": rule(end)}


def block(data, position, width):
    if data[position:position + 4] != b"TZif":
        raise ValueError("not a TZif file")
    gmt, standard, leaps, count, types, characters = struct.unpack_from(">6I", data, position + 20)
    position += 44
    times = list(struct.unpack_from(f">{count}{'q' if width == 8 else 'i'}", data, position))
    position += count * width
    indices = list(data[position:position + count])
    position += count
    entries = [struct.unpack_from(">iBB", data, position + index * 6) for index in range(types)]
    position += types * 6
    names = data[position:position + characters]
    entries = [{"offset": offset, "dst": bool(dst), "name": names[index:].split(b"\0", 1)[0].decode("ascii")}
               for offset, dst, index in entries]
    position += characters + leaps * (width + 4) + standard + gmt
    return {"times": times, "indices": indices, "types": entries}, position


def parse(data):
    result, end = block(data, 0, 4)
    if data[4:5] in (b"2", b"3", b"4"):
        result, end = block(data, end, 8)
        footer = data[end:].strip(b"\n").decode("ascii")
        if footer:
            result["future"] = future(footer)
    offsets = {entry["offset"] for entry in result["types"]}
    offsets.update(entry["offset"] for key, entry in result.get("future", {}).items() if key in ("standard", "daylight"))
    result["offsets"] = sorted(offsets)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("compiled", type=Path)
    parser.add_argument("archive", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    version = re.fullmatch(r"tzdata(\d{4}[a-z]+)\.tar\.gz", args.archive.name)
    if not version:
        parser.error("archive must use its original tzdataVERSION.tar.gz name")
    names, records, ids = {}, [], {}
    for path in sorted(args.compiled.rglob("*")):
        if not path.is_file():
            continue
        data = path.read_bytes()
        if data[:4] != b"TZif":
            continue
        digest = hashlib.sha256(data).hexdigest()
        if digest not in ids:
            ids[digest] = len(records)
            records.append(parse(data))
        names[path.relative_to(args.compiled).as_posix()] = ids[digest]
    if not names:
        parser.error("compiled directory contains no TZif files")
    result = {"version": version[1], "source": f"https://data.iana.org/time-zones/releases/{args.archive.name}",
              "sha256": hashlib.sha256(args.archive.read_bytes()).hexdigest(), "names": names, "records": records}
    args.output.write_text(json.dumps(result, ensure_ascii=True, separators=(",", ":")) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
