#!/usr/bin/env python3
"""Guard grammar.js (production) and grammar-lambda.js (official full grammar)
against drift.

Both compose grammar-common.js, so their cores cannot diverge by construction.
What CAN drift is the replacement layer each one carries: the production layer
must define exactly the seam names the core references, and must not smuggle in
extra rules the full grammar does not have.
"""
import re
import sys
from pathlib import Path

PKG = Path(__file__).resolve().parent.parent / 'lambda' / 'tree-sitter-lambda'
RULE_RE = re.compile(r'^    ([A-Za-z_][A-Za-z_0-9]*): [\$_]\s*=>', re.M)

# names the shared core references and every type layer must therefore provide
SEAM = {'_type_pattern', '_primary_type', '_view_atom_type', '_value_island', 'content_type'}

# These complete common rules are intentionally replaced by both the full type
# layer and/or the production external-token layer. Everything else must remain
# owned by grammar-common.js.
RULE_OVERRIDES = {
    '_attr_dotted_name', 'dotted_name', 'return_type', 'view_pattern', 'path_expr'
}


def layer_rules(path):
    src = path.read_text()
    start = src.index('TypeLayer = {') if 'TypeLayer = {' in src else src.index('typeLayer = {')
    return set(RULE_RE.findall(src[start:]))


def main():
    prod = layer_rules(PKG / 'grammar.js')
    full = layer_rules(PKG / 'grammar-lambda.js')
    common = (PKG / 'grammar-common.js').read_text()
    core = set(RULE_RE.findall(common[common.index('coreRules: {'):]))

    problems = []

    overlap = (core & (prod | full)) - RULE_OVERRIDES
    if overlap:
        problems.append(f"rules defined in BOTH the core and a type layer: {sorted(overlap)}")

    for name, rules in (('production', prod), ('full', full)):
        missing = {s for s in SEAM if s not in rules and s not in core}
        if missing:
            problems.append(f"{name} type layer is missing seam rules: {sorted(missing)}")

    extra = prod - full
    if extra:
        # the production layer legitimately adds external token wrappers; flag
        # anything else, which would mean the shipped grammar accepts syntax the
        # normative grammar does not describe
        non_token = {r for r in extra if 'token' not in r and r not in RULE_OVERRIDES}
        if non_token:
            problems.append(f"production-only rules that are not scanner tokens: {sorted(non_token)}")

    if problems:
        print("grammar sync check FAILED:")
        for p in problems:
            print(f"  - {p}")
        return 1

    print(f"grammar sync check OK — core {len(core)} rules; "
          f"type layer: full {len(full)}, production {len(prod)}")
    return 0


if __name__ == '__main__':
    sys.exit(main())
