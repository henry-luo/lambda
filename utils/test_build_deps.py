#!/usr/bin/env python3
"""Run the input-owned tests for the shared dependency builder."""
import runpy
from split_paths import sibling_roots

runpy.run_path(str(sibling_roots()[0] / 'test/test_build_deps.py'), run_name='__main__')
