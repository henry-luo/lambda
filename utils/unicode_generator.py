"""Shared command-line driver for reproducible Unicode data generators."""

import argparse
from pathlib import Path


def codepoint_range(span):
    parts = span.split("..")
    return int(parts[0], 16), int(parts[-1], 16) + 1


def run_generator(generate, description, output, label, mismatch):
    parser = argparse.ArgumentParser(description=description)
    parser.add_argument("source", type=Path)
    parser.add_argument("--output", type=Path, default=output)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    text = generate(args.source.read_text(encoding="utf-8"))
    if args.check:
        if not args.output.exists() or args.output.read_text() != text:
            raise SystemExit(mismatch)
    else:
        args.output.write_text(text)
    print(f"{label}: {'checked' if args.check else 'generated'} {args.output}")
