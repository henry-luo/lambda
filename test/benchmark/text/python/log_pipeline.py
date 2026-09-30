#!/usr/bin/env python3
"""Mixed log parsing and aggregation workload from log_pipeline.js."""

import time

ROUNDS = 180
COUNT = 12000
MODULUS = 1000000007
SERVICES = ("api", "worker", "db", "cache")
REGIONS = ("us-east", "eu-west", "ap-south")


def make_log_line(index):
    timestamp = f"2026-09-07T{index % 24:02d}:{index % 60:02d}:{(index * 7) % 60:02d}Z"
    level = "ERROR" if index % 13 == 0 else "WARN" if index % 5 == 0 else "INFO"
    service = SERVICES[index % len(SERVICES)]
    region = REGIONS[(index * 3) % len(REGIONS)]
    status = 503 if index % 19 == 0 else 404 if index % 7 == 0 else 200
    latency = (index * 37) % 900 + 4
    size = (index * 113) % 50000 + 512
    route = "/v1/items" if index % 2 == 0 else "/v1/search"
    message = "retry-scheduled" if index % 11 == 0 else "request-complete"
    if index % 3 == 0:
        prefix = f"{timestamp} level={level} service={service}"
    else:
        prefix = f"{timestamp} {level} {service}"
    return (f"{prefix} status={status} latency={latency} region={region}"
            f" route={route} bytes={size} message={message}")


def parse_log_line(line):
    fields = line.split(" ")
    record = dict(timestamp=fields[0], level="", service="", status=0,
                  latency=0, region="", route="", bytes=0, message="")
    start = 1
    if "=" not in fields[1]:
        record["level"] = fields[1]
        record["service"] = fields[2]
        start = 3
    for token in fields[start:]:
        separator = token.find("=")
        if separator >= 0:
            key, value = token[:separator], token[separator + 1:]
            record[key] = int(value) if key in ("status", "latency", "bytes") else value
    return record


def empty_group():
    return dict(count=0, errors=0, slow=0, total_latency=0, total_bytes=0)


def process_logs(lines):
    groups = {service: empty_group() for service in SERVICES}
    accepted = rejected = 0
    for line in lines:
        record = parse_log_line(line)
        if record["status"] >= 500 or record["level"] == "ERROR":
            rejected += 1
            continue
        group = groups[record["service"]]
        group["count"] += 1
        group["total_latency"] += record["latency"]
        group["total_bytes"] += record["bytes"]
        if record["latency"] >= 500:
            group["slow"] += 1
        accepted += 1
    return groups, accepted, rejected


def main():
    logs = [make_log_line(index) for index in range(COUNT)]
    checksum = 0
    started = time.perf_counter_ns()
    for round_index in range(ROUNDS):
        groups, accepted, rejected = process_logs(logs)
        checksum = (checksum + accepted * 31 + rejected * 17
                    + groups["api"]["total_latency"]
                    + groups["worker"]["total_bytes"] + round_index) % MODULUS
    elapsed_ms = (time.perf_counter_ns() - started) / 1_000_000
    if checksum != 292634526:
        raise RuntimeError(f"unexpected log_pipeline checksum: {checksum}")
    print(f"log_pipeline: CHECKSUM:{checksum}")
    print(f"__TIMING__:{elapsed_ms:.3f}")


if __name__ == "__main__":
    main()
