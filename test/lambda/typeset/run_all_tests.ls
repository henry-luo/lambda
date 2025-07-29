#!/usr/bin/env lambda

# Typesetting System Test Runner
# Runs all typesetting tests from the proper test directory

print("Lambda Typesetting System - Test Suite")
print("=====================================")

# Test 1: Basic typesetting functionality
print("\\nRunning basic typesetting tests...")
try {
    result1 = run_file("test/lambda/typeset/test_typesetting.ls")
    if result1 {
        print("✓ Basic typesetting tests passed")
    } else {
        print("✗ Basic typesetting tests failed")
    }
} catch error {
    print("✗ Basic typesetting tests error:", error)
}

# Test 2: Refined view tree architecture
print("\\nRunning refined architecture tests...")
try {
    result2 = run_file("test/lambda/typeset/test_refined_typesetting.ls")
    if result2 {
        print("✓ Refined architecture tests passed")
    } else {
        print("✗ Refined architecture tests failed")
    }
} catch error {
    print("✗ Refined architecture tests error:", error)
}

print("\\nTypesetting test suite completed.")
print("For individual tests, run:")
print("  make test-typeset")
print("  make test-typeset-refined")
print("  make test-typeset-math")
print("  make test-typeset-markdown")
