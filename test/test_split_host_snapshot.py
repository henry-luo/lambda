#!/usr/bin/env python3
"""A split host can start away from its build directory and load Jube lazily."""

from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile


root = Path(__file__).resolve().parent.parent
source = Path(sys.argv[1]).resolve()
(root / 'temp').mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix='split-host-', dir=root / 'temp') as directory:
    host = Path(directory) / 'lambda-snapshot.exe'
    shutil.copy2(source, host)
    environment = os.environ.copy()
    # Startup must not depend on a developer's dynamic-library search path.
    environment.pop('DYLD_LIBRARY_PATH', None)
    environment.pop('DYLD_FALLBACK_LIBRARY_PATH', None)
    help_result = subprocess.run([str(host), '--help'], cwd=root, text=True,
                                 capture_output=True, check=True, timeout=30, env=environment)
    if 'Lambda Script Runtime' not in help_result.stdout:
        raise SystemExit('split host snapshot did not start normally')
    node_result = subprocess.run([str(host), 'js', 'test/node/string_decoder_basic.js', '--no-log'],
                                 cwd=root, text=True, capture_output=True, check=True, timeout=30,
                                 env=environment)
    if 'decoder created: true' not in node_result.stdout or 'write hello: hello' not in node_result.stdout:
        raise SystemExit(f'split host did not load node-core: {node_result.stdout} {node_result.stderr}')
print('SPLIT_HOST: snapshot starts and loads node-core on demand')
