#!/usr/bin/env python3
"""Inject macOS full-sync failures into real SQLite and export publication."""
import json
import os
from pathlib import Path
import selectors
import sqlite3
import stat
import subprocess
import sys
import tempfile


def publication_body(name, source, target, upgrade):
    quote = lambda path: json.dumps(str(path))
    if name == 'create':
        return f'open tx = (sqlite.create({quote(target)})^) {{ sqlite.execute(tx,"CREATE TABLE t(n INTEGER)")^ }}'
    if name == 'clone':
        return f'open tx = (sqlite.clone({quote(source)},{quote(target)})^) {{ sqlite.query(tx,"SELECT * FROM example")^ }}'
    if name == 'preimage':
        return f'open tx = (sqlite.clone({quote(source)},{quote(upgrade)})^) {{ sqlite.preimage(tx,{quote(target)})^ }}'
    return f'io.create_file({quote(target)},"complete")^'


def run(runtime, folder, library, tier, kind, name, body):
    script = folder / f'{tier}-{kind}-{name}.ls'
    script.write_text('import sqlite: lambda.io.sqlite\npn main() {\n' + body + '\n}\n')
    env = dict(os.environ, LAMBDA_EXEC_BACKEND=tier, DYLD_INSERT_LIBRARIES=str(library),
               LAMBDA_TEST_FULLSYNC_TARGET=str(folder), LAMBDA_TEST_FULLSYNC_KIND=kind)
    result = subprocess.run([runtime, 'run', str(script), '--no-log'], cwd=Path.cwd(),
                            env=env, capture_output=True, text=True, timeout=30)
    if result.returncode < 0:
        raise AssertionError(f'{tier} {kind} {name} terminated by signal {-result.returncode}')
    if result.returncode == 0:
        raise AssertionError(f'{tier} {kind} {name} accepted the failed full sync')
    if 'FULLSYNC-FAULT-INJECTED' not in result.stderr:
        raise AssertionError(f'{tier} {kind} {name} failed before reaching the injected barrier: '
                             f'{result.stdout}\n{result.stderr}')
    if kind == 'file':
        expected = {'create': 'transaction rolled back or commit failed',
                    'clone': 'transaction rolled back or commit failed',
                    'preimage': 'backup failed', 'export': 'publication failed'}[name]
        if expected not in result.stdout + result.stderr:
            raise AssertionError(f'{tier} {name} failed for an unrelated reason: '
                                 f'{result.stdout}\n{result.stderr}')
    if kind == 'directory' and 'durability is uncertain' not in result.stdout + result.stderr:
        raise AssertionError(f'{tier} {name} did not report the published file as uncertain: '
                             f'exit {result.returncode}: {result.stdout}\n{result.stderr}')


def interrupt_publication(runtime, folder, library, tier, kind, name, body, target, upgrade):
    script = folder / 'interrupted.ls'
    script.write_text('import sqlite: lambda.io.sqlite\npn main() {\n' + body + '\n}\n')
    env = dict(os.environ, LAMBDA_EXEC_BACKEND=tier, DYLD_INSERT_LIBRARIES=str(library),
               LAMBDA_TEST_FULLSYNC_TARGET=str(folder), LAMBDA_TEST_FULLSYNC_KIND=kind,
               LAMBDA_TEST_FULLSYNC_PAUSE='1')
    proc = subprocess.Popen([runtime, 'run', str(script), '--no-log'], cwd=Path.cwd(), env=env,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    seen = b''
    try:
        with selectors.DefaultSelector() as selector:
            selector.register(proc.stderr, selectors.EVENT_READ)
            while b'FULLSYNC-PAUSE-READY\n' not in seen:
                if not selector.select(timeout=30):
                    raise AssertionError(f'{tier} {kind} {name} did not reach the sync boundary: {seen!r}')
                data = os.read(proc.stderr.fileno(), 4096)
                if not data:
                    raise AssertionError(f'{tier} {kind} {name} exited before the sync boundary: {seen!r}')
                seen += data
        proc.kill()
        proc.communicate(timeout=30)
    finally:
        if proc.poll() is None:
            proc.kill()
            proc.communicate(timeout=30)
    assert proc.returncode < 0
    stages = list(folder.glob('.lambda-stage-*'))
    assert not upgrade.exists()
    if kind == 'file':
        assert not target.exists() and len(stages) == 1
        info = stages[0].lstat()
        assert stat.S_ISREG(info.st_mode) and info.st_mode & 0o077 == 0 and info.st_nlink == 1
        # The private test directory has no live publisher after kill and wait.
        stages[0].unlink()
    else:
        assert target.is_file() and not stages
        prior = target.read_bytes()
    retry_env = dict(os.environ, LAMBDA_EXEC_BACKEND=tier)
    retry = subprocess.run([runtime, 'run', str(script), '--no-log'], cwd=Path.cwd(),
                           env=retry_env, capture_output=True, timeout=30)
    if kind == 'file':
        assert retry.returncode == 0 and target.is_file(), (tier, name, retry.stdout, retry.stderr)
    else:
        assert retry.returncode > 0 and target.read_bytes() == prior, (tier, name, retry.stdout, retry.stderr)
    assert not list(folder.glob('.lambda-stage-*'))


def main():
    if sys.platform != 'darwin':
        print('SKIP: macOS F_FULLFSYNC fault injection')
        return
    root = Path.cwd()
    runtime = str((root / 'lambda.exe').resolve())
    (root / 'temp').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='sqlite-fullsync-', dir=root / 'temp') as directory:
        folder = Path(directory).resolve()
        library = folder / 'interpose.dylib'
        input_root = Path(os.environ.get('LAMBDA_INPUT_ROOT', root.parent / 'lambda-input'))
        subprocess.run(['cc', '-I', str(input_root), '-Wno-deprecated-declarations', '-dynamiclib', '-o', str(library),
                        'test/sqlite_fullsync_interpose.c'],
                       check=True, cwd=root)
        source = folder / 'source.db'
        with sqlite3.connect(source) as db:
            db.execute('CREATE TABLE example(value INTEGER)')
            db.execute('INSERT INTO example VALUES (42)')
        for tier in ('interp', 'jit'):
            for kind in ('file', 'directory'):
                for name in ('create', 'clone', 'preimage', 'export'):
                    target = folder / f'{tier}-{kind}-{name}.out'
                    upgrade = folder / f'{tier}-{kind}-{name}-upgrade.db'
                    body = publication_body(name, source, target, upgrade)
                    run(runtime, folder, library, tier, kind, name, body)
                    assert target.exists() == (kind == 'directory'), (tier, kind, name)
                    assert not upgrade.exists()
                    assert not list(folder.glob('.lambda-stage-*'))
                    if target.exists() and name != 'export':
                        with sqlite3.connect(target) as db:
                            assert db.execute('PRAGMA integrity_check').fetchone() == ('ok',)
                            if name == 'create':
                                assert db.execute("SELECT name FROM sqlite_schema WHERE name='t'").fetchone() == ('t',)
                            else:
                                assert db.execute('SELECT value FROM example').fetchone() == (42,)
                    if target.exists() and name == 'export':
                        assert target.read_text() == 'complete'
                print(f'{tier}: {kind} full-sync failure handled for SQLite and export publication')
            for kind in ('file', 'directory'):
                for name in ('create', 'clone', 'preimage', 'export'):
                    case = folder / f'interrupted-{tier}-{kind}-{name}'
                    case.mkdir()
                    target, upgrade = case / 'target', case / 'upgrade.db'
                    body = publication_body(name, source, target, upgrade)
                    interrupt_publication(runtime, case, library, tier, kind, name, body, target, upgrade)
                    if name == 'export':
                        assert target.read_text() == 'complete'
                    else:
                        with sqlite3.connect(target) as db:
                            assert db.execute('PRAGMA integrity_check').fetchone() == ('ok',)
                            if name == 'create':
                                assert db.execute("SELECT name FROM sqlite_schema WHERE name='t'").fetchone() == ('t',)
                            else:
                                assert db.execute('SELECT value FROM example').fetchone() == (42,)
                print(f'{tier}: interruption at {kind} sync preserved retry and publication boundaries')


if __name__ == '__main__':
    main()
