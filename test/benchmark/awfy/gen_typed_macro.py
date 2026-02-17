#!/usr/bin/env python3
"""Generate typed versions of macro AWFY benchmarks.
Only adds : int to loop counters and simple integer state variables.
Does NOT add : float to anything (bug #21 with array/map access).
"""
import re, os

DIR = os.path.dirname(os.path.abspath(__file__))

def add_int_types(code, filename):
    """Add : int annotations to safe integer variables."""
    lines = code.split('\n')
    result = []

    # Patterns for variables that are safe to type as int
    # var name = <integer_literal>
    # var name = <integer_literal> (where followed by int arithmetic)
    int_init_re = re.compile(r'^(\s*var\s+)(\w+)(\s*=\s*)(-?\d+)\s*$')
    # var name = expr % number  or  var name = expr + number  etc (likely int)

    for line in lines:
        m = int_init_re.match(line)
        if m:
            prefix, varname, eq, value = m.groups()
            # Skip variables that hold references, arrays, or floats
            skip_vars = {'arr', 'extra', 'rest', 'flags', 'piles', 'tops',
                        'seed_arr', 'bx', 'by', 'bz', 'bvx', 'bvy', 'bvz', 'bmass',
                        'result', 'work', 'message', 'pkt', 'tcb', 'queue_head',
                        'mouse', 'lnk', 't', 'ct', 'packet', 'workq',
                        'idle_data', 'worker_data', 'handler_data_a', 'handler_data_b',
                        'device_data_a', 'device_data_b', 'sched', 'task_table',
                        'e', 'dx', 'dy', 'dz', 'distance', 'd_squared', 'mag',
                        'ke', 'px', 'py', 'pz', 'r', 'cr', 'ci', 'zr', 'zi',
                        'zrzr', 'zizi', 'x_tail', 'y_tail', 'node',
                        'rec', 'tl', 'nxt', 'new_input', 'inp', 'inp2',
                        'msg_link', 'hnd', 'fw', 'pend', 'dev_pkt',
                        'work_pkt', 'dl', 'wl', 'wi', 'di', 'nwi', 'ndi',
                        'wd', 'v', 'tmp', 'state', 'free_rows', 'free_maxs',
                        'free_mins', 'queen_rows', 'old_task',
                        }
            if varname not in skip_vars:
                line = f"{prefix}{varname}: int{eq}{value}"
        result.append(line)

    return '\n'.join(result)


for name in ['richards', 'json', 'deltablue', 'havlak', 'cd']:
    src_path = os.path.join(DIR, f'{name}.ls')
    dst_path = os.path.join(DIR, f'{name}2.ls')

    with open(src_path, 'r') as f:
        code = f.read()

    # Add typed comment
    code = code.replace(f'// AWFY Benchmark: ', f'// AWFY Benchmark: ', 1)

    typed_code = add_int_types(code, name)

    with open(dst_path, 'w') as f:
        f.write(typed_code)

    # Count changes
    orig_lines = code.split('\n')
    new_lines = typed_code.split('\n')
    changes = sum(1 for a, b in zip(orig_lines, new_lines) if a != b)
    print(f"  {name}2.ls: {changes} vars typed")

print("\nDone")
