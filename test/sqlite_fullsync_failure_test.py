#!/usr/bin/env python3
"""Inject macOS full-sync failures into real SQLite and export publication."""
import json
import os
from pathlib import Path
import sqlite3
import subprocess
import sys
import tempfile


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
        subprocess.run(['cc', '-Wno-deprecated-declarations', '-dynamiclib', '-o', str(library),
                        'test/sqlite_fullsync_interpose.c'],
                       check=True, cwd=root)
        source = folder / 'source.db'
        with sqlite3.connect(source) as db:
            db.execute('CREATE TABLE example(value INTEGER)')
            db.execute('INSERT INTO example VALUES (42)')
        quote = lambda path: json.dumps(str(path))
        for tier in ('interp', 'jit'):
            for kind in ('file', 'directory'):
                for name in ('create', 'clone', 'preimage', 'export'):
                    target = folder / f'{tier}-{kind}-{name}.out'
                    upgrade = folder / f'{tier}-{kind}-{name}-upgrade.db'
                    if name == 'create':
                        body = f'open tx = (sqlite.create({quote(target)})^) {{ sqlite.execute(tx,"CREATE TABLE t(n INTEGER)")^ }}'
                    elif name == 'clone':
                        body = f'open tx = (sqlite.clone({quote(source)},{quote(target)})^) {{ sqlite.query(tx,"SELECT * FROM example")^ }}'
                    elif name == 'preimage':
                        body = f'open tx = (sqlite.clone({quote(source)},{quote(upgrade)})^) {{ sqlite.preimage(tx,{quote(target)})^ }}'
                    else:
                        body = f'io.create_file({quote(target)},"complete")^'
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


if __name__ == '__main__':
    main()
