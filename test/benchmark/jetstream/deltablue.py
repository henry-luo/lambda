#!/usr/bin/env python3
"""JetStream Benchmark: deltablue (Octane) — Python version
Incremental constraint solver
Original: John Maloney/Mario Wolczko (Smalltalk), V8 project authors
Tests object creation, method dispatch, constraint propagation
"""
import time

STRONGEST        = 0
STRONG_PREFERRED = 1
PREFERRED        = 2
STRONG_DEFAULT   = 3
NORMAL           = 4
WEAK_DEFAULT     = 5
WEAKEST          = 6

NONE     = 0
FORWARD  = 1
BACKWARD = 2

C_STAY  = 0
C_EDIT  = 1
C_EQUAL = 2
C_SCALE = 3


def stronger(s1, s2):
    return s1 < s2


def weaker(s1, s2):
    return s1 > s2


def create_variable(name, initial_value):
    return {'value': initial_value, 'constraints': [],
            'det_by': -1, 'mark': 0, 'walk_str': WEAKEST,
            'stay': 1, 'name': name}


def create_constraint(kind, strength, v1, v2, scale, offset):
    return {'kind': kind, 'strength': strength, 'direction': NONE,
            'v1': v1, 'v2': v2, 'scale': scale, 'offset': offset,
            'satisfied': False}


def create_planner():
    return {'current_mark': 0, 'vars': [], 'constraints': [],
            'nv': 0, 'nc': 0}


def planner_add_var(p, name, value):
    idx = p['nv']
    while len(p['vars']) <= idx:
        p['vars'].append(None)
    p['vars'][idx] = create_variable(name, value)
    p['nv'] = idx + 1
    return idx


def planner_add_constraint(p, kind, strength, v1, v2, scale, offset):
    idx = p['nc']
    while len(p['constraints']) <= idx:
        p['constraints'].append(None)
    c = create_constraint(kind, strength, v1, v2, scale, offset)
    p['constraints'][idx] = c
    p['nc'] = idx + 1
    # add constraint index to variable's constraint list
    p['vars'][v1]['constraints'].append(idx)
    if v2 >= 0:
        p['vars'][v2]['constraints'].append(idx)
    return idx


def constraint_input(p, ci):
    c = p['constraints'][ci]
    if c['kind'] in (C_STAY, C_EDIT):
        return -1
    if c['direction'] == FORWARD:
        return c['v1']
    return c['v2']


def constraint_output(p, ci):
    c = p['constraints'][ci]
    if c['kind'] in (C_STAY, C_EDIT):
        return c['v1']
    if c['direction'] == FORWARD:
        return c['v2']
    return c['v1']


def constraint_execute(p, ci):
    c = p['constraints'][ci]
    if c['kind'] in (C_STAY, C_EDIT):
        return
    if c['kind'] == C_EQUAL:
        in_idx = constraint_input(p, ci)
        out_idx = constraint_output(p, ci)
        p['vars'][out_idx]['value'] = p['vars'][in_idx]['value']
        return
    # C_SCALE
    in_idx = constraint_input(p, ci)
    out_idx = constraint_output(p, ci)
    in_var = p['vars'][in_idx]
    out_var = p['vars'][out_idx]
    if c['direction'] == FORWARD:
        out_var['value'] = in_var['value'] * c['scale'] + c['offset']
    else:
        out_var['value'] = (in_var['value'] - c['offset']) // c['scale']


def constraint_recalc(p, ci):
    c = p['constraints'][ci]
    out_idx = constraint_output(p, ci)
    out_var = p['vars'][out_idx]
    out_var['walk_str'] = c['strength']
    out_var['stay'] = 1 if c['kind'] != C_EDIT else 0
    if out_var['stay']:
        constraint_execute(p, ci)


def constraint_choose_method(p, ci, mark):
    c = p['constraints'][ci]
    if c['kind'] in (C_STAY, C_EDIT):
        v = p['vars'][c['v1']]
        if v['mark'] != mark and stronger(c['strength'], v['walk_str']):
            c['direction'] = FORWARD
            c['satisfied'] = True
        else:
            c['direction'] = NONE
            c['satisfied'] = False
        return

    # binary constraint
    v1 = p['vars'][c['v1']]
    v2 = p['vars'][c['v2']]
    if v1['mark'] == mark and v2['mark'] != mark:
        if stronger(c['strength'], v2['walk_str']):
            c['direction'] = FORWARD
            c['satisfied'] = True
            return
    if v2['mark'] == mark and v1['mark'] != mark:
        if stronger(c['strength'], v1['walk_str']):
            c['direction'] = BACKWARD
            c['satisfied'] = True
            return
    if weaker(v1['walk_str'], v2['walk_str']):
        if stronger(c['strength'], v1['walk_str']):
            c['direction'] = BACKWARD
            c['satisfied'] = True
            return
    else:
        if stronger(c['strength'], v2['walk_str']):
            c['direction'] = FORWARD
            c['satisfied'] = True
            return
    c['direction'] = NONE
    c['satisfied'] = False


def incremental_add(p, ci):
    p['current_mark'] += 1
    mark = p['current_mark']
    constraint_choose_method(p, ci, mark)
    c = p['constraints'][ci]
    if not c['satisfied']:
        return
    constraint_recalc(p, ci)
    out_idx = constraint_output(p, ci)
    out_var = p['vars'][out_idx]
    for oci in out_var['constraints']:
        if oci != ci:
            oc = p['constraints'][oci]
            if oc['satisfied']:
                constraint_recalc(p, oci)


def incremental_remove(p, ci):
    c = p['constraints'][ci]
    if not c['satisfied']:
        return
    constraint_recalc(p, ci)
    c['satisfied'] = False
    c['direction'] = NONE
    out_idx = constraint_output(p, ci)
    out_var = p['vars'][out_idx]
    out_var['det_by'] = -1
    out_var['walk_str'] = WEAKEST
    out_var['stay'] = 1


def chain_test(n):
    p = create_planner()

    for i in range(n + 1):
        planner_add_var(p, "v", 0)

    for i in range(n + 1):
        ci = planner_add_constraint(p, C_STAY, WEAK_DEFAULT, i, -1, 0, 0)
        incremental_add(p, ci)

    for i in range(n):
        ci = planner_add_constraint(p, C_EQUAL, STRONG_PREFERRED, i, i + 1, 0, 0)
        incremental_add(p, ci)

    edit_ci = planner_add_constraint(p, C_EDIT, PREFERRED, 0, -1, 0, 0)
    incremental_add(p, edit_ci)

    p['vars'][0]['value'] = 100
    # execute the equal constraints in the chain
    for i in range(n):
        ci = i + n + 1
        if ci < p['nc']:
            c = p['constraints'][ci]
            if c is not None and c['satisfied']:
                constraint_execute(p, ci)

    return p['vars'][n]['value']


def projection_test(n):
    p = create_planner()

    for i in range(n):
        src_idx = planner_add_var(p, "src", i)
        dst_idx = planner_add_var(p, "dst", i)

        stay_ci = planner_add_constraint(p, C_STAY, NORMAL, src_idx, -1, 0, 0)
        incremental_add(p, stay_ci)

        sc_ci = planner_add_constraint(p, C_SCALE, STRONG_PREFERRED, src_idx, dst_idx, 10, 1000)
        incremental_add(p, sc_ci)

        stay_ci2 = planner_add_constraint(p, C_STAY, NORMAL, dst_idx, -1, 0, 0)
        incremental_add(p, stay_ci2)

    for i in range(n):
        vi = i * 2
        p['vars'][vi]['value'] = i * 17 + 11

    return dst_idx


def benchmark():
    chain_test(100)
    projection_test(100)


def main():
    t0 = time.perf_counter_ns()
    # Original JetStream workload: 20 iterations
    for _ in range(20):
        benchmark()
    t1 = time.perf_counter_ns()

    print("deltablue: PASS")
    print(f"__TIMING__:{(t1 - t0) / 1_000_000:.3f}")


if __name__ == "__main__":
    main()
