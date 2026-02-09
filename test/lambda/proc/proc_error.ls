// Test error handling in procedural context (pn functions)

// ============================================
// 1. Procedural function with T^. (any error)
// ============================================

pn may_fail_proc(x) int^. {
    if (x < 0) {
        raise error("negative input")
    }
    return x * 2
}

// ============================================
// 2. Procedural function with explicit error type
// ============================================

pn safe_divide(a, b) int^error {
    if (b == 0) {
        raise error("division by zero")
    }
    return a / b
}

// ============================================
// 3. Procedural function with multiple raise paths
// ============================================

pn validate_range(x) int^. {
    if (x < 0) {
        raise error("too small")
    }
    if (x > 100) {
        raise error("too large")
    }
    return x
}

// ============================================
// 4. Main procedure testing basic raise
// ============================================

pn main() {
    // Test successful calls
    print(may_fail_proc(5))
    print(may_fail_proc(0))
    
    print(safe_divide(20, 4))
    print(safe_divide(15, 3))
    
    print(validate_range(50))
    print(validate_range(0))
    print(validate_range(100))
    
    print("done")
}
