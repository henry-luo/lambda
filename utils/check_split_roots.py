#!/usr/bin/env python3
"""Ensure Lambda uses the selected sibling projects for source ownership."""

import os
from pathlib import Path
import sys

root = Path(__file__).resolve().parent.parent
input_root = Path(os.environ.get('LAMBDA_INPUT_ROOT', root.parent / 'lambda-input')).resolve()
radiant_root = Path(os.environ.get('RADIANT_ROOT', root.parent / 'radiant')).resolve()
for forbidden in (root / 'lib', root / 'radiant', root / 'lambda/input',
                  root / 'lambda/io', root / 'lambda/network', root / 'lambda/format'):
    if forbidden.exists() or forbidden.is_symlink():
        print(f'SPLIT_ROOTS: {forbidden} must not exist in Lambda', file=sys.stderr)
        raise SystemExit(1)
source_suffixes = {'.c', '.cc', '.cpp', '.h', '.hpp', '.mm', '.py'}
for source_tree in (root / 'lambda', root / 'test', root / 'utils'):
    for directory, names, files in os.walk(source_tree, followlinks=False):
        for name in names + files:
            link = Path(directory) / name
            if link.is_symlink() and link.suffix in source_suffixes and any(
                    link.resolve().is_relative_to(owner) for owner in (input_root, radiant_root)):
                print(f'SPLIT_ROOTS: sibling source link in Lambda: {link}', file=sys.stderr)
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
print('SPLIT_ROOTS: source ownership follows the sibling project roots')
