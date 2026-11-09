#!/usr/bin/env python3
"""Ensure Lambda uses the selected sibling projects for source ownership."""

import os
from pathlib import Path
import sys

root = Path(__file__).resolve().parent.parent
input_root = Path(os.environ.get('LAMBDA_INPUT_ROOT', root.parent / 'lambda-input')).resolve()
radiant_root = Path(os.environ.get('RADIANT_ROOT', root.parent / 'radiant')).resolve()
paths = ((root / 'lambda/input', input_root / 'lambda/input'),)
for link, expected in paths:
    if not link.is_dir() or not expected.is_dir() or expected.resolve() != expected or \
            link.resolve() != expected:
        print(f'SPLIT_ROOTS: {expected} is not the owning directory for {link}', file=sys.stderr)
        raise SystemExit(1)
for forbidden in (root / 'lib', root / 'radiant'):
    if forbidden.exists() or forbidden.is_symlink():
        print(f'SPLIT_ROOTS: {forbidden} must not exist in Lambda', file=sys.stderr)
        raise SystemExit(1)
for owned in (input_root / 'lib', radiant_root / 'radiant'):
    if not owned.is_dir():
        print(f'SPLIT_ROOTS: missing sibling source directory {owned}', file=sys.stderr)
        raise SystemExit(1)
for source_tree in (input_root / 'lib', input_root / 'lambda', radiant_root / 'radiant'):
    for directory, names, files in os.walk(source_tree, followlinks=False):
        for name in names + files:
            link = Path(directory) / name
            if link.is_symlink() and link.resolve().is_relative_to(root):
                print(f'SPLIT_ROOTS: reverse link into Lambda: {link}', file=sys.stderr)
                raise SystemExit(1)
print('SPLIT_ROOTS: sibling roots are present without top-level source links')
