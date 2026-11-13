"""Release-only warm delivery and durable-install latency between native agents."""
import argparse
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import secrets
import statistics
import time

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('lan_acceptance', ROOT / 'test/sync_connectivity/test_lan_events.py')
lan = importlib.util.module_from_spec(spec)
spec.loader.exec_module(lan)


def covers(actual, expected):
    have = {item['actor']: item['counter'] for item in actual or []}
    return bool(expected) and all(have.get(item['actor'], 0) >= item['counter'] for item in expected)


def summarize(values):
    ordered = sorted(values)
    return dict(median=statistics.median(ordered), p95=ordered[math.ceil(len(ordered)*.95)-1], maximum=max(ordered))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--seed-files', type=int, default=10)
    parser.add_argument('--samples', type=int, default=10)
    parser.add_argument('--peers', type=int, choices=[2, 3], default=2)
    parser.add_argument('--workload', choices=['edit', 'create'], default='edit')
    parser.add_argument('--max-seconds', type=float, default=1.0)
    parser.add_argument('--report', type=Path, default=ROOT / 'temp/sync-warm-latency/acceptance.json')
    args = parser.parse_args()
    assert args.seed_files >= 1 and args.samples >= 1 and args.max_seconds > 0
    executable = Path(lan.peer.EXE).resolve()
    native = executable.read_bytes()
    assert b'VALIDATOR] validate_against_base_type' not in native, 'Latency acceptance requires a release executable'
    area = ROOT / 'temp/sync-warm-latency' / ('benchmark-' + str(os.getpid()))
    lan.AREA = area
    lan.peer.AREA = area
    names = ['a', 'b', 'c'][:args.peers]
    samples = []
    ids = {}

    def poll():
        reports = {name: lan.report(name) for name in names}
        now = time.perf_counter()
        for sample in samples:
            if sample['frontier'] is None:
                fresh = reports['a'].get('captured_frontier')
                if fresh and fresh != sample['previous_frontier']:
                    sample['frontier'] = fresh
                    sample['capture_seconds'] = now-sample['started']
            for name in names[1:]:
                if name not in sample['visible'] and lan.content(name, sample['path'], sample['value']):
                    sample['visible'][name] = now-sample['started']
                if name in sample['visible'] and name not in sample['installed'] and covers(
                        reports[name].get('installed_frontier'), sample['frontier']):
                    assert not reports[name].get('installation') and not reports[name].get('review'), 'Installation needs review'
                    sample['installed'][name] = now-sample['started']
        return reports

    def wait(predicate, label):
        deadline = time.perf_counter()+60
        while True:
            reports = poll()
            if predicate(reports):
                return reports
            for name in names:
                assert lan.jobs[name][0].poll() is None, name+' exited: '+label
            assert time.perf_counter() < deadline, label+' timed out'
            time.sleep(.01)

    def settled(reports):
        frontier = reports['a'].get('captured_frontier')
        return frontier and all(r.get('active_connections', 0) > 0 and r.get('phase') == 'idle' and
            r.get('pending_changes', 1) == 0 and covers(r.get('installed_frontier'), frontier) for r in reports.values())

    try:
        area.mkdir(parents=True)
        for name in names:
            (area / name / 'agent').mkdir(parents=True)
            (area / name / 'tree').mkdir()
            lan.peer.keys[name] = secrets.token_hex(32)
            ids[name] = lan.peer.identifier(lan.peer.run(name, 'device'))
            lan.peer.run(name, 'export_identity', output=str(area / (name+'.device')))
        lan.peer.run('a', 'init', root=str(area/'a/tree'), space='latency-'+secrets.token_hex(8), label='a')
        for name in names[1:]:
            invitation = str(area / (name+'.invitation'))
            lan.peer.run('a', 'invite', recipient=str(area/(name+'.device')), output=invitation)
            lan.peer.run(name, 'enroll', root=str(area/name/'tree'), label=name, invitation=invitation, expected=ids['a'])
        for index in range(args.seed_files):
            (area/'a/tree'/('seed-'+str(index))).write_text(str(index)+'x'*4096)
        for name in names:
            lan.start(name)
        wait(lambda r: all(lan.content(n, 'seed-0', '0'+'x'*4096) for n in names[1:]), 'initial catch-up')
        lan.quiet(tuple(names))
        (area/'a/tree/warmup').write_text('warmup')
        wait(lambda r: settled(r) and all(lan.content(n, 'warmup', 'warmup') for n in names[1:]), 'settled authenticated warm connections')
        for mode in ['settled', 'continuous']:
            for index in range(args.samples):
                reports = wait(settled, 'settled warm delivery') if mode == 'settled' else poll()
                assert all(r.get('active_connections', 0) > 0 for r in reports.values()), 'Warm session closed before write'
                before = {n: r['connections'] for n, r in reports.items()}
                path = 'seed-0' if args.workload == 'edit' else mode+'-'+str(index)
                sample = dict(mode=mode, index=index, path=path, value=(mode+':'+str(index)).ljust(4096, 'x'),
                              previous_frontier=reports['a'].get('captured_frontier'), frontier=None,
                              visible={}, installed={}, started=time.perf_counter())
                samples.append(sample)
                (area/'a/tree'/path).write_text(sample['value'])
                predicate = lambda r: len(sample['installed'] if mode == 'settled' else sample['visible']) == len(names)-1
                after = wait(predicate, 'warm '+mode+' delivery')
                sample['reused'] = before == {n: r['connections'] for n, r in after.items()}
                assert sample['reused'], 'Delivery reconnected instead of reusing the warm session'
                print(mode, index, 'visible', round(max(sample['visible'].values()), 3),
                      'installed', round(max(sample['installed'].values()), 3) if sample['installed'] else 'pending', flush=True)
        wait(lambda r: all(len(s['installed']) == len(names)-1 for s in samples), 'completed durable installations')
    finally:
        for name in list(lan.jobs):
            lan.stop(name)
        for child in lan.peer.children:
            if child.poll() is None:
                child.kill()
            child.wait()
    summary = {mode: {stage: summarize([max(s[stage].values()) for s in samples if s['mode'] == mode])
                      for stage in ['visible', 'installed']} for mode in ['settled', 'continuous']}
    public = [{k: v for k, v in s.items() if k not in ['value', 'previous_frontier', 'started']} for s in samples]
    result = dict(executable=str(executable), sha256=hashlib.sha256(native).hexdigest(),
                  backend=os.environ.get('LAMBDA_EXEC_BACKEND', 'auto'), host_scope='independent processes on one host',
                  peers=args.peers, seed_files=args.seed_files, workload=args.workload, bytes_per_edit=4096,
                  samples_per_mode=args.samples, observer_ms=10, summary_seconds=summary, samples=public)
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(summary, indent=2), flush=True)
    assert all(s['reused'] and max(s['installed'].values()) < args.max_seconds for s in samples), 'Warm durable delivery exceeded target; inspect '+str(args.report)


if __name__ == '__main__':
    main()
