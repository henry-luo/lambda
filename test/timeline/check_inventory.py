#!/usr/bin/env python3
"""Validate pinned timeline provenance and acceptance evidence without network I/O."""
import argparse
import json
import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parents[2]
STATUSES = {"planned", "partial", "blocked", "complete", "excluded"}


def check(require_complete=False):
    inventory = json.loads((ROOT / "test/timeline/reference_inventory.json").read_text())
    failures = []
    counts = {status: 0 for status in STATUSES}

    def evidence(record, label, example=False):
        status = record.get("status")
        if status not in STATUSES:
            failures.append(label + ": invalid status")
            return
        counts[status] += 1
        if status == "excluded":
            if not example or record.get("exclusion") not in inventory.get("scope", {}).get("excluded_requirements", []):
                failures.append(label + ": exclusion must name a removed example requirement")
            return
        acceptance = record.get("acceptance", [])
        if status == "complete" and not acceptance:
            failures.append(label + ": complete requires acceptance evidence")
        for path in acceptance:
            resolved = (ROOT / path).resolve()
            if not resolved.is_relative_to(ROOT) or not resolved.is_file():
                failures.append(label + ": missing/unsafe evidence " + path)
        if require_complete and status != "complete":
            failures.append(label + ": " + status)

    if inventory.get("schema_version") != 1:
        failures.append("unsupported inventory version")
    references = inventory.get("references", [])
    if len(references) != 4:
        failures.append("expected all four pinned references")
    for reference in references:
        name = reference["name"]
        commit = reference.get("commit", "")
        if not re.fullmatch(r"[0-9a-f]{40}", commit):
            failures.append(name + ": invalid commit pin")
        if not reference.get("sources") or not reference.get("examples"):
            failures.append(name + ": missing option/example inventory")
        for source in reference.get("sources", []):
            if "/" + commit + "/" not in source["url"]:
                failures.append(name + ": source URL is not pinned")
            if not re.fullmatch(r"[0-9a-f]{64}", source.get("sha256", "")):
                failures.append(name + ": source hash is missing")
            if len(set(source["declared_options"])) != len(source["declared_options"]):
                failures.append(name + ": duplicate option names in " + source["path"])
        for example in reference.get("examples", []):
            evidence(example, name + ":" + example["path"], example=True)
    families = inventory.get("features", [])
    if len(families) != 27 or len({row["family"] for row in families}) != 27:
        failures.append("expected the revised proposal's 27 distinct feature families")
    scope = inventory.get("scope", {})
    if scope.get("hosts") != ["pure", "native"]:
        failures.append("revised scope requires pure/native acceptance only")
    for feature in families:
        if feature["family"] in scope.get("excluded_families", []):
            failures.append("excluded feature family remains in acceptance")
        if set(feature["hosts"]) != set(scope.get("hosts", [])):
            failures.append(feature["family"] + ": host records disagree with revised scope")
        for host, record in feature["hosts"].items():
            evidence(record, feature["family"] + ":" + host)
    if failures:
        raise SystemExit("Timeline inventory failed:\n" + "\n".join(failures))
    print("Timeline inventory: 4 pinned references, 27 families; " +
          ", ".join(str(counts[status]) + " " + status for status in sorted(counts)))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--require-complete", action="store_true")
    check(parser.parse_args().require_complete)
