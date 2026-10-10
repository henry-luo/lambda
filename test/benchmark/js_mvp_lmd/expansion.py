#!/usr/bin/env python3
"""Capture MVP expansion phases with frozen workloads and exact release controls."""
import argparse
import datetime
import hashlib
import itertools
import json
import os
import platform
from pathlib import Path
import re
import shutil
import statistics
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'test/benchmark'))
from run_benchmarks import (build_benchmark_list, check_release_build, expand_benchmark_js,
    _detect_jetstream_run_function, jetstream_timing_trailer, mir_script_variants,
    NAVIER_STOKES_DENSITY_ORACLE_V1)
from run_paired_benchmarks import run_once, paired_ratio_bootstrap, normalized_stdout
from closures import sha

TARGETS = ('awfy/deltablue', 'awfy/json', 'text/text_search', 'text/three_way_merge',
    'text/log_pipeline', 'julia/parse_integers', 'julia/iteration_pi_sum',
    'julia/matrix_statistics', 'awfy/havlak', 'beng/fasta', 'beng/revcomp',
    'beng/knucleotide', 'julia/formatted_output', 'jetstream/cube3d',
    'jetstream/navier_stokes', 'jetstream/splay')
CONTROL_SHA = '5f0ce0ed8d41059e76a65ac9f972f95c0daed9d42ccedb1b419c0c779d10ce16'
LIBRARY_TARGETS = ('jetstream/raytrace3d', 'text/prettier_ast', 'beng/pidigits',
    'beng/regexredux', 'text/fast_diff', 'text/hyphen', 'text/microdiff',
    'text/jq_mix', 'text/jq_records', 'text/jq_bf', 'text/jq_tree')
LIBRARY_CONTROL_SHA = '990bae9b78b747f85e8ec5053a15a3757c7b5f8f0ac3f868293dcd96dd7f876b'
FLAGS = dict(JS_EXEC_BACKEND='mir', JS_EXECUTION_BACKEND='mir', JS_MIR_INTERP='0',
    LAMBDA_JS_LARGE_INTERP='0', LAMBDA_EXEC_BACKEND='jit', LAMBDA_TIER='jit')


def prepare_new(out, binary, references, targets=TARGETS, population='new16'):
    specs = {r['suite'] + '/' + r['name']: r
             for r in build_benchmark_list(None, None, include_text=True)}
    rows = []
    inventory = {row['id']: row for row in json.loads(
        (ROOT / 'test/benchmark/js_mvp_lmd_manifest_v1.json').read_text())['workloads']}
    for identifier in targets:
        spec = specs[identifier]
        original = Path(spec['js_path'] or spec['ref_js'])
        if spec['suite'] == 'awfy':
            original = Path(str(original).replace('2.js', '2_bundle.js'))
        expanded = Path(expand_benchmark_js(str(original)))
        code = expanded.read_text()
        contract = 'canonical script-defined timing, setup and oracle'
        pending = []
        fixture = None
        if spec['is_jetstream']:
            entry, count = _detect_jetstream_run_function(str(original))
            oracle = NAVIER_STOKES_DENSITY_ORACLE_V1 if spec['name'] == 'navier_stokes' else None
            code += jetstream_timing_trailer(entry, count, oracle)
            contract = f'canonical {entry}, {count} repetitions; setup outside timer'
            if spec['name'] == 'splay':
                code += '\nSplayTearDown();\n'
                fixture = Path('test/benchmark/octane/base.js')
                rng = fixture.read_text().split('  Math.random = ', 1)[1].split('  })();', 1)[0] + '  })()'
                code = 'var Math = { random: ' + rng + ' };\n' + code
                contract += '; Octane Jenkins RNG seed 49734321, shared lexical Math binding' 
            code += '\nconsole.log("__ORACLE__:PASS");\n'
        script = out / (identifier.replace('/', '_') + '.js')
        script.write_text(code)
        sources = [{'original': str(original), 'sha256': sha(original)},
                   {'frozen': str(script), 'sha256': sha(script)}]
        if fixture:
            sources.append({'original': str(fixture), 'sha256': sha(fixture)})
        if expanded != original:
            helper = original.parent / original.read_text().splitlines()[0].split()[-1]
            sources.append({'original': str(helper), 'sha256': sha(helper)})
        for data in inventory[identifier]['inputs']:
            path = Path(data['path'])
            if sha(path) != data['sha256']:
                raise ValueError('input differs from manifest: ' + str(path))
            sources.append({'input': str(path), 'sha256': sha(path)})
        if spec['suite'] == 'text' and spec['name'].startswith('jq_'):
            # The jq driver reads its filter and optional data outside the timed region.
            call = re.search(r'runJqBenchmark\("([^"]+)", "(?:null|json|raw)", (null|"[^"]+")', original.read_text())
            if not call or call.group(1) != spec['name']:
                raise ValueError('unrecognized jq input contract: ' + identifier)
            inputs = [ROOT / 'test/benchmark/text/jq' / (spec['name'][3:] + '.jq')]
            if call.group(2) != 'null':
                inputs.append(ROOT / json.loads(call.group(2)))
            for path in inputs:
                frozen = out / 'inputs' / path.relative_to(ROOT)
                frozen.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(path, frozen)
                sources.extend([{'input': str(path), 'sha256': sha(path)},
                                {'frozen': str(frozen), 'sha256': sha(frozen)}])
        commands = {'node': [shutil.which('node'), str(script)],
                    'candidate': [binary, 'js', '--runtime=mvp-lmd', '--no-log', str(script)]}
        row = dict(id=identifier, population=population, contract=contract,
                   pending=pending, sources=sources, commands=commands, samples=[])
        if references:
            untyped, _ = mir_script_variants(spec)
            path = Path(untyped)
            frozen = out / 'lambda_reference' / spec['suite'] / path.name
            frozen.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, frozen)
            commands['lambda_untyped'] = [binary, 'run', '--no-log', str(frozen)]
            sources.extend([{'reference': str(path), 'sha256': sha(path)},
                            {'frozen': str(frozen), 'sha256': sha(frozen)}])
            if spec['suite'] == 'julia':
                helper = path.parent / 'micro_common.ls'
                shutil.copy2(helper, frozen.parent / helper.name)
                sources.extend([{'reference': str(helper), 'sha256': sha(helper)},
                    {'frozen': str(frozen.parent / helper.name), 'sha256': sha(helper)}])
            golden = path.with_suffix('.txt')
            row['lambda_expected'] = normalized_stdout(golden.read_text()) if golden.exists() else None
            if spec['suite'] == 'jetstream':
                row['lambda_expected'] = {'cube3d': '3d-cube: PASS',
                    'navier_stokes': 'navier-stokes: PASS (checksum=77)',
                    'splay': 'splay: PASS (nodes=8000)',
                    'raytrace3d': '3d-raytrace: PASS (pixels=7200)'}.get(spec['name'], row['lambda_expected'])
            row['lambda_variant'] = 'canonical .ls with annotations' if re.search(
                r':\s*(?:int|float|u32)\b', path.read_text()) else 'untyped canonical .ls'
            row['lambda_comparability'] = {
                'jetstream/navier_stokes': 'unmatched: Lambda times setup and 15 frames; JS times one frame',
                'jetstream/splay': 'unmatched: Lambda includes setup and uses LCG/tree port; JS uses Jenkins fixture',
            }.get(identifier, 'canonical counts and script-defined timing; source hashes retained')
        rows.append(row)
    return rows


def prepare_previous(out, baseline, binary, control):
    rows = []
    for group in ('cohort48', 'class6', 'new6'):
        path = baseline / group / 'comparison.json'
        captured = json.loads(path.read_text())
        for previous in captured['rows']:
            name = previous.get('id') or previous['name']
            identifier = name if '/' in name else ('jetstream/' if name == 'crypto_sha1' else 'awfy/') + name
            command = previous['commands']['candidate']
            source = command[command.index('-p') + 1]
            script = out / ('prior_' + identifier.replace('/', '_') + '.js')
            script.write_text(source)
            # Preserve the archived CLI timing/printing contract verbatim.
            commands = {lane: [executable] + command[1:] for lane, executable in
                        [('candidate', binary), ('control', control), ('control_peer', control)]}
            rows.append(dict(id=identifier, population='prior60',
                contract='exact Result8 wrapper and CLI entry timing',
                expected=previous.get('expected', 1),
                oracle='Result8 JSON value comparison, including literal newlines in CLI strings',
                sources=[{'record': str(path), 'sha256': sha(path)},
                         {'frozen': str(script), 'sha256': sha(script)}],
                commands=commands, samples=[], pending=[]))
    if len(rows) != 60 or len({r['id'] for r in rows}) != 60:
        raise ValueError('Result8 archive must contain 60 distinct contracts')
    return rows


def prepare_phase29(out, binary, control):
    """Retain each accepted source/timer/oracle, including the older value-printing CLI."""
    summary_path = ROOT / 'temp/mvp_expansion/final-stage25.json'
    summary = json.loads(summary_path.read_text())
    if summary['candidate_sha256'] != LIBRARY_CONTROL_SHA:
        raise ValueError('phase29 summary has a different control')
    rows = []
    for cohort in ('prior60', 'new16'):
        evidence = summary['captures'][cohort]
        path = summary_path.parent / evidence['path']
        if sha(path) != evidence['sha256']:
            raise ValueError('phase29 capture changed: ' + str(path))
        record = json.loads(path.read_text())
        if record['metadata']['candidate']['sha256'] != LIBRARY_CONTROL_SHA:
            raise ValueError('phase29 capture has a different control')
        for previous in record['rows']:
            if previous['status'] != 'ok':
                raise ValueError('phase29 workload was not validated: ' + previous['id'])
            command = previous['commands']['candidate'][:]
            sources = previous['sources'][:]
            for source in sources:
                original = next(source[key] for key in ('frozen', 'original', 'input', 'reference', 'record') if key in source)
                if sha(original) != source['sha256']:
                    raise ValueError('phase29 dependency changed: ' + original)
            if '-p' not in command:
                index = next(i for i, argument in enumerate(command) if argument.endswith('.js'))
                script = out / ('prior_' + previous['id'].replace('/', '_') + '.js')
                shutil.copy2(command[index], script); command[index] = str(script)
                sources.append({'frozen': str(script), 'sha256': sha(script)})
            sources.append({'record': str(path), 'sha256': sha(path)})
            commands = {lane: [executable] + command[1:] for lane, executable in
                [('candidate', binary), ('control', control), ('control_peer', control)]}
            rows.append(dict(id=previous['id'], population='prior60' if cohort == 'prior60' else 'prior16',
                contract=previous['contract'], expected=previous['expected'], sources=sources,
                commands=commands, samples=[], pending=[]))
    if len(rows) != 76 or len({row['id'] for row in rows}) != 76:
        raise ValueError('phase29 must retain 76 distinct contracts')
    return rows


def capture(row, lane, iteration, out, environment, timeout):
    command = row['commands'][lane][:]
    env = environment.copy()
    stem = row['id'].replace('/', '_') + '_' + lane + '_' + str(iteration)
    mir = out / (stem + '.mir')
    if iteration == 0 and lane != 'node':
        if '--no-log' in command:
            command.remove('--no-log')
        env['LAMBDA_MIR_DUMP_PATH'] = str(mir)
    result = run_once(command[0], '', timeout, language='lambda' if lane == 'lambda_untyped' else 'js',
        command=command, environment=env, strict_timing=True,
        exact_stdout=row['population'] != 'prior60' and lane != 'lambda_untyped', capture_output=True)
    for stream in ('stdout', 'stderr'):
        (out / (stem + '.' + stream)).write_text(result.pop(stream, ''))
    output = result.pop('stable_stdout', '')
    if lane == 'node':
        if result['status'] == 'ok' and output and 'FAIL' not in output:
            if 'expected' not in row:
                row['expected'] = output
        else:
            result['status'] = 'oracle_failed'
    expected = row.get('lambda_expected') if lane == 'lambda_untyped' else row.get('expected')
    if row['population'] == 'prior60':
        # The archived CLI prints JSON values; retain the original typed oracle.
        try:
            output = json.loads(output, strict=False)
        except (ValueError, TypeError):
            result['status'] = 'oracle_failed' if result['status'] == 'ok' else result['status']
    if expected is None or output != expected:
        result['status'] = 'oracle_failed' if result['status'] == 'ok' else result['status']
        result['expected'] = expected
        result['actual'] = output
    if iteration == 0 and lane != 'node' and result['status'] == 'ok':
        if not mir.exists() or not mir.stat().st_size:
            result['status'] = 'backend_unverified'
        else:
            result['mir'] = {'path': str(mir), 'sha256': sha(mir)}
    result.update(lane=lane, round=iteration)
    row['samples'].append(result)
    return result['status'] == 'ok'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--phase', choices=['expansion', 'library'], default='expansion')
    parser.add_argument('--candidate', default='lambda.exe')
    parser.add_argument('--control')
    parser.add_argument('--baseline-dir', default='temp/mvp_regressions_20261009/accepted')
    parser.add_argument('--population', choices=['new16', 'prior60', 'new11', 'prior76', 'all'], default='all')
    parser.add_argument('--bench', help='comma-separated exact workload IDs')
    parser.add_argument('--runs', type=int, default=15,
        help='measured repeats after preflight; 0 validates outputs/backend without a timing comparison')
    parser.add_argument('--timeout', type=int, default=180)
    parser.add_argument('--out', default='temp/mvp_expansion/capture')
    parser.add_argument('--prepare-only', action='store_true')
    parser.add_argument('--lambda-reference', action='store_true')
    args = parser.parse_args()
    os.chdir(ROOT)
    out = Path(args.out).resolve()
    if not out.is_relative_to(ROOT / 'temp') or args.runs < 0:
        parser.error('use an output below temp/ and nonnegative run count')
    out.mkdir(parents=True, exist_ok=True)
    binary = out / 'candidate.exe'
    if binary.resolve() != Path(args.candidate).resolve():
        shutil.copy2(args.candidate, binary)
    check_release_build(exe_path=str(binary), log_path=str(out / 'release.log'))
    library = args.phase == 'library'
    control = Path(args.control or ('temp/mvp_library/control.exe' if library else 'temp/mvp_expansion/control.exe')).resolve()
    if args.population not in (('new11', 'prior76', 'all') if library else ('new16', 'prior60', 'all')):
        parser.error('population does not belong to the selected phase')
    rows = []
    if args.population in ('new16', 'new11', 'all'):
        rows.extend(prepare_new(out, str(binary), args.lambda_reference,
            LIBRARY_TARGETS if library else TARGETS, 'new11' if library else 'new16'))
    if args.population in ('prior60', 'prior76', 'all'):
        if sha(control) != (LIBRARY_CONTROL_SHA if library else CONTROL_SHA):
            parser.error('control differs from the accepted phase binary')
        rows.extend(prepare_phase29(out, str(binary), str(control)) if library else
            prepare_previous(out, Path(args.baseline_dir), str(binary), str(control)))
    if args.bench:
        selected = set(args.bench.split(','))
        rows = [r for r in rows if r['id'] in selected]
        if len(rows) != len(selected):
            parser.error('unknown workload ID in --bench')
    environment = {k: v for k, v in os.environ.items() if not k.startswith('LAMBDA_GC_') and k not in
        ('LAMBDA_MIR_DUMP_PATH', 'JS_MIR_DUMP', 'LAMBDA_MIR_LOG_FRAME_SLOTS', 'LAMBDA_LOG_FILE',
         'JS_OPT_TRACE', 'JS_TRANSPILE_TIMING', 'LAMBDA_COMPILER_TIMING')}
    environment.update(FLAGS)
    record = {'metadata': dict(started=datetime.datetime.now().astimezone().isoformat(),
        candidate={'path': str(binary), 'sha256': sha(binary)}, runner_sha256=sha(__file__),
        revision=subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip(),
        platform=platform.platform(), machine=platform.machine(),
        control={'path': str(control), 'sha256': sha(control)} if control.exists() else None,
        runs=args.runs, warmups=1, validation_only=args.runs == 0, flags=FLAGS, phase=args.phase,
        node_version=subprocess.check_output(['node', '--version'], text=True).strip(),
        node_sha256=sha(shutil.which('node')),
        timing='Script self-timing except exact archived entry timers for prior60; process wall time separate. '
               'Fresh processes; initial MIR compilation excluded; Node in-workload tiering included.'),
        'rows': rows}
    def save():
        (out / 'comparison.json').write_text(json.dumps(record, indent=2) + '\n')
    save()
    if args.prepare_only:
        return
    complete = True
    permutations = list(itertools.permutations(('candidate', 'control', 'control_peer')))
    for row in rows:
        valid = True
        for lane in row['commands']:
            valid = capture(row, lane, 0, out, environment, args.timeout) and valid
            save()
        if valid:
            for iteration in range(1, args.runs + 1):
                lanes = list(permutations[(iteration - 1) % 6]) if 'control' in row['commands'] else list(row['commands'])
                if 'control' not in row['commands']:
                    offset = iteration % len(lanes)
                    lanes = lanes[offset:] + lanes[:offset]
                for lane in lanes:
                    valid = capture(row, lane, iteration, out, environment, args.timeout) and valid
                    save()
                if not valid:
                    break
        row['status'] = 'ok' if valid and not row['pending'] else 'pending_contract' if valid else 'failed'
        if valid and args.runs:
            row['median_ms'] = {lane: statistics.median(s['exec_ms'] for s in row['samples']
                if s['lane'] == lane and s['round']) for lane in row['commands']}
            if 'control' in row['commands']:
                for lane in ('candidate', 'control_peer'):
                    pairs = [{'control': next(s for s in row['samples']
                              if s['lane'] == 'control' and s['round'] == i),
                              'candidate': next(s for s in row['samples']
                              if s['lane'] == lane and s['round'] == i)} for i in range(1, args.runs + 1)]
                    row[lane + '_interval'] = paired_ratio_bootstrap(pairs, 10000, 260026)
        complete = complete and row['status'] == 'ok'
        save()
        print(row['id'], row['status'], row.get('median_ms', {}), flush=True)
    record['metadata']['finished'] = datetime.datetime.now().astimezone().isoformat()
    record['metadata']['complete'] = complete
    if sha(binary) != record['metadata']['candidate']['sha256']:
        raise RuntimeError('candidate changed during capture')
    for row in rows:
        for source in row['sources']:
            path = next(source[key] for key in ('frozen', 'original', 'input', 'reference', 'record') if key in source)
            if sha(path) != source['sha256']:
                raise RuntimeError('source changed during capture: ' + path)
    save()
    return 0 if complete else 1


if __name__ == '__main__':
    sys.exit(main())
