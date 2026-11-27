#!/usr/bin/env python3
"""Generate or check the scaled pi/2 constant used by the TeX fp evaluator."""
import argparse
from pathlib import Path
import re

# Input magnitudes reach 10^200; 16 result digits plus 24 guard digits follow.
DIGITS = 240
GUARD = 30
ROOT = Path(__file__).resolve().parents[1]
DEFAULT = ROOT / 'lmd/package/doc/latex/tex_engine/fp.ls'


def calculate():
    scale = 10 ** (DIGITS + GUARD)

    def atan_inverse(base):
        power, square, total, index = base, base * base, 0, 0
        while True:
            term = scale // ((2 * index + 1) * power)
            if not term:
                return total, index + 1
            total = total + term if index % 2 == 0 else total - term
            power *= square
            index += 1

    # Machin's formula and 30 extra digits bound all series-floor errors.
    a, na = atan_inverse(5)
    b, nb = atan_inverse(239)
    estimate = (16 * a - 4 * b) // 2
    bound = 8 * na + 2 * nb + 1
    divisor = 10 ** GUARD
    remainder = estimate % divisor
    if min(remainder, divisor - remainder) <= bound:
        raise ValueError('insufficient guard digits to determine pi/2')
    return estimate // divisor


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    parser.add_argument('--source', type=Path, default=DEFAULT)
    args = parser.parse_args()
    expected = str(calculate())
    if not args.check:
        print(expected)
        return
    source = args.source.read_text()
    matches = re.findall(r'^let QUARTER_SCALED = (\d+)n;$', source, re.M)
    if matches != [expected]:
        raise SystemExit('TeX fp pi/2 constant differs from the generated value')
    print('TeX fp pi/2 constant verified')


if __name__ == '__main__':
    main()
