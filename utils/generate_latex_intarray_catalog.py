#!/usr/bin/env python3
"""Extract static and initially empty expl3 arrays from the bundled TeX source."""

import argparse
import hashlib
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "lmd/package/doc/latex/tex/expl3-code.tex"
DESTINATION = ROOT / "lmd/package/doc/latex/tex_engine/intarray_catalog.ls"


def render(data):
    source = data.decode("utf-8")
    pattern = re.compile(
        r"\\intarray_const_from_clist:Nn\s+\\(c__fp_(?:exp|trig)_intarray)\s*\{([^{}]*)\}",
        re.S,
    )
    arrays = []
    for match in pattern.finditer(source):
        body = re.sub(r"%[^\n]*", "", match.group(2))
        values = [re.sub(r"\s+", "", item) for item in body.split(",") if item.strip()]
        if not values or any(not re.fullmatch(r"[+-]?\d+", value) for value in values):
            raise ValueError(f"unsupported integer array data: {match.group(1)}")
        arrays.append((match.group(1), [int(value) for value in values]))
    if [name for name, _ in arrays] != ["c__fp_exp_intarray", "c__fp_trig_intarray"]:
        raise ValueError("unexpected static floating-point array declarations")
    regex_pattern = re.compile(
        r"(?m)^\\intarray_new:Nn\s+\\(g__regex_[a-z_]+_intarray)\s*\{\s*(\d+)\s*\}"
    )
    regex_arrays = [(match.group(1), int(match.group(2))) for match in regex_pattern.finditer(source)]
    prefix = re.search(
        r"\\int_step_inline:nn\s*\{\s*(\d+)\s*\}\s*"
        r"\{\s*\\__kernel_intarray_gset:Nnn\s*#1\s*\{##1\}\s*\\c_zero_int\s*\}",
        source,
    )
    if len(regex_arrays) != 7 or prefix is None:
        raise ValueError("unexpected expl3 regex array declarations or font initializer")
    zero_prefix = int(prefix.group(1))
    if zero_prefix <= 0 or any(length < zero_prefix for _, length in regex_arrays):
        raise ValueError("invalid expl3 regex array length")
    lines = ["// Generated from bundled expl3-code.tex; do not edit by hand.",
             "// Regenerate: python3 utils/generate_latex_intarray_catalog.py",
             "// Source SHA-256: " + hashlib.sha256(data).hexdigest(), "let arrays = ["]
    for array_index, (name, values) in enumerate(arrays):
        lines.append('    {name:"' + name + '", values:[')
        for index in range(0, len(values), 12):
            suffix = "," if index + 12 < len(values) else ""
            lines.append("        " + ",".join(str(value) for value in values[index:index + 12]) + suffix)
        lines.append("    ]}" + ("," if array_index + 1 < len(arrays) else ""))
    lines.extend(["];", "pub fn prepared() => arrays", "let regex_arrays = ["])
    for index, (name, length) in enumerate(regex_arrays):
        suffix = "," if index + 1 < len(regex_arrays) else ""
        lines.append('    {name:"' + name + '", length:' + str(length) +
                     ', values:[' + ",".join("0" for _ in range(zero_prefix)) +
                     '], regex:true}' + suffix)
    lines.extend(["];", "pub fn regex_prepared() => regex_arrays"])
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="verify the generated catalog")
    args = parser.parse_args()
    data = SOURCE.read_bytes()
    output = render(data)
    if SOURCE.read_bytes() != data:
        raise SystemExit("the bundled expl3 source changed during generation")
    if args.check:
        if not DESTINATION.is_file() or DESTINATION.read_text() != output:
            raise SystemExit("LaTeX integer-array catalog differs; regenerate it")
    else:
        DESTINATION.write_text(output)


if __name__ == "__main__":
    main()
