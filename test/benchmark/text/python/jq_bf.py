#!/usr/bin/env python3
"""jq_bf on purejq; the filter is test/benchmark/text/jq/bf.jq."""

from jq_common import run_jq_benchmark

if __name__ == "__main__":
    run_jq_benchmark("jq_bf", "raw", "test/benchmark/text/jq/fib.bf", 478890292)
