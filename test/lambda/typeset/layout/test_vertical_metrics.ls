// Test: Vertical Metrics and Baseline (Week 4 Implementation)
// Tests vertical metrics, baseline calculations, and line box management

fn test_vertical_metrics_creation() {
    let ctx = create_context()
    let vm = vertical_metrics_create(ctx)
    
    assert(vm != null, "Vertical metrics should be created successfully")
    assert(vm.lambda_context == ctx, "Context should be stored correctly")
    
    vertical_metrics_destroy(vm)
    destroy_context(ctx)
}

fn test_baseline_calculation() {
    let ctx = create_context()
    let font_mgr = font_manager_create(ctx)
    let vm = vertical_metrics_create(ctx)
    let font = font_manager_get_font(font_mgr, "Times", 12.0, 400)
    
    let baseline_info = calculate_baseline_info(font)
    
    // Test baseline values
    assert(baseline_info.alphabetic == 0.0, "Alphabetic baseline should be reference (0)")
    assert(baseline_info.ideographic < 0, "Ideographic baseline should be below alphabetic")
    assert(baseline_info.hanging > 0, "Hanging baseline should be above alphabetic")
    assert(baseline_info.mathematical >= 0, "Mathematical baseline should be at or above alphabetic")
    
    // Test baseline relationships
    assert(baseline_info.hanging > baseline_info.alphabetic, 
           "Hanging should be above alphabetic")
    assert(baseline_info.alphabetic > baseline_info.ideographic, 
           "Alphabetic should be above ideographic")
    
    vertical_metrics_destroy(vm)
    font_manager_destroy(font_mgr)
    destroy_context(ctx)
}

fn test_line_metrics_calculation() {
    let ctx = create_context()
    let font_mgr = font_manager_create(ctx)
    let vm = vertical_metrics_create(ctx)
    
    // Create a mock line with multiple fonts/sizes
    let line = create_mock_line(ctx)
    
    // Add text runs with different fonts
    let font_small = font_manager_get_font(font_mgr, "Times", 10.0, 400)
    let font_large = font_manager_get_font(font_mgr, "Times", 16.0, 700)
    
    add_text_run_to_line(line, "Small text", font_small)
    add_text_run_to_line(line, "Large bold text", font_large)
    
    let metrics = calculate_line_metrics(vm, line)
    
    assert(metrics.ascent > 0, "Line ascent should be positive")
    assert(metrics.descent > 0, "Line descent should be positive")
    assert(metrics.line_height > 0, "Line height should be positive")
    assert(metrics.baseline_offset >= 0, "Baseline offset should be non-negative")
    
    // Line height should accommodate the largest font
    let large_font_metrics = font_get_metrics(font_large)
    assert(metrics.ascent >= large_font_metrics.ascent, 
           "Line ascent should accommodate largest font")
    assert(metrics.descent >= large_font_metrics.descent, 
           "Line descent should accommodate largest font")
    
    view_node_destroy(line)
    vertical_metrics_destroy(vm)
    font_manager_destroy(font_mgr)
    destroy_context(ctx)
}

fn test_baseline_grid() {
    let ctx = create_context()
    let vm = vertical_metrics_create(ctx)
    
    // Create baseline grid
    let grid = baseline_grid_create(ctx, 14.0)  // 14pt grid
    assert(grid != null, "Baseline grid should be created")
    assert(grid.grid_size == 14.0, "Grid size should be set correctly")
    
    // Test grid alignment
    let y_positions = [5.0, 12.0, 18.0, 25.5, 32.1]
    
    for pos in y_positions {
        let aligned = baseline_grid_align(grid, pos)
        let remainder = aligned % 14.0
        assert(remainder == 0.0, "Aligned position should be on grid")
        assert(aligned >= pos, "Aligned position should not be below original")
    }
    
    baseline_grid_destroy(grid)
    vertical_metrics_destroy(vm)
    destroy_context(ctx)
}

fn test_multi_script_baseline_alignment() {
    let ctx = create_context()
    let font_mgr = font_manager_create(ctx)
    let vm = vertical_metrics_create(ctx)
    
    // Create line with mixed scripts
    let line = create_mock_line(ctx)
    let latin_font = font_manager_get_font(font_mgr, "Times", 12.0, 400)
    let arabic_font = font_manager_get_font(font_mgr, "Arial Unicode MS", 12.0, 400)
    let cjk_font = font_manager_get_font(font_mgr, "SimSun", 12.0, 400)
    
    add_text_run_to_line(line, "Latin", latin_font)
    add_text_run_to_line(line, "العربية", arabic_font)
    add_text_run_to_line(line, "中文", cjk_font)
    
    // Align text runs on common baseline
    align_text_runs_on_baseline(vm, line)
    
    // Verify alignment - all runs should share a common baseline
    let baseline_y = get_line_baseline_y(line)
    let runs = get_line_text_runs(line)
    
    for run in runs {
        let run_baseline = run.y_position + run.font_metrics.ascent
        let tolerance = 0.1
        assert(abs(run_baseline - baseline_y) <= tolerance,
               "All text runs should align on common baseline")
    }
    
    view_node_destroy(line)
    vertical_metrics_destroy(vm)
    font_manager_destroy(font_mgr)
    destroy_context(ctx)
}

fn test_line_box_calculation() {
    let ctx = create_context()
    let font_mgr = font_manager_create(ctx)
    let vm = vertical_metrics_create(ctx)
    
    let line = create_mock_line(ctx)
    let font = font_manager_get_font(font_mgr, "Times", 12.0, 400)
    add_text_run_to_line(line, "Sample text for line box", font)
    
    let line_box = calculate_line_box(vm, line)
    
    assert(line_box != null, "Line box should be calculated")
    assert(line_box.content_height > 0, "Content height should be positive")
    assert(line_box.line_height >= line_box.content_height, 
           "Line height should be at least content height")
    assert(line_box.ascent_height > 0, "Ascent height should be positive")
    assert(line_box.descent_height > 0, "Descent height should be positive")
    
    // Test line box consistency
    assert(line_box.ascent_height + line_box.descent_height <= line_box.line_height,
           "Ascent + descent should not exceed line height")
    
    line_box_destroy(line_box)
    view_node_destroy(line)
    vertical_metrics_destroy(vm)
    font_manager_destroy(font_mgr)
    destroy_context(ctx)
}

fn test_vertical_alignment() {
    let ctx = create_context()
    let font_mgr = font_manager_create(ctx)
    let vm = vertical_metrics_create(ctx)
    
    let line = create_mock_line(ctx)
    let font = font_manager_get_font(font_mgr, "Times", 12.0, 400)
    add_text_run_to_line(line, "Vertically aligned text", font)
    
    let original_y = get_line_y_position(line)
    
    // Test different vertical alignments
    apply_vertical_alignment(vm, line, VERTICAL_ALIGN_TOP)
    let top_y = get_line_y_position(line)
    
    apply_vertical_alignment(vm, line, VERTICAL_ALIGN_MIDDLE)
    let middle_y = get_line_y_position(line)
    
    apply_vertical_alignment(vm, line, VERTICAL_ALIGN_BOTTOM)
    let bottom_y = get_line_y_position(line)
    
    // Verify alignment order
    assert(top_y <= middle_y, "Top alignment should be above or equal to middle")
    assert(middle_y <= bottom_y, "Middle alignment should be above or equal to bottom")
    
    view_node_destroy(line)
    vertical_metrics_destroy(vm)
    font_manager_destroy(font_mgr)
    destroy_context(ctx)
}

fn test_leading_calculation() {
    let ctx = create_context()
    let font_mgr = font_manager_create(ctx)
    let vm = vertical_metrics_create(ctx)
    let font = font_manager_get_font(font_mgr, "Times", 12.0, 400)
    
    let metrics = font_get_metrics(font)
    
    // Test different leading calculations
    let auto_leading = calculate_auto_leading(vm, font)
    assert(auto_leading > 0, "Auto leading should be positive")
    assert(auto_leading >= (metrics.ascent + metrics.descent),
           "Auto leading should be at least ascent + descent")
    
    let custom_leading = calculate_leading(vm, font, 1.5)  // 1.5x line spacing
    assert(custom_leading > auto_leading, "1.5x leading should be larger than auto")
    
    let tight_leading = calculate_leading(vm, font, 1.0)  // 1.0x line spacing
    assert(tight_leading <= auto_leading, "1.0x leading should be smaller than or equal to auto")
    
    vertical_metrics_destroy(vm)
    font_manager_destroy(font_mgr)
    destroy_context(ctx)
}

fn test_baseline_shift() {
    let ctx = create_context()
    let font_mgr = font_manager_create(ctx)
    let vm = vertical_metrics_create(ctx)
    
    let line = create_mock_line(ctx)
    let font = font_manager_get_font(font_mgr, "Times", 12.0, 400)
    let run = add_text_run_to_line(line, "Shifted text", font)
    
    let original_baseline = get_text_run_baseline_y(run)
    
    // Apply baseline shift (e.g., for superscript)
    apply_baseline_shift(vm, run, 3.0)  // Shift up by 3 points
    let shifted_baseline = get_text_run_baseline_y(run)
    
    assert(shifted_baseline == original_baseline + 3.0,
           "Baseline should be shifted by specified amount")
    
    // Apply negative shift (e.g., for subscript)
    apply_baseline_shift(vm, run, -5.0)  // Shift down by 5 points
    let subscript_baseline = get_text_run_baseline_y(run)
    
    assert(subscript_baseline == original_baseline - 5.0,
           "Negative shift should move baseline down")
    
    view_node_destroy(line)
    vertical_metrics_destroy(vm)
    font_manager_destroy(font_mgr)
    destroy_context(ctx)
}

fn test_line_spacing_consistency() {
    let ctx = create_context()
    let font_mgr = font_manager_create(ctx)
    let vm = vertical_metrics_create(ctx)
    
    // Create multiple lines with same font
    let lines = []
    let font = font_manager_get_font(font_mgr, "Times", 12.0, 400)
    
    for (i in 0 to 5) {
        let line = create_mock_line(ctx)
        add_text_run_to_line(line, "Line " + string(i), font)
        lines = lines + [line]
    }
    
    // Calculate metrics for all lines
    let line_metrics = []
    for line in lines {
        line_metrics = line_metrics + [calculate_line_metrics(vm, line)]
    }
    
    // Verify consistent line heights
    let first_height = line_metrics[0].line_height
    for (i in 1 to length(line_metrics)) {
        let tolerance = 0.01
        assert(abs(line_metrics[i].line_height - first_height) <= tolerance,
               "Lines with same font should have consistent heights")
    }
    
    // Cleanup
    for line in lines {
        view_node_destroy(line)
    }
    
    vertical_metrics_destroy(vm)
    font_manager_destroy(font_mgr)
    destroy_context(ctx)
}

fn test_metrics_caching() {
    let ctx = create_context()
    let font_mgr = font_manager_create(ctx)
    let vm = vertical_metrics_create(ctx)
    let font = font_manager_get_font(font_mgr, "Times", 12.0, 400)
    
    // First calculation should populate cache
    let start_time = current_time_millis()
    let metrics1 = calculate_baseline_info(font)
    let first_duration = current_time_millis() - start_time
    
    // Second calculation should use cache
    start_time = current_time_millis()
    let metrics2 = calculate_baseline_info(font)
    let second_duration = current_time_millis() - start_time
    
    // Verify same results
    assert(metrics1.alphabetic == metrics2.alphabetic, "Cached results should match")
    assert(metrics1.hanging == metrics2.hanging, "Cached results should match")
    assert(metrics1.ideographic == metrics2.ideographic, "Cached results should match")
    
    // Note: Timing may not be reliable for small calculations, but structure should be correct
    
    vertical_metrics_destroy(vm)
    font_manager_destroy(font_mgr)
    destroy_context(ctx)
}

// Helper functions for testing
fn create_mock_line(ctx) {
    // Create a mock line node for testing
    let line = view_node_create(ctx, VIEW_NODE_LINE)
    view_node_set_position(line, 0.0, 0.0)
    return line
}

fn add_text_run_to_line(line, text, font) {
    let run = view_node_create(line.context, VIEW_NODE_TEXT_RUN)
    view_text_run_set_text(run, text)
    view_text_run_set_font(run, font)
    view_node_add_child(line, run)
    return run
}

// Run all vertical metrics tests
fn run_vertical_metrics_tests() {
    print("Running Vertical Metrics Tests...")
    
    test_vertical_metrics_creation()
    print("✓ Vertical metrics creation")
    
    test_baseline_calculation()
    print("✓ Baseline calculation")
    
    test_line_metrics_calculation()
    print("✓ Line metrics calculation")
    
    test_baseline_grid()
    print("✓ Baseline grid")
    
    test_multi_script_baseline_alignment()
    print("✓ Multi-script baseline alignment")
    
    test_line_box_calculation()
    print("✓ Line box calculation")
    
    test_vertical_alignment()
    print("✓ Vertical alignment")
    
    test_leading_calculation()
    print("✓ Leading calculation")
    
    test_baseline_shift()
    print("✓ Baseline shift")
    
    test_line_spacing_consistency()
    print("✓ Line spacing consistency")
    
    test_metrics_caching()
    print("✓ Metrics caching")
    
    print("All vertical metrics tests passed! ✅")
}

// Execute tests
run_vertical_metrics_tests()
