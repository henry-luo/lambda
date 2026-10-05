#!/usr/bin/env python3
"""jq_mix on purejq; the filter is test/benchmark/text/jq/mix.jq."""

from jq_common import run_jq_benchmark

if __name__ == "__main__":
    run_jq_benchmark("jq_mix", "null", None, 98172625)
