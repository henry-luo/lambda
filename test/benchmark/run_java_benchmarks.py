#!/usr/bin/env python3
"""Run native Java ports; see java/README.md."""
from native_benchmark_runner import main, runtime_metadata as _metadata, shell_command as _command

def shell_command(suite, name):
    return _command('java', suite, name)

def runtime_metadata():
    return _metadata('java')

if __name__ == '__main__':
    raise SystemExit(main('java'))
