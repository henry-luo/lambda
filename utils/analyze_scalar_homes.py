#!/usr/bin/env python3
"""Count MIR instructions attributable to wide-scalar-home handling in
Lambda mir_dump.txt files.

Categories:
  A. adopt sequences   : classify+branch+call cluster ending in a call to
                         lambda_item_adopt_scalar_home (20 insns/site:
                         ursh,eq,eq,or,bt,eq,eq,or,bf,and,bt,eq,bt,eq,bt,jmp,
                         mov,jmp,call,mov)
  B. home materialize  : `add %rX, %base, imm` insns whose dest is used as a
                         home pointer (adopt target arg, or trailing arg of a
                         user-fn call whose callee has a p:_scalar_home param)
  C. call-site widening: user-fn call insns that carry a trailing home arg
                         (counted as sites, not extra insns)
  D. func home param   : functions declaring p:_scalar_home (ABI cost, sites)
"""
import re, sys, os

ADOPT_SEQ_LEN = 20  # measured: 16 classify + 2 passthrough + call + mov

def parse(path):
    funcs = {}          # name -> list of insn lines
    cur = None
    home_param_funcs = set()
    proto_home = set()  # proto names whose last param is p (candidate home-taking protos)
    with open(path) as f:
        for raw in f:
            line = raw.rstrip('\n')
            m = re.match(r'^(\w+):\tfunc\t(.*)$', line)
            if m:
                cur = m.group(1)
                funcs[cur] = []
                if '_scalar_home' in m.group(2):
                    home_param_funcs.add(cur)
                continue
            if re.match(r'^\w+:\tproto\t', line):
                continue
            if cur is None:
                continue
            s = line.strip()
            if not s or s.startswith('local') or s.startswith('forward') \
               or s.startswith('import') or s.startswith('export') \
               or s.startswith('#') or re.match(r'^L\d+:$', s) \
               or re.match(r'^\w+:\s*(bss|module)', s):
                continue
            # a label prefix on same line? dumps put labels on own lines
            funcs[cur].append(s)
    return funcs, home_param_funcs

def analyze(path):
    funcs, home_param_funcs = parse(path)
    total = 0
    adopt_sites = 0
    home_mat = 0
    home_call_sites = 0
    per_func = {}
    for fn, insns in funcs.items():
        total += len(insns)
        # map dest reg -> index of defining add-with-const insn
        add_def = {}
        used_adds = set()
        f_adopt = 0
        f_callsites = 0
        for i, ins in enumerate(insns):
            m = re.match(r'add\t(%\w+), (%\w+), (-?\d+)$', ins)
            if m:
                add_def[m.group(1)] = i
            if ins.startswith('call\t'):
                parts = [p.strip() for p in ins.split('\t',1)[1].split(',')]
                callee = parts[1] if len(parts) > 1 else ''
                args = parts[2:]  # result reg + actual args
                if callee == 'lambda_item_adopt_scalar_home':
                    f_adopt += 1
                    # target home = last arg
                    tgt = args[-1] if args else ''
                    if tgt in add_def:
                        used_adds.add(add_def[tgt])
                elif callee.startswith('_'):
                    # user lambda function call: does callee take a home?
                    if callee in home_param_funcs or callee.rstrip('0123456789').endswith('_'):
                        tgt = args[-1] if args else ''
                        if tgt == '_scalar_home' or tgt in add_def:
                            f_callsites += 1
                            if tgt in add_def:
                                used_adds.add(add_def[tgt])
        adopt_sites += f_adopt
        home_mat += len(used_adds)
        home_call_sites += f_callsites
        if f_adopt or f_callsites:
            per_func[fn] = (len(insns), f_adopt, f_callsites)
    a_insns = adopt_sites * ADOPT_SEQ_LEN
    home_insns = a_insns + home_mat
    return {
        'total': total,
        'funcs': len(funcs),
        'home_param_funcs': len(home_param_funcs),
        'adopt_sites': adopt_sites,
        'adopt_insns': a_insns,
        'home_materialize_insns': home_mat,
        'home_call_sites': home_call_sites,
        'home_insns': home_insns,
        'pct': 100.0 * home_insns / total if total else 0.0,
        'per_func': per_func,
    }

if __name__ == '__main__':
    rows = []
    for path in sys.argv[1:]:
        name = os.path.basename(path).replace('.mir','')
        r = analyze(path)
        rows.append((name, r))
    print(f"{'bench':<12} {'insns':>6} {'funcs':>5} {'homeP':>5} "
          f"{'adoptS':>6} {'adoptI':>6} {'matI':>5} {'callS':>5} "
          f"{'homeI':>6} {'pct':>6}")
    for name, r in rows:
        print(f"{name:<12} {r['total']:>6} {r['funcs']:>5} {r['home_param_funcs']:>5} "
              f"{r['adopt_sites']:>6} {r['adopt_insns']:>6} {r['home_materialize_insns']:>5} "
              f"{r['home_call_sites']:>5} {r['home_insns']:>6} {r['pct']:>5.1f}%")
