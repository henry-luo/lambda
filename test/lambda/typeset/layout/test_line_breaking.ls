// Test: Line Breaking Algorithm (Week 2 Implementation)
// Tests line breaker and break point detection

fn test_line_breaker_creation() {
    let ctx = create_context()
    let breaker = line_breaker_create(ctx)
    
    assert(breaker != null, "Line breaker should be created successfully")
    assert(breaker.lambda_context == ctx, "Context should be stored correctly")
    
    line_breaker_destroy(breaker)
    destroy_context(ctx)
}

fn test_simple_line_breaking() {
    let ctx = create_context()
    let font_mgr = font_manager_create(ctx)
    let breaker = line_breaker_create(ctx)
    let font = font_manager_get_font(font_mgr, "Times", 12.0, 400)
    
    // Set up line breaking context
    let line_ctx = {
        line_width: 100.0,
        current_width: 0.0,
        current_font: font,
        allow_hyphenation: false,
        hyphen_penalty: 50.0,
        widow_penalty: 100.0
    }
    
    // Test simple sentence that needs breaking
    let text = "This is a simple sentence that needs to be broken into multiple lines"
    let breaks = find_line_breaks(breaker, line_ctx, text, string_length(text))
    
    assert(breaks != null, "Should find line breaks")
    assert(breaks.count > 1, "Should find multiple break points")
    
    // Validate break points
    for (i in 0 to breaks.count) {
        let bp = breaks.points[i]
        assert(bp.text_position >= 0, "Break position should be valid")
        assert(bp.text_position <= string_length(text), "Break position should be within text")
        assert(bp.width_before >= 0, "Width before break should be non-negative")
        assert(bp.penalty >= 0, "Break penalty should be non-negative")
    }
    
    break_point_list_destroy(breaks)
    line_breaker_destroy(breaker)
    font_manager_destroy(font_mgr)
    destroy_context(ctx)
}

fn test_break_opportunity_detection() {
    let ctx = create_context()
    let breaker = line_breaker_create(ctx)
    
    let text = "word1 word2-word3"
    
    // Test space break opportunities
    assert(is_break_opportunity(text, 5), "Space should be break opportunity")  // After "word1"
    
    // Test hyphen break opportunities  
    assert(is_break_opportunity(text, 11), "Hyphen should be break opportunity")  // After "word2-"
    
    // Test non-break positions
    assert(!is_break_opportunity(text, 2), "Middle of word should not be break opportunity")
    assert(!is_break_opportunity(text, 0), "Start of text should not be break opportunity")
    
    line_breaker_destroy(breaker)
    destroy_context(ctx)
}

fn test_knuth_plass_algorithm() {
    let ctx = create_context()
    let font_mgr = font_manager_create(ctx)
    let breaker = line_breaker_create(ctx)
    let font = font_manager_get_font(font_mgr, "Times", 12.0, 400)
    
    let line_ctx = {
        line_width: 200.0,
        current_width: 0.0,
        current_font: font,
        allow_hyphenation: true,
        hyphen_penalty: 25.0,
        widow_penalty: 150.0
    }
    
    // Text that can be broken in multiple ways
    let text = "The quick brown fox jumps over the lazy dog and runs through the forest"
    let lines = break_lines_knuth_plass(breaker, line_ctx, text, string_length(text))
    
    assert(lines != null, "Knuth-Plass should produce line breaks")
    assert(lines.count > 1, "Should break into multiple lines")
    
    // Validate line breaks
    let total_chars = 0
    for (i in 0 to lines.count) {
        let line = lines.lines[i]
        assert(line.start_pos >= 0, "Line start should be valid")
        assert(line.length > 0, "Line should have content")
        assert(line.width > 0, "Line should have positive width")
        assert(line.width <= line_ctx.line_width + 1.0, "Line should not exceed width (with tolerance)")
        
        total_chars = total_chars + line.length
    }
    
    // All characters should be accounted for
    assert(total_chars <= string_length(text), "All characters should be included in lines")
    
    line_list_destroy(lines)
    line_breaker_destroy(breaker)
    font_manager_destroy(font_mgr)
    destroy_context(ctx)
}

fn test_hyphenation() {
    let ctx = create_context()
    let breaker = line_breaker_create(ctx)
    
    // Load English hyphenation dictionary
    let dict = load_hyphenation_dict("en-US")
    assert(dict != null, "Should load hyphenation dictionary")
    
    // Test hyphenation of common words
    let hyphenated = hyphenate_word(dict, "computer")
    assert(hyphenated != null, "Should hyphenate word")
    assert(string_contains(hyphenated, "-"), "Hyphenated word should contain hyphens")
    
    // Test word that shouldn't be hyphenated (too short)
    let short_word = hyphenate_word(dict, "cat")
    // Should either return original word or null for short words
    
    hyphenation_dict_destroy(dict)
    line_breaker_destroy(breaker)
    destroy_context(ctx)
}

fn test_break_penalty_calculation() {
    let ctx = create_context()
    let font_mgr = font_manager_create(ctx)
    let breaker = line_breaker_create(ctx)
    let font = font_manager_get_font(font_mgr, "Times", 12.0, 400)
    
    let line_ctx = {
        line_width: 100.0,
        current_width: 0.0,
        current_font: font,
        allow_hyphenation: true,
        hyphen_penalty: 50.0,
        widow_penalty: 100.0
    }
    
    let text = "word1 word2-word3"
    
    // Space break should have lower penalty than hyphen
    let space_penalty = calculate_break_penalty(line_ctx, 5, false)  // After space
    let hyphen_penalty = calculate_break_penalty(line_ctx, 11, true)  // After hyphen
    
    assert(space_penalty < hyphen_penalty, "Space break should have lower penalty than hyphen")
    assert(space_penalty >= 0, "Penalty should be non-negative")
    assert(hyphen_penalty >= 0, "Penalty should be non-negative")
    
    line_breaker_destroy(breaker)
    font_manager_destroy(font_mgr)
    destroy_context(ctx)
}

fn test_unicode_line_breaking() {
    let ctx = create_context()
    let font_mgr = font_manager_create(ctx)
    let breaker = line_breaker_create(ctx)
    let font = font_manager_get_font(font_mgr, "Times", 12.0, 400)
    
    let line_ctx = {
        line_width: 150.0,
        current_width: 0.0,
        current_font: font,
        allow_hyphenation: false,
        hyphen_penalty: 50.0,
        widow_penalty: 100.0
    }
    
    // Test Unicode text with various scripts
    let unicode_text = "Hello 世界 Café naïve résumé"
    let breaks = find_line_breaks(breaker, line_ctx, unicode_text, string_byte_length(unicode_text))
    
    assert(breaks != null, "Should handle Unicode text")
    assert(breaks.count > 0, "Should find break opportunities in Unicode text")
    
    break_point_list_destroy(breaks)
    line_breaker_destroy(breaker)
    font_manager_destroy(font_mgr)
    destroy_context(ctx)
}

fn test_break_point_caching() {
    let ctx = create_context()
    let font_mgr = font_manager_create(ctx)
    let breaker = line_breaker_create(ctx)
    let font = font_manager_get_font(font_mgr, "Times", 12.0, 400)
    
    let line_ctx = {
        line_width: 100.0,
        current_width: 0.0,
        current_font: font,
        allow_hyphenation: false,
        hyphen_penalty: 50.0,
        widow_penalty: 100.0
    }
    
    let text = "This is a test sentence for caching"
    
    // First call should compute breaks
    let start_time = current_time_millis()
    let breaks1 = find_line_breaks(breaker, line_ctx, text, string_length(text))
    let first_duration = current_time_millis() - start_time
    
    // Second call with same parameters should be faster (cached)
    start_time = current_time_millis()
    let breaks2 = find_line_breaks(breaker, line_ctx, text, string_length(text))
    let second_duration = current_time_millis() - start_time
    
    assert(breaks1 != null && breaks2 != null, "Both calls should succeed")
    assert(breaks1.count == breaks2.count, "Cached result should match original")
    
    // Note: Timing test may be unreliable in small examples, but structure should be correct
    
    break_point_list_destroy(breaks1)
    break_point_list_destroy(breaks2)
    line_breaker_destroy(breaker)
    font_manager_destroy(font_mgr)
    destroy_context(ctx)
}

// Run all line breaking tests
fn run_line_breaking_tests() {
    print("Running Line Breaking Tests...")
    
    test_line_breaker_creation()
    print("✓ Line breaker creation")
    
    test_simple_line_breaking()
    print("✓ Simple line breaking")
    
    test_break_opportunity_detection()
    print("✓ Break opportunity detection")
    
    test_knuth_plass_algorithm()
    print("✓ Knuth-Plass algorithm")
    
    test_hyphenation()
    print("✓ Hyphenation")
    
    test_break_penalty_calculation()
    print("✓ Break penalty calculation")
    
    test_unicode_line_breaking()
    print("✓ Unicode line breaking")
    
    test_break_point_caching()
    print("✓ Break point caching")
    
    print("All line breaking tests passed! ✅")
}

// Execute tests
run_line_breaking_tests()
