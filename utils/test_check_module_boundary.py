#!/usr/bin/env python3
"""Exercise the ownership checks for the split integration boundary."""

import unittest
from check_module_boundary import classify_adapter_imports


class AdapterImportOwnershipTest(unittest.TestCase):
    def test_dom_adapter_requires_one_native_provider(self):
        symbol = "_dom_example"
        imports = {symbol: {"lambda/dom/dom.cpp"}}
        self.assertEqual(classify_adapter_imports({symbol}, imports, {symbol: 1}, ""), {symbol})
        self.assertEqual(classify_adapter_imports({symbol}, imports, {}, ""), set())
        self.assertEqual(classify_adapter_imports({symbol}, imports, {symbol: 2}, ""), set())
        self.assertEqual(classify_adapter_imports({symbol}, {}, {symbol: 1}, ""), set())

    def test_runtime_callers_and_unlisted_registry_exports_fail(self):
        symbol = "_dom_example"
        imports = {symbol: {"lambda/dom/dom.cpp", "lambda/runtime/runner.cpp"}}
        self.assertEqual(classify_adapter_imports({symbol}, imports, {symbol: 1}, ""), set())
        imports[symbol] = {"lambda/jube/jube_registry.cpp"}
        self.assertEqual(classify_adapter_imports({symbol}, imports, {symbol: 1}, ""), set())
        catalog = "DOM_RAW(example, geometry, float, (void* e), dom_example, DOM_F_NATIVE)"
        self.assertEqual(classify_adapter_imports({symbol}, imports, {symbol: 1}, catalog), {symbol})
        self.assertEqual(classify_adapter_imports({symbol}, imports, {symbol: 1},
            catalog.replace("DOM_F_NATIVE", "DOM_F_ENGINE")), set())


if __name__ == "__main__":
    unittest.main()
