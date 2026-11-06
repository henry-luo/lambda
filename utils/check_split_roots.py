#!/usr/bin/env python3
"""Ensure the transitional include paths and selected archives share owners."""

import os
from pathlib import Path
import sys

root = Path(__file__).resolve().parent.parent
input_root = Path(os.environ.get('LAMBDA_INPUT_ROOT', root.parent / 'lambda-input')).resolve()
radiant_root = Path(os.environ.get('RADIANT_ROOT', root.parent / 'radiant')).resolve()
paths = ((root / 'lambda/input', input_root / 'lambda/input'),
         (root / 'lib', input_root / 'lib'),
         (root / 'radiant', radiant_root / 'radiant'),
         *((radiant_root / 'lambda' / name, input_root / 'lambda' / name)
           for name in ('input', 'core', 'io', 'format', 'network')),
         (radiant_root / 'lib', input_root / 'lib'))
for link, expected in paths:
    if not link.is_dir() or not expected.is_dir() or expected.resolve() != expected or \
            link.resolve() != expected:
        print(f'SPLIT_ROOTS: {expected} is not the owning directory for {link}', file=sys.stderr)
        raise SystemExit(1)
print('SPLIT_ROOTS: source links match the selected sibling projects')
