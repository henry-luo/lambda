#!/usr/bin/env python3
"""jq_records on purejq; the filter is test/benchmark/text/jq/records.jq."""

from jq_common import run_jq_benchmark

if __name__ == "__main__":
    run_jq_benchmark("jq_records", "json", "test/benchmark/text/jq/orders.json", 878885883)
