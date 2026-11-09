#!/usr/bin/env python3
"""Observe scoped SQLite commit syncs and the VFS's macOS fallback."""
import json
import os
from pathlib import Path
import sqlite3
import subprocess
import sys
import tempfile
from contextlib import closing


def check_journal_directory(runtime, folder, library, tier, style, phase):
    name = f'{tier}-{style}-directory-{phase}'
    case = folder / name
    case.mkdir()
    db_path = case / 'company.db'
    with closing(sqlite3.connect(db_path)) as db:
        assert db.execute('PRAGMA journal_mode=delete').fetchone() == ('delete',)
        db.execute('CREATE TABLE entries(value INTEGER NOT NULL)')
        db.execute('INSERT INTO entries VALUES (1)')
        db.commit()
    path = json.dumps(str(db_path))
    statement = 'sqlite.execute(tx,"INSERT INTO entries VALUES (2)")^'
    body = (f'open tx = (sqlite.existing({path})^) {{ {statement} }}' if style == 'scoped'
            else f'sqlite.execute({path},"INSERT INTO entries VALUES (2)")^')
    script = case / 'worker.ls'
    script.write_text('import sqlite: lambda.io.sqlite\npn main() {\n' + body + '\nprint("committed\\n")\n}\n')
    env = dict(os.environ, LAMBDA_EXEC_BACKEND=tier, DYLD_INSERT_LIBRARIES=str(library),
               LAMBDA_TEST_FSYNC_DIRECTORY=str(case), LAMBDA_TEST_FSYNC_MODE='trace' if phase == 'trace' else 'fail',
               LAMBDA_TEST_FSYNC_ORDINAL='1' if phase == 'creation' else '2')
    result = subprocess.run([runtime, 'run', str(script), '--no-log'], capture_output=True,
                            text=True, env=env, timeout=30)
    # A failed post-unlink sync may be reported after the journal is already gone.
    with closing(sqlite3.connect(db_path)) as db:
        assert db.execute('PRAGMA integrity_check').fetchone() == ('ok',)
        assert db.execute('SELECT value FROM entries ORDER BY value').fetchall() == [(1,), (2,)]
    if phase == 'trace':
        if result.returncode or result.stdout.strip() != 'committed' or result.stderr.count('FSYNC-DIRECTORY-OBSERVED') < 2:
            raise AssertionError(f'{name}: expected journal creation and removal syncs: {result.stdout}\n{result.stderr}')
    elif phase == 'creation':
        if result.returncode or result.stdout.strip() != 'committed' or 'FSYNC-DIRECTORY-FAULT' not in result.stderr:
            raise AssertionError(f'{name}: expected SQLite to ignore the creation directory failure: {result.stdout}\n{result.stderr}')
    elif result.returncode <= 0 or 'FSYNC-DIRECTORY-FAULT' not in result.stderr or 'commit outcome is uncertain' not in result.stdout + result.stderr:
        raise AssertionError(f'{name}: expected an uncertain commit: {result.stdout}\n{result.stderr}')


def main():
    if sys.platform != 'darwin':
        print('SKIP: macOS SQLite full-sync observation')
        return
    root = Path.cwd()
    runtime = root / 'lambda.exe'
    (root / 'temp').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='sqlite-commit-sync-', dir=root / 'temp') as directory:
        folder = Path(directory).resolve()
        library = folder / 'interpose.dylib'
        input_root = Path(os.environ.get('LAMBDA_INPUT_ROOT', root.parent / 'lambda-input'))
        subprocess.run(['cc', '-I', str(input_root), '-Wno-deprecated-declarations', '-dynamiclib', '-o', str(library),
                        'test/sqlite_fullsync_interpose.c'], cwd=root, check=True)
        for tier in ('interp', 'jit'):
            for mode, kinds in (('delete', ('sqlite-journal', 'sqlite-db')), ('wal', ('sqlite-wal',))):
                for style in ('scoped', 'standalone'):
                    for kind in kinds:
                        for trace in (True, False):
                            name = f'{tier}-{mode}-{style}-{kind}-{trace}'
                            db_path = folder / (name + '.db')
                            with closing(sqlite3.connect(db_path)) as db:
                                assert db.execute(f'PRAGMA journal_mode={mode}').fetchone() == (mode,)
                                db.execute('CREATE TABLE entries(value INTEGER NOT NULL)')
                                db.execute('INSERT INTO entries VALUES (1)')
                                db.commit()
                            path = json.dumps(str(db_path))
                            statement = 'sqlite.execute(tx,"INSERT INTO entries VALUES (2)")^'
                            body = (f'open tx = (sqlite.existing({path})^) {{ {statement} }}' if style == 'scoped'
                                    else f'sqlite.execute({path},"INSERT INTO entries VALUES (2)")^')
                            script = folder / (name + '.ls')
                            script.write_text('import sqlite: lambda.io.sqlite\npn main() {\n' + body + '\nprint("committed\\n")\n}\n')
                            env = dict(os.environ, LAMBDA_EXEC_BACKEND=tier, DYLD_INSERT_LIBRARIES=str(library),
                                LAMBDA_TEST_FULLSYNC_TARGET=str(db_path), LAMBDA_TEST_FULLSYNC_KIND=kind)
                            if trace:
                                env['LAMBDA_TEST_FULLSYNC_TRACE'] = '1'
                            else:
                                env.pop('LAMBDA_TEST_FULLSYNC_TRACE', None)
                            result = subprocess.run([runtime, 'run', str(script), '--no-log'], cwd=root,
                                capture_output=True, text=True, env=env, timeout=30)
                            marker = 'FULLSYNC-OBSERVED' if trace else 'FULLSYNC-FAULT-INJECTED'
                            if result.returncode or result.stdout.strip() != 'committed' or marker not in result.stderr:
                                raise AssertionError(f'{name}: expected committed and {marker}: '
                                                     f'{result.stdout}\n{result.stderr}')
                            # SQLite currently falls back to fsync when F_FULLFSYNC fails.
                            with closing(sqlite3.connect(db_path)) as db:
                                assert db.execute('PRAGMA integrity_check').fetchone() == ('ok',)
                                assert db.execute('SELECT value FROM entries ORDER BY value').fetchall() == [(1,), (2,)]
                print(f'{tier}: {mode} scoped and standalone commits requested full sync; fallback observed')
            for style in ('scoped', 'standalone'):
                for phase in ('trace', 'creation', 'removal'):
                    check_journal_directory(runtime, folder, library, tier, style, phase)
            print(f'{tier}: journal directory sync observed; creation failure ignored and removal failure uncertain')


if __name__ == '__main__':
    main()
