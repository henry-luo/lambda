from pathlib import Path
import sys, json, hashlib, statistics
sys.path.insert(0, 'test/benchmark')
from run_paired_benchmarks import run_once
from run_benchmarks import check_release_build

binary = str(Path(sys.argv[1]).resolve())
check_release_build(exe_path=binary)
groups = {
    'primes': ['test/benchmark/kostya/primes.ls', 'test/benchmark/tune32/diagnostic_sources/primes_array_only.ls', 'test/benchmark/kostya/primes2.ls'],
    'fast_diff': ['test/benchmark/text/fast_diff.ls', 'test/benchmark/tune32/diagnostic_sources/fast_diff_strings_only.ls', 'test/benchmark/text/fast_diff2.ls'],
    'levenshtein': ['test/benchmark/kostya/levenshtein.ls', 'test/benchmark/tune32/diagnostic_sources/levenshtein_strings_only.ls', 'test/benchmark/kostya/levenshtein2.ls'],
    'divrec': ['test/benchmark/larceny/divrec.ls', 'test/benchmark/larceny/divrec2.ls'],
    'leven_decomposition': ['test/benchmark/tune32/diagnostic_sources/levenshtein_strings_only.ls'] + ['test/benchmark/tune32/diagnostic_sources/levenshtein_' + x + '.ls' for x in ['arrays', 'scalars', 'helpers', 'remaining']] + ['test/benchmark/kostya/levenshtein2.ls'],
}
report = {'binary': binary, 'sha256': hashlib.sha256(Path(binary).read_bytes()).hexdigest(), 'tier': 'jit', 'groups': {}}
for name, sources in groups.items():
    rows = {s: {'sha256': hashlib.sha256(Path(s).read_bytes()).hexdigest(), 'samples': []} for s in sources}
    for s in sources:
        warm = run_once(binary, str(Path(s).resolve()), 120)
        assert warm['status'] == 'ok', warm
    for i in range(12):
        order = sources[i % len(sources):] + sources[:i % len(sources)]
        if i % 2: order = order[::-1]
        for s in order:
            rows[s]['samples'].append(run_once(binary, str(Path(s).resolve()), 120))
    hashes = set()
    for s, row in rows.items():
        assert all(x['status'] == 'ok' for x in row['samples']), row
        hashes.update(x['stdout_sha256'] for x in row['samples'])
        row['median_ms'] = statistics.median(x['exec_ms'] for x in row['samples'])
    assert len(hashes) == 1, (name, hashes)
    report['groups'][name] = rows
    print(name, [(Path(s).name, row['median_ms']) for s, row in rows.items()], flush=True)
Path(sys.argv[2]).write_text(json.dumps(report, indent=2) + '\n')
