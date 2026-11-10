#!/usr/bin/env python3
"""Run focused MVP emission/ownership units through the normal report protocol."""

import argparse
import json
import os
from pathlib import Path
import sys
import unittest

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]


class GateResult(unittest.TextTestResult):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.records = []
        self.current = None

    def startTest(self, test):
        super().startTest(test)
        self.current = {"name": test.id(), "pass": True, "errors": []}
        self.records.append(self.current)

    def stopTest(self, test):
        super().stopTest(test)
        self.current = None

    def record_failure(self, test, text):
        if self.current is None:
            # class setup/teardown errors have no startTest call.
            self.current = {"name": test.id(), "pass": False, "errors": []}
            self.records.append(self.current)
        self.current["pass"] = False
        self.current["errors"].append(text)

    def addFailure(self, test, err):
        super().addFailure(test, err)
        self.record_failure(test, self._exc_info_to_string(err, test))

    def addError(self, test, err):
        super().addError(test, err)
        self.record_failure(test, self._exc_info_to_string(err, test))

    def addSubTest(self, test, subtest, err):
        super().addSubTest(test, subtest, err)
        if err:
            self.record_failure(test, subtest.id() + "\n" + self._exc_info_to_string(err, subtest))

    def addSkip(self, test, reason):
        super().addSkip(test, reason)
        self.record_failure(test, "required gate case skipped: " + reason)

    def addExpectedFailure(self, test, err):
        super().addExpectedFailure(test, err)
        self.record_failure(test, "required gate case still fails: " + self._exc_info_to_string(err, test))

    def addUnexpectedSuccess(self, test):
        super().addUnexpectedSuccess(test)
        self.record_failure(test, "unexpected success needs its expected-failure marker removed")


def report(result):
    failed = sum(not case["pass"] for case in result.records)
    if not result.records:
        return {"summary": {"passed": 0, "failed": 1, "total": 1},
                "results": [{"name": "MVP emission gate selected no tests", "pass": False}]}
    return {"summary": {"passed": len(result.records) - failed,
                        "failed": failed, "total": len(result.records)},
            "results": result.records}


def suite(methods=None):
    from test_lambda_mir_mvp import LambdaMirMvpTests, MIR_EMISSION_METHODS
    selected = tuple(MIR_EMISSION_METHODS if methods is None else methods)
    if not selected or len(selected) != len(set(selected)):
        raise ValueError("MVP emission selection must be nonempty and unique")
    # Resolve every explicit case; a renamed/missing method is a gate error.
    for name in selected:
        if not name.startswith("test_") or not callable(getattr(LambdaMirMvpTests, name, None)):
            raise ValueError("unknown MVP emission case: " + name)
    return unittest.TestSuite(LambdaMirMvpTests(name) for name in selected)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--report", type=Path)
    parser.add_argument("--exe", type=Path)
    parser.add_argument("--method", action="append")
    args = parser.parse_args()
    if args.exe:
        os.environ["LAMBDA_TEST_EXE"] = str(args.exe.resolve())
    os.chdir(ROOT)
    result = unittest.TextTestRunner(verbosity=2, resultclass=GateResult).run(suite(args.method))
    data = report(result)
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(data, indent=2) + "\n")
    return int(data["summary"]["failed"] != 0)


if __name__ == "__main__":
    raise SystemExit(main())
