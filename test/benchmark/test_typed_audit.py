#!/usr/bin/env python3
"""Check that the causal annotation control changes annotations alone."""
import sys
import unittest

sys.dont_write_bytecode = True
from run_typed_audit import erase_annotations
from run_paired_benchmarks import paired_ratio_bootstrap


class TypedAuditTest(unittest.TestCase):
    def test_erasure_preserves_contract_definitions_casts_and_borrowing(self):
        source = ('type Node = {value: int, next: Node?}\n'
            'pn f(var values: int[], node: Node?) float {\n'
            '    let text: string = "a: int" // comment: int\n'
            '    var result: int = int(values[0])\n}\n')
        expected = ('type Node = {value: int, next: Node?}\n'
            'pn f(var values, node) {\n'
            '    let text = "a: int" // comment: int\n'
            '    var result = int(values[0])\n}\n')
        self.assertEqual(erase_annotations(source), expected)
        self.assertEqual(erase_annotations(expected), expected)

    def test_erasure_preserves_record_casts_and_default_records(self):
        source = ('type Doc = {text: string}\n'
            'fn f(nodes: array, options: map = {value: int(2)}) Doc {\n'
            '    let result: Doc = {text: string(nodes[0])}\n}\n')
        expected = ('type Doc = {text: string}\n'
            'fn f(nodes, options = {value: int(2)}) {\n'
            '    let result = {text: string(nodes[0])}\n}\n')
        self.assertEqual(erase_annotations(source), expected)

    def test_two_sided_interval_retains_exact_constant_ratio(self):
        pairs = [{"control": {"exec_ms": value}, "candidate": {"exec_ms": value}}
                 for value in (100, 120, 110, 105, 115)]
        result = paired_ratio_bootstrap(pairs, 1000, 260026)
        self.assertEqual(result["two_sided_confidence"], 0.95)
        self.assertEqual(result["two_sided_lower_bound"], 1)
        self.assertEqual(result["two_sided_upper_bound"], 1)
        self.assertEqual(result["upper_bound"], 1)


if __name__ == "__main__":
    unittest.main()
