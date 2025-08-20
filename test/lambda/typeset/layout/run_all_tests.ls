// Master Test Runner for Text Layout Foundation (Weeks 1-4)
// Runs all component tests and integration tests

fn run_all_layout_tests() {
    print("🚀 Lambda Typesetting System - Text Layout Foundation Tests")
    print("Testing Weeks 1-4 Implementation")
    print("=" * 60)
    print()
    
    let start_time = current_time_millis()
    let total_tests = 0
    let passed_tests = 0
    
    // Week 1: Font System Tests
    print("📝 WEEK 1: FONT SYSTEM TESTS")
    print("-" * 30)
    try {
        include("test_font_system.ls")
        passed_tests = passed_tests + 7  // Number of font system tests
        print("✅ Font System: All tests passed")
    } catch (error) {
        print("❌ Font System: " + error.message)
    }
    total_tests = total_tests + 7
    print()
    
    // Week 2: Line Breaking Tests
    print("📏 WEEK 2: LINE BREAKING TESTS")
    print("-" * 30)
    try {
        include("test_line_breaking.ls")
        passed_tests = passed_tests + 8  // Number of line breaking tests
        print("✅ Line Breaking: All tests passed")
    } catch (error) {
        print("❌ Line Breaking: " + error.message)
    }
    total_tests = total_tests + 8
    print()
    
    // Week 3: Text Flow Tests
    print("🌊 WEEK 3: TEXT FLOW TESTS")
    print("-" * 30)
    try {
        include("test_text_flow.ls")
        passed_tests = passed_tests + 9  // Number of text flow tests
        print("✅ Text Flow: All tests passed")
    } catch (error) {
        print("❌ Text Flow: " + error.message)
    }
    total_tests = total_tests + 9
    print()
    
    // Week 4: Vertical Metrics Tests
    print("📐 WEEK 4: VERTICAL METRICS TESTS")
    print("-" * 30)
    try {
        include("test_vertical_metrics.ls")
        passed_tests = passed_tests + 11  // Number of vertical metrics tests
        print("✅ Vertical Metrics: All tests passed")
    } catch (error) {
        print("❌ Vertical Metrics: " + error.message)
    }
    total_tests = total_tests + 11
    print()
    
    // Integration Tests
    print("🔗 INTEGRATION TESTS")
    print("-" * 30)
    try {
        include("test_integration.ls")
        passed_tests = passed_tests + 7  // Number of integration tests
        print("✅ Integration: All tests passed")
    } catch (error) {
        print("❌ Integration: " + error.message)
    }
    total_tests = total_tests + 7
    print()
    
    // Performance Summary
    let duration = current_time_millis() - start_time
    print("⏱️  PERFORMANCE SUMMARY")
    print("-" * 30)
    print("Total execution time: " + string(duration) + "ms")
    print("Average per test: " + string(duration / total_tests) + "ms")
    print()
    
    // Final Results
    print("📊 FINAL RESULTS")
    print("=" * 30)
    print("Total tests: " + string(total_tests))
    print("Passed: " + string(passed_tests))
    print("Failed: " + string(total_tests - passed_tests))
    print("Success rate: " + string((passed_tests * 100) / total_tests) + "%")
    print()
    
    if (passed_tests == total_tests) {
        print("🎉 ALL TESTS PASSED!")
        print("✅ Text Layout Foundation (Weeks 1-4) is ready for production!")
        print()
        print("📋 IMPLEMENTATION STATUS:")
        print("✅ Week 1: Font System Integration")
        print("✅ Week 2: Line Breaking Algorithm")
        print("✅ Week 3: Text Flow Engine")
        print("✅ Week 4: Vertical Metrics and Baseline")
        print()
        print("🚀 Ready to proceed to Weeks 5-8: Box Model Implementation")
    } else {
        print("⚠️  SOME TESTS FAILED!")
        print("Please review failed tests before proceeding to next phase.")
    }
    
    print("=" * 60)
}

fn run_quick_smoke_tests() {
    print("💨 Quick Smoke Tests for Text Layout Foundation")
    print("-" * 50)
    
    let ctx = create_context()
    
    // Quick font system check
    print("Testing font system...")
    let font_mgr = font_manager_create(ctx)
    let font = font_manager_get_font(font_mgr, "Times", 12.0, 400)
    assert(font != null, "Font loading should work")
    print("✓ Font system operational")
    
    // Quick line breaking check
    print("Testing line breaking...")
    let breaker = line_breaker_create(ctx)
    assert(breaker != null, "Line breaker should initialize")
    print("✓ Line breaking operational")
    
    // Quick text flow check
    print("Testing text flow...")
    let flow = text_flow_create(ctx)
    assert(flow != null, "Text flow should initialize")
    print("✓ Text flow operational")
    
    // Quick vertical metrics check
    print("Testing vertical metrics...")
    let vm = vertical_metrics_create(ctx)
    assert(vm != null, "Vertical metrics should initialize")
    print("✓ Vertical metrics operational")
    
    // Cleanup
    vertical_metrics_destroy(vm)
    text_flow_destroy(flow)
    line_breaker_destroy(breaker)
    font_manager_destroy(font_mgr)
    destroy_context(ctx)
    
    print("✅ All systems operational - Ready for full testing!")
}

fn run_benchmark_tests() {
    print("⚡ Performance Benchmark Tests")
    print("-" * 40)
    
    let ctx = create_context()
    let font_mgr = font_manager_create(ctx)
    let font = font_manager_get_font(font_mgr, "Times", 12.0, 400)
    
    // Benchmark font loading
    let start_time = current_time_millis()
    for (i in 0 to 100) {
        let test_font = font_manager_get_font(font_mgr, "Times", 12.0, 400)
    }
    let font_load_time = current_time_millis() - start_time
    print("Font loading (100x): " + string(font_load_time) + "ms")
    
    // Benchmark text measurement
    start_time = current_time_millis()
    for (i in 0 to 1000) {
        let width = font_measure_text_width(font, "Sample text for measurement", 27)
    }
    let measure_time = current_time_millis() - start_time
    print("Text measurement (1000x): " + string(measure_time) + "ms")
    
    // Benchmark text shaping
    start_time = current_time_millis()
    for (i in 0 to 100) {
        let result = text_shape(font, "Complex text with Unicode café", 30)
        text_shape_result_destroy(result)
    }
    let shape_time = current_time_millis() - start_time
    print("Text shaping (100x): " + string(shape_time) + "ms")
    
    font_manager_destroy(font_mgr)
    destroy_context(ctx)
    
    print("✅ Benchmark complete")
}

// Main execution - choose test level
fn main() {
    let test_mode = env_get("LAMBDA_TEST_MODE", "full")  // full, quick, benchmark
    
    if (test_mode == "quick") {
        run_quick_smoke_tests()
    } else if (test_mode == "benchmark") {
        run_benchmark_tests()
    } else {
        run_all_layout_tests()
    }
}

// Execute based on command line or default to full tests
main()
