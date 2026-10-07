"""Shared runner for the compiled native Java and Erlang benchmark ports."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile
import time

import run_julia_benchmarks as contract

ROOT = contract.PROJECT_ROOT
BASE = ROOT / 'test/benchmark'
_CACHE = {}


def executable(language, compiler=False):
    name = ('javac' if compiler else 'java') if language == 'java' else ('erlc' if compiler else 'erl')
    configured = os.environ.get(name.upper() + '_EXE')
    if configured:
        return shutil.which(configured)
    # macOS's /usr/bin/java can be a launcher without an installed system JDK.
    for root in (Path(os.environ.get('JAVA_HOME', '/nonexistent')) / 'bin',
                 Path('/opt/homebrew/opt/openjdk/bin') if language == 'java' else Path('/opt/homebrew/opt/erlang/bin')):
        candidate = root / name
        if candidate.is_file():
            return str(candidate)
    return shutil.which(name)


def source_files(language):
    extension = '*.java' if language == 'java' else '*.erl'
    files = sorted((BASE / language / 'native').rglob(extension))
    if language == 'erlang':
        files += sorted((BASE / language / 'native').rglob('*.hrl'))
    if language == 'java':
        # Compile upstream AWFY sources unchanged, alongside the direct ports.
        files += sorted((ROOT / 'ref/are-we-fast-yet/benchmarks/Java/src').rglob('*.java'))
    return files


def port_source(language, suite, name):
    manifest = BASE / 'native_ports/manifest.json'
    entries = json.loads(manifest.read_text())['entries']
    entry = entries.get(f'{suite}/{name}')
    if not entry or language not in entry:
        return None
    source = BASE / entry[language]
    return source if source.is_file() else None


def environment():
    warmup = int(os.environ.get('NATIVE_BENCH_WARMUP', '1'))
    if warmup not in (0, 1):
        raise ValueError('NATIVE_BENCH_WARMUP must be 0 or 1')
    return {'TMPDIR': str(ROOT / 'temp'), 'NATIVE_BENCH_WARMUP': str(warmup),
            'ERL_CRASH_DUMP': str(ROOT / 'temp/erlang-benchmark-crash.dump')}


def build(language):
    files = source_files(language)
    compiler = executable(language, True)
    if not executable(language) or not compiler:
        raise FileNotFoundError(f'{language} toolchain is missing')
    version = toolchain_version(language)
    sources = [(p, p.read_bytes()) for p in files]
    digest = hashlib.sha256((compiler + version + 'direct-native-v1-java17').encode()
        + b''.join(str(p.relative_to(ROOT)).encode() + data for p, data in sources)).hexdigest()
    directory = ROOT / f'temp/{language}-benchmarks' / digest[:16]
    stamp = directory / 'built.json'
    if _CACHE.get(language) == directory or stamp.is_file():
        _CACHE[language] = directory
        return directory
    directory.parent.mkdir(parents=True, exist_ok=True)
    # Compile a frozen snapshot and publish only a complete cache directory.
    with tempfile.TemporaryDirectory(prefix='compile-', dir=directory.parent) as scratch:
        staging = Path(scratch)
        output = staging / 'output'
        output.mkdir()
        for source, data in sources:
            destination = staging / source.relative_to(ROOT)
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(data)
        command = ([compiler, '--release', '17', '-d', str(output)] if language == 'java' else [compiler, '-W0', '-o', str(output)])
        proc = subprocess.run(command + [str(staging / p.relative_to(ROOT)) for p in files if p.suffix != ".hrl"], cwd=ROOT, capture_output=True, text=True)
        if proc.returncode:
            raise RuntimeError(f'{language} compilation failed:\n{proc.stdout}\n{proc.stderr}')
        (output / 'built.json').write_text(json.dumps({'source_sha256': digest, 'command': command,
            'files_sha256': {str(p.relative_to(ROOT)): hashlib.sha256(data).hexdigest() for p, data in sources}}) + '\n')
        try:
            output.rename(directory)
        except OSError:
            if not stamp.is_file():
                raise RuntimeError(f'incomplete {language} build cache: {directory}')
    _CACHE[language] = directory
    return directory


def build_command(language, suite, name, directory=None):
    if directory is None:
        directory = build(language)
    arguments = [f'{suite}/{name}']
    import run_benchmarks as registry
    if suite == 'awfy':
        outer, inner = registry.awfy_node_iterations(name)
        arguments += [str(inner), str(outer)]
    elif suite == 'jetstream':
        _, count = registry._detect_jetstream_run_function(registry.JETSTREAM_NODE[name])
        arguments.append(str(count))
    if language == 'java':
        return [executable(language), '-Xss16m', '-Xmx2g', '-cp', str(directory), 'NativeBench'] + arguments
    return [executable(language), '+S', '1:1', '+A', '1', '-noshell', '-pa', str(directory), '-s', 'native_bench', 'main', '-extra'] + arguments


def shell_command(language, suite, name):
    if port_source(language, suite, name) is None:
        return None, 'missing_port'
    try:
        command = build_command(language, suite, name)
    except FileNotFoundError:
        return None, 'toolchain_missing'
    except RuntimeError:
        return None, 'build_failed'
    env = ' '.join(f'{key}={shlex.quote(value)}' for key, value in environment().items())
    return 'env ' + env + ' ' + shlex.join(command), 'ok'


def toolchain_version(language):
    exe = executable(language)
    if not exe:
        return ''
    args = ['-version'] if language == 'java' else ['+S', '1:1', '+A', '1', '-noshell', '-eval', 'io:format("~s", [erlang:system_info(system_version)]), halt().']
    proc = subprocess.run([exe] + args, capture_output=True, text=True, timeout=30)
    return (proc.stdout + proc.stderr).strip()


def runtime_metadata(language):
    exe = executable(language)
    version = toolchain_version(language) or None
    compiler = executable(language, True)
    compiled = {}
    build_status = 'toolchain_missing'
    if exe and compiler:
        try:
            directory = build(language)
            compiled = {str(p.relative_to(ROOT)): contract.sha256(p) for p in sorted(directory.rglob('*'))
                        if p.suffix in ('.class', '.beam')}
            build_status = 'ok'
        except (FileNotFoundError, RuntimeError):
            build_status = 'build_failed'
    paths = source_files(language) + [BASE / 'native_ports/manifest.json',
            BASE / 'native_benchmark_runner.py', BASE / f'run_{language}_benchmarks.py']
    fixtures = sorted((BASE / 'text').glob('*.json')) + sorted(p for p in (BASE / 'text/jq').iterdir() if p.suffix in ('.jq', '.bf', '.json'))
    fixtures += sorted((BASE / 'native_ports/fixtures').iterdir())
    fixtures += [BASE / 'beng/input/fasta_1000.txt', BASE / 'julia/expected.json', BASE / 'julia/SUITE.md']
    fixtures += sorted((BASE / 'beng').glob('*.txt'))
    return {'executable': exe, 'version': version,
            'executable_sha256': contract.sha256(exe) if exe else None,
            'compiler': compiler, 'build_status': build_status, 'compiled_sha256': compiled,
            'flags': ['-Xss16m', '-Xmx2g'] if language == 'java' else ['+S', '1:1', '+A', '1', '-noshell'],
            'warmup_runs': int(environment()['NATIVE_BENCH_WARMUP']),
            'suite_warmup_runs': {'julia': 1},
            'implementation': 'direct native functions and data structures; no generated language adapter',
            'reference_sources_sha256': json.loads((BASE/'native_ports/manifest.json').read_text())['reference_sources_sha256'],
            'timing': 'workload execution; excludes source compilation, process startup, warmup and verification',
            'sources_sha256': {str(p.relative_to(ROOT)): contract.sha256(p) for p in paths},
            'fixtures_sha256': {str(p.relative_to(ROOT)): contract.sha256(p) for p in fixtures}}


def main(language):
    parser = argparse.ArgumentParser(description=f'Run native {language} benchmark ports')
    parser.add_argument('--suite', action='append')
    parser.add_argument('--bench', action='append')
    parser.add_argument('--all', action='store_true', help='include noncanonical duplicates')
    parser.add_argument('--list', action='store_true')
    parser.add_argument('--verify-node', action='store_true')
    parser.add_argument('--timeout', type=float, default=600, help='hard timeout in seconds for each process')
    parser.add_argument('--warmup', type=int, choices=(0, 1))
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    if args.warmup is not None:
        os.environ['NATIVE_BENCH_WARMUP'] = str(args.warmup)
    entries = [e for e in contract.benchmark_entries(args.all)
               if (not args.suite or e['suite'] in args.suite) and (not args.bench or e['name'] in args.bench)]
    if not entries:
        parser.error('no matching benchmarks')
    if args.list:
        for e in entries:
            print(f"{e['suite']}/{e['name']}" + (' (missing port)' if port_source(language, e['suite'], e['name']) is None else ''))
        return 0
    try:
        directory = build(language)
    except (FileNotFoundError, RuntimeError) as exc:
        parser.error(str(exc))
    if args.verify_node:
        import run_benchmarks as registry
        registry.require_pinned_node_version(['nodejs'], 'time')
    env = os.environ.copy()
    env.update(environment())
    metadata = runtime_metadata(language)
    records = []
    for e in entries:
        record = {'suite': e['suite'], 'name': e['name']}
        try:
            command = build_command(language, e['suite'], e['name'], directory)
            start = time.perf_counter_ns()
            proc = subprocess.run(command, cwd=ROOT, env=env, capture_output=True, text=True, timeout=args.timeout)
            record.update(status=contract.output_status(proc), wall_ms=(time.perf_counter_ns()-start)/1e6,
                          stdout=proc.stdout, stderr=proc.stderr, command=command)
            if record['status'] == 'ok':
                record['exec_ms'] = float(contract.TIMING_RE.search(proc.stdout).group(1))
                if args.verify_node:
                    ref = subprocess.run(contract.node_command(e), cwd=ROOT, capture_output=True, text=True, timeout=args.timeout)
                    record.update(node_stdout=ref.stdout, node_stderr=ref.stderr)
                    if contract.output_status(ref) != 'ok':
                        record['status'] = 'node_reference_failed'
                    elif contract.normalized_output(proc.stdout) != contract.normalized_output(ref.stdout):
                        record['status'] = 'node_output_mismatch'
        except subprocess.TimeoutExpired:
            record['status'] = 'timeout'
        records.append(record)
        print(f"{e['suite']}/{e['name']:<20} {record['status']}", flush=True)
        if record['status'] != 'ok':
            print((record.get('stderr') or record.get('stdout') or '')[:2000], flush=True)
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(json.dumps({language: metadata, 'verify_node': args.verify_node, 'records': records}, indent=2)+'\n')
    passed = sum(r['status'] == 'ok' for r in records)
    print(f'{passed}/{len(records)} {language} benchmarks passed')
    return 0 if passed == len(records) else 1
