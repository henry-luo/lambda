#!/usr/bin/env python3
"""Build Lambda dependencies with the input-owned shared recipe builder."""
from pathlib import Path
import subprocess
import sys

from split_paths import sibling_roots, use_input_scripts
use_input_scripts()
from dependency_builder import (CompilerProbe, expand, generate_headers, generate_premake,
                                install, make_variable, main as build_main, projects_for,
                                render_template, run)

ROOT = Path(__file__).resolve().parents[1]
CONFIG = ROOT / 'build_deps_config.json'


def main():
    if len(sys.argv) > 1 and sys.argv[1] == 'libdatachannel':
        from prepare_sync_connectivity import configure_mbedtls
        configure_mbedtls()
    build_main(ROOT, CONFIG, sibling_roots()[0])


if __name__ == '__main__':
    try:
        main()
    except (OSError, RuntimeError, ValueError, subprocess.CalledProcessError) as error:
        print(f'dependency-build: {error}', file=sys.stderr)
        sys.exit(1)
