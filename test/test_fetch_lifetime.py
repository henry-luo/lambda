#!/usr/bin/env python3
"""Check successful and rejected CLI downloads release their resource fields."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parent.parent
INPUT = Path(os.environ.get('LAMBDA_INPUT_ROOT', ROOT.parent / 'lambda-input')).resolve()
FIXTURE = INPUT / 'test/run_native_tests.py'


def main():
    if len(sys.argv) != 2:
        raise SystemExit('usage: test_fetch_lifetime.py LAMBDA_EXECUTABLE')
    executable = str(Path(sys.argv[1]).resolve())
    temporary = ROOT / 'temp'
    temporary.mkdir(exist_ok=True)
    env = dict(os.environ, MEMTRACK_MODE='DEBUG', INPUT_CURL_FIXTURE_CWD=str(ROOT))
    with tempfile.TemporaryDirectory(prefix='fetch-lifetime-', dir=temporary) as directory:
        output = Path(directory) / 'response.txt'
        cases = [('{fixture_url}', ['-o', str(output)], 0),
                 ('{fixture_ip_url}', [], 1)]
        for url, arguments, expected in cases:
            result = subprocess.run([sys.executable, str(FIXTURE), executable, 'fetch', url, *arguments],
                                    cwd=ROOT, env=env, text=True, stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT, timeout=60)
            if result.returncode != expected or 'memtrack: LEAK' in result.stdout:
                raise SystemExit(f'fetch lifetime failed for {url}: exit {result.returncode}\n{result.stdout}')
            if expected and 'Download failed' not in result.stdout:
                raise SystemExit(f'wrong-host fetch did not report failure:\n{result.stdout}')
        if output.read_bytes() != b'input-owned curl':
            raise SystemExit('successful fetch returned unexpected bytes')
    print('FETCH_LIFETIME: trusted and rejected HTTPS requests release their resources')


if __name__ == '__main__':
    main()
