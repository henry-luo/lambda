#!/usr/bin/env python3
"""Mandatory-path runtime-call census over a MIR Direct dump (Tune27 §6).

A call is reported only when its basic block DOMINATES the latch of a natural
loop, i.e. it executes on every iteration.  Anything reachable only through a
guard-failure arm -- the int53 overflow stubs, the `item_at` fallback of a
native float load -- is excluded by construction, which a call-count or an
"arm is short" heuristic cannot do correctly.

Usage:
    LAMBDA_TIER=jit LAMBDA_MIR_DUMP_PATH=temp/x.mir ./lambda.exe run bench.ls
    python3 test/benchmark/mir_mandatory_census.py temp/x.mir [...]

Caveat: the dump holds the whole MIR context and is rewritten per module, so a
benchmark that imports a core module leaves only the entry module behind.  Dump
the core `.ls` separately.
"""
import collections
import os
import re
import sys

CALL = re.compile(r'^\s*call\s+([A-Za-z_]\w*)\s*,\s*([A-Za-z_]\w*)')
LABEL = re.compile(r'^(L\d+|[A-Za-z_]\w*):\s*$')
FUNC = re.compile(r'^([A-Za-z_]\w*):\s*func\b')
COND = re.compile(r'^\s*(bt|bf|beq|bne|blt|ble|bgt|bge|ubgt|ubge|ublt|uble'
                  r'|bo|bno|bts|bfs|dbeq|dbne|dblt|dble|dbgt|dbge|fbeq|fbne'
                  r'|ubeq|ubne)\b')
JMP = re.compile(r'^\s*jmp\b')
RET = re.compile(r'^\s*(ret|jcall)\b')
LREF = re.compile(r'\b(L\d+)\b')

# emitted in every prologue; carries no per-operation meaning
PROLOGUE = {'lambda_stack_overflow_error', 'lambda_side_stack_ensure_for',
            'lambda_module_name_id_at', 'lambda_module_var_store',
            'pn_print', 'pn_clock', 'pn_error'}


def functions(path):
    out, name, body = {}, None, []
    for line in open(path, errors='replace').read().split('\n'):
        head = FUNC.match(line)
        if head:
            if name:
                out[name] = body
            name, body = head.group(1), []
        elif line.strip() == 'endfunc':
            if name:
                out[name] = body
            name, body = None, []
        elif name is not None:
            body.append(line)
    if name:
        out[name] = body
    return out


def build_cfg(body):
    labels = {}
    for i, line in enumerate(body):
        head = LABEL.match(line.strip())
        if head:
            labels[head.group(1)] = i

    leaders = {0}
    for i, line in enumerate(body):
        if LABEL.match(line.strip()):
            leaders.add(i)
        if (COND.match(line) or JMP.match(line) or RET.match(line)) and i + 1 < len(body):
            leaders.add(i + 1)

    starts = sorted(leaders)
    blocks, index_of = [], {}
    for n, start in enumerate(starts):
        end = starts[n + 1] if n + 1 < len(starts) else len(body)
        index_of[start] = n
        blocks.append((start, end))

    def block_at(line):
        chosen = 0
        for start in starts:
            if start <= line:
                chosen = start
            else:
                break
        return index_of[chosen]

    succ = collections.defaultdict(set)
    for n, (start, end) in enumerate(blocks):
        insns = [body[i] for i in range(start, end)
                 if body[i].strip() and not LABEL.match(body[i].strip())]
        if not insns:
            if n + 1 < len(blocks):
                succ[n].add(n + 1)
            continue
        last = insns[-1]
        if COND.match(last) or JMP.match(last):
            for ref in LREF.finditer(last):
                if ref.group(1) in labels:
                    succ[n].add(block_at(labels[ref.group(1)]))
        if not RET.match(last) and not JMP.match(last) and n + 1 < len(blocks):
            succ[n].add(n + 1)
    return blocks, succ


def dominators(count, succ):
    pred = collections.defaultdict(set)
    for a, targets in succ.items():
        for b in targets:
            pred[b].add(a)
    dom = [set(range(count)) for _ in range(count)]
    dom[0] = {0}
    changed = True
    while changed:
        changed = False
        for n in range(1, count):
            sets = [dom[p] for p in pred[n]]
            merged = {n} | (set.intersection(*sets) if sets else set())
            if merged != dom[n]:
                dom[n] = merged
                changed = True
    return dom


def mandatory_calls(body):
    blocks, succ = build_cfg(body)
    if not blocks:
        return []
    dom = dominators(len(blocks), succ)

    # natural loop: back edge latch -> header where the header dominates the
    # latch.  A block runs every iteration when it dominates the latch and is
    # itself dominated by the header.
    every_iteration = set()
    for latch, targets in succ.items():
        for header in targets:
            if header in dom[latch]:
                for block in range(len(blocks)):
                    if block in dom[latch] and header in dom[block]:
                        every_iteration.add(block)

    found = []
    for block in sorted(every_iteration):
        start, end = blocks[block]
        for i in range(start, end):
            call = CALL.match(body[i])
            if call and call.group(2) not in PROLOGUE:
                found.append(call.group(2))
    return found


def census(path, include_lambda_calls=False):
    hist = collections.Counter()
    for body in functions(path).values():
        hist.update(mandatory_calls(body))
    if not include_lambda_calls:
        hist = collections.Counter({k: v for k, v in hist.items()
                                    if not k.startswith('_')})
    return hist


def main(argv):
    include = '--with-lambda-calls' in argv
    paths = [a for a in argv if not a.startswith('--')]
    if not paths:
        print(__doc__)
        return 1
    for path in paths:
        hist = census(path, include)
        name = os.path.basename(path).replace('.mir', '')
        if not hist:
            print(f'{name:<18} (none - mandatory loop paths are call-free)')
            continue
        top = ', '.join(f'{k}:{v}' for k, v in hist.most_common(10))
        print(f'{name:<18} {top}')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
