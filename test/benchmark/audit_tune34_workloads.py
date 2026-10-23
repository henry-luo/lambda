"""Tune34 output and matched-work audit; generated variants never replace canonical rows.

S9.1.2 preserves FFT's plain-parameter snapshot. S4.2 preserves each JSON
port's rounding until the explicitly named floor-aligned diagnostic.
"""
import argparse
import cmath
import hashlib
import json
import math
import os
from pathlib import Path
import subprocess

import run_benchmarks as registry


ROOT = Path(__file__).resolve().parents[2]
PILOTS = {'puzzle', 'splay', 'crypto_sha1', 'brainfuck', 'json_gen',
          'levenshtein', 'navier_stokes', 'fft'}


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def replace_once(source, old, new):
    if source.count(old) != 1:
        raise ValueError('source shape changed: ' + old[:70])
    return source.replace(old, new, 1)


def definitions(path, marker='pn main()'):
    source = Path(path).read_text()
    if source.count(marker) != 1:
        raise ValueError('missing unique entry in ' + str(path))
    return source.split(marker)[0]


def run_script(command, path, output, label, backend=None):
    env = os.environ.copy()
    if backend:
        env.update(LAMBDA_EXEC_BACKEND=backend, LAMBDA_TIER=backend)
    if command[-1] == 'js':
        env['JS_EXEC_BACKEND'] = 'mir'
    proc = subprocess.run([*command, str(path)], env=env, capture_output=True,
                          text=True, timeout=180, cwd=ROOT)
    (output / (label + '.stdout')).write_text(proc.stdout)
    (output / (label + '.stderr')).write_text(proc.stderr)
    if proc.returncode:
        raise RuntimeError(label + ': ' + proc.stderr[-1000:])
    return proc.stdout


def value_line(stdout, prefix='__VALUE__:'):
    values = [line[len(prefix):] for line in stdout.splitlines() if line.startswith(prefix)]
    if len(values) != 1:
        raise ValueError('missing unique output marker ' + prefix)
    return values[0]


def json_content(args, output):
    records = []
    js = definitions(ROOT / 'test/benchmark/kostya/json_gen.js', 'function main()')
    js = replace_once(js, 'return json.length;', 'return json;')
    for variant, filename in [('untyped', 'json_gen.ls'), ('typed', 'json_gen2.ls')]:
        source = definitions(ROOT / 'test/benchmark/kostya' / filename)
        source = replace_once(source, 'return len(json)', 'return json')
        path = output / ('json_content_' + variant + '.ls')
        path.write_text(source + 'pn main() { print("__VALUE__:" ++ benchmark()); print("\\n") }\n')
        values = []
        for side in ('control', 'candidate'):
            text = run_script([getattr(args, side), 'run'], path, output,
                              'json_' + variant + '_' + side, 'jit')
            values.append(value_line(text))
        assert values[0] == values[1], variant
        parsed = json.loads(values[0])
        assert len(parsed) == 1000
        record = {'variant': variant, 'script_sha256': digest(path),
                  'engine_ab_full_content_equal': True,
                  'content_sha256': hashlib.sha256(values[0].encode()).hexdigest()}
        for rounding in ('trunc', 'floor'):
            script = output / ('json_content_' + rounding + '.js')
            script.write_text(js.replace('Math.trunc', 'Math.' + rounding) +
                              'console.log("__VALUE__:" + benchmark());\n')
            text = run_script(['node'], script, output, 'json_node_' + rounding)
            other = json.loads(value_line(text))
            differences = sum(left != right for left, right in zip(parsed, other))
            record['node_' + rounding + '_different_objects'] = differences
            if rounding == 'floor':
                assert parsed == other
        records.append(record)
    return records


def fft_outputs(args, output):
    records = []
    for variant, filename in [('untyped', 'fft.ls'), ('typed', 'fft2.ls')]:
        source = definitions(ROOT / 'test/benchmark/r7rs' / filename, 'pn benchmark()')
        end = source.rfind('\n}')
        source = source[:end] + '\n    return data' + source[end:]
        for count in (4, 8, 16):
            values = [float(i + 1) if part == 0 else 0.0
                      for i in range(count) for part in range(2)]
            expected = [sum((i + 1) * cmath.exp(2j * math.pi * i * k / count)
                            for i in range(count)) for k in range(count)]
            flattened = [part for value in expected for part in (value.real, value.imag)]
            path = output / ('fft_nonzero_' + variant + '_' + str(count) + '.ls')
            path.write_text(source + '''
pn main() {
    let original = VALUES
    let transformed = four1(original, SIZE)
    let expected = EXPECTED
    var good = true
    var k = 0
    while (k < SIZE) {
        if (math.abs(transformed[k] - expected[k]) > 0.000000001) { good = false }
        k = k + 1
    }
    print("__CHECK__:"); print([good, original == VALUES]); print("\\n")
}
'''.replace('VALUES', json.dumps(values)).replace('EXPECTED', json.dumps(flattened))
                .replace('SIZE', str(2 * count)))
            for side in ('control', 'candidate'):
                text = run_script([getattr(args, side), 'run'], path, output,
                                  'fft_' + variant + '_' + str(count) + '_' + side, 'jit')
                assert json.loads(value_line(text, '__CHECK__:')) == [True, True], (variant, count, side)
            records.append({'variant': variant, 'complex_samples': count,
                            'script_sha256': digest(path), 'dft_absolute_tolerance': 1e-9,
                            'returned_transform_matches_dft': True, 'caller_snapshot_unchanged': True})
    return records


def splay_work(args, output):
    path = ROOT / 'test/benchmark/jetstream/splay.ls'
    source = definitions(path)
    source = replace_once(source, '    var tree = splay_tree_new()',
                          '    let setup0 = clock()\n    var tree = splay_tree_new()')
    source = replace_once(source, '    // Run: do TREE_MODIFICATIONS',
                          '    let setup1 = clock()\n    // Run: do TREE_MODIFICATIONS')
    source = replace_once(source, '    // Verify: should still have TREE_SIZE nodes',
                          '    let mods1 = clock()\n    // Verify: should still have TREE_SIZE nodes')
    source = replace_once(source, '    var node_count = count_nodes(tree.root)\n    return node_count', '''
    var keys = fill(TREE_SIZE, 0.0)
    let node_count = traverse_keys(tree.root, keys, 0)
    var words = fill(TREE_SIZE, 0)
    var k = 0
    var valid = audit_node(tree.root)
    while (k < TREE_SIZE) {
        words[k] = int(keys[k] * 4294967296.0)
        if (k > 0 and keys[k - 1] >= keys[k]) { valid = false }
        k = k + 1
    }
    let verify1 = clock()
    k = 0
    while (k < TREE_SIZE) {
        print("__KEYS__:"); print(slice(words, k, min(k + 128, TREE_SIZE))); print("\\n")
        k = k + 128
    }
    print("__CHECK__:"); print([node_count == TREE_SIZE, valid]); print("\\n")
    print("__PHASE_MS__:"); print([(setup1-setup0)*1000.0, (mods1-setup1)*1000.0,
        (verify1-mods1)*1000.0, (verify1-setup0)*1000.0]); print("\\n")
    return node_count''')
    source += '''
pn audit_payload(value, depth: int, key) {
    if (depth == 0) { return value.str == key and value.arr == [0,1,2,3,4,5,6,7,8,9] }
    return audit_payload(value.left_p, depth-1, key) and audit_payload(value.right_p, depth-1, key)
}
pn audit_node(node) {
    if (node == null) { return true }
    return audit_payload(node.value, 5, node.key) and audit_node(node.left) and audit_node(node.right)
}
pn main() { run_splay() }
'''
    generated = output / 'splay_matched.ls'
    generated.write_text(source)
    js = (ROOT / 'test/benchmark/jetstream/splay.js').read_text()
    js = replace_once(js, '\nSplaySetup();', '\n')
    js = replace_once(js, 'GeneratePayloadTree(kSplayTreePayloadDepth, String(key))',
                      'GeneratePayloadTree(kSplayTreePayloadDepth, key)')
    start = js.index('function GeneratePayloadTree(')
    end = js.index('var splaySamples', start)
    # Replace definitions in the generated port. Reassigning function bindings
    # would introduce indirect-call costs absent from the canonical program.
    js = js[:start] + '''
var auditSeed = 49734321;
function GenerateKey() { auditSeed = (Math.imul(auditSeed,1103515245)+12345)>>>0; return auditSeed/4294967296; }
function GeneratePayloadTree(depth,tag) { return depth === 0 ? {arr:[0,1,2,3,4,5,6,7,8,9],str:tag} :
    {left_p:GeneratePayloadTree(depth-1,tag),right_p:GeneratePayloadTree(depth-1,tag)}; }
''' + js[end:]
    js += '''
function auditArray(a) { if(a.length!==10) return false; for(var i=0;i<10;i++) if(a[i]!==i) return false; return true; }
function auditPayload(v,d,k) { return d === 0 ? v.str === k && auditArray(v.arr) :
    auditPayload(v.left_p,d-1,k) && auditPayload(v.right_p,d-1,k); }
function auditNode(n) { return !n || auditPayload(n.value,5,n.key) && auditNode(n.left) && auditNode(n.right); }
var t0=performance.now(); SplaySetup(); var t1=performance.now();
for(var i=0;i<50;i++) SplayRun(); var t2=performance.now();
var keys=splayTree.exportKeys(), valid=keys.length===8000 && auditNode(splayTree.root_), words=[];
for(var i=0;i<keys.length;i++) { if(i>0 && keys[i-1]>=keys[i]) valid=false;
    words.push(keys[i]*4294967296); }
var t3=performance.now();
for(var i=0;i<words.length;i+=128) console.log('__KEYS__:'+JSON.stringify(words.slice(i,i+128)));
console.log('__CHECK__:'+JSON.stringify([keys.length===8000,valid]));
console.log('__PHASE_MS__:'+JSON.stringify([t1-t0,t2-t1,t3-t2,t3-t0]));
'''
    generated_js = output / 'splay_matched.js'
    generated_js.write_text(js)
    results, keys_seen = {}, []
    lanes = [(side, [getattr(args, side), 'run'], generated, 'jit') for side in ('control', 'candidate')]
    lanes += [('node', ['node'], generated_js, None), ('lambda_js', [args.candidate, 'js'], generated_js, None)]
    for label, command, script, backend in lanes:
        text = run_script(command, script, output, 'splay_matched_' + label, backend)
        assert json.loads(value_line(text, '__CHECK__:')) == [True, True], label
        keys_seen.append([key for line in text.splitlines() if line.startswith('__KEYS__:')
                          for key in json.loads(line[len('__KEYS__:'):])])
        assert len(keys_seen[-1]) == 8000, label
        results[label] = {'phase_ms': json.loads(value_line(text, '__PHASE_MS__:')),
                          'script_sha256': digest(script)}
    assert all(keys == keys_seen[0] for keys in keys_seen)
    return {'same_complete_sorted_keys_and_payloads': True, 'nodes': 8000, 'modifications': 4000,
            'seed': 49734321, 'payload_depth': 5, 'lanes': results,
            'timing_qualification': 'single diagnostic sample; recursive value versus top-down reference algorithms remain different'}


def levenshtein_work(args, output):
    records = {}
    for variant, filename in [('untyped', 'levenshtein.ls'), ('typed', 'levenshtein2.ls')]:
        source = definitions(ROOT / 'test/benchmark/kostya' / filename)
        source = source.split('    print("levenshtein: d(')[0]
        path = output / ('levenshtein_aligned_' + variant + '.ls')
        path.write_text(source + '''    return [d1, d2, d3, d4]
}
pn main() {
    let start = clock()
    let distances = benchmark()
    let elapsed = (clock() - start) * 1000.0
    print("__VALUE__:"); print(distances); print("\\n")
    print("__TIMING__:" ++ elapsed); print("\\n")
}
''')
        for side in ('control', 'candidate'):
            text = run_script([getattr(args, side), 'run'], path, output,
                              'levenshtein_' + variant + '_' + side, 'jit')
            assert json.loads(value_line(text)) == [3, 3, 500, 2]
            records[variant + '_' + side] = {'script_sha256': digest(path), 'distances': [3, 3, 500, 2]}
    js_path = ROOT / 'test/benchmark/kostya/levenshtein.js'
    original = js_path.read_text()
    source = definitions(js_path, 'function main()')
    measured = original.split('function main() {')[1].split('    const __t1')[0]
    path = output / 'levenshtein_aligned.js'
    path.write_text(source + 'function main() {' + measured + '''
    const __t1 = performance.now();
    console.log('__VALUE__:'+JSON.stringify([d1,d2,d3,d4]));
    console.log('__TIMING__:'+(__t1-__t0));
}
main();
''')
    for label, command in [('node', ['node']), ('lambda_js', [args.candidate, 'js'])]:
        text = run_script(command, path, output, 'levenshtein_' + label)
        assert json.loads(value_line(text)) == [3, 3, 500, 2]
        records[label] = {'script_sha256': digest(path), 'distances': [3, 3, 500, 2]}
    return {'output_after_timer_in_all_lanes': True, 'lanes': records}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--control', required=True)
    parser.add_argument('--candidate', required=True)
    parser.add_argument('--output-dir', default='temp/tune34/workload_audit')
    args = parser.parse_args()
    args.control, args.candidate = str(Path(args.control).resolve()), str(Path(args.candidate).resolve())
    output = (ROOT / args.output_dir).resolve()
    output.mkdir(parents=True, exist_ok=True)
    rows = []
    for bench in registry.build_benchmark_list(None, sorted(PILOTS)):
        untyped, typed = registry.mir_script_variants(bench)
        js = registry.JETSTREAM_LJS.get(bench['name']) if bench['is_jetstream'] else bench['js_path']
        rows.append({'suite': bench['suite'], 'name': bench['name'],
                     'source_hashes': {str(path): digest(path) for path in (untyped, typed, js)}})
    assert len(rows) == 8
    result = {'canonical_sources_unchanged': True, 'pilot_sources': rows,
              'binary_hashes': {side: digest(getattr(args, side)) for side in ('control', 'candidate')}}
    result['json_full_content'] = json_content(args, output)
    result['fft_nonzero'] = fft_outputs(args, output)
    result['splay_matched'] = splay_work(args, output)
    result['levenshtein_aligned'] = levenshtein_work(args, output)
    result['capture_complete'] = True
    (output / 'audit.json').write_text(json.dumps(result, indent=2) + '\n')
    print('Tune34 JSON, FFT, matched Splay and aligned Levenshtein checks passed')


if __name__ == '__main__':
    main()
