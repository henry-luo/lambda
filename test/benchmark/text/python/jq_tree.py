#!/usr/bin/env python3
"""jq_tree on purejq; the filter is test/benchmark/text/jq/tree.jq."""

from jq_common import run_jq_benchmark

if __name__ == "__main__":
    run_jq_benchmark("jq_tree", "null", None, 313746104)
