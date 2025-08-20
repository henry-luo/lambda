// Test: Font System (Week 1 Implementation)
// Tests font manager, font metrics, and text shaper components

fn test_font_manager_creation() {
    let ctx = create_context()
    let font_mgr = font_manager_create(ctx)
    
    assert(font_mgr != null, "Font manager should be created successfully")
    assert(font_mgr.font_cache != null, "Font cache should be initialized")
    assert(font_mgr.lambda_context == ctx, "Context should be stored correctly")
    
    font_manager_destroy(font_mgr)
    destroy_context(ctx)
}

fn test_font_loading() {
    let ctx = create_context()
    let font_mgr = font_manager_create(ctx)
    
    // Test default font loading
    let default_font = font_manager_get_font(font_mgr, "Times", 12.0, 400)
    assert(default_font != null, "Default font should load successfully")
    
    // Test font properties
    assert(default_font.family == "Times", "Font family should be set correctly")
    assert(default_font.size == 12.0, "Font size should be set correctly")
    assert(default_font.weight == 400, "Font weight should be set correctly")
    
    // Test font caching - should return same instance
    let cached_font = font_manager_get_font(font_mgr, "Times", 12.0, 400)
    assert(cached_font == default_font, "Font should be cached and reused")
    
    font_manager_destroy(font_mgr)
    destroy_context(ctx)
}

fn test_font_metrics() {
    let ctx = create_context()
    let font_mgr = font_manager_create(ctx)
    let font = font_manager_get_font(font_mgr, "Times", 12.0, 400)
    
    let metrics = font_get_metrics(font)
    
    // Test basic metrics
    assert(metrics.ascent > 0, "Font ascent should be positive")
    assert(metrics.descent > 0, "Font descent should be positive")
    assert(metrics.line_height > 0, "Line height should be positive")
    assert(metrics.x_height > 0, "X-height should be positive")
    assert(metrics.cap_height > 0, "Cap height should be positive")
    
    // Test logical relationships
    assert(metrics.line_height >= (metrics.ascent + metrics.descent), 
           "Line height should be at least ascent + descent")
    assert(metrics.cap_height <= metrics.ascent, 
           "Cap height should not exceed ascent")
    assert(metrics.x_height <= metrics.cap_height, 
           "X-height should not exceed cap height")
    
    font_manager_destroy(font_mgr)
    destroy_context(ctx)
}

fn test_text_measurement() {
    let ctx = create_context()
    let font_mgr = font_manager_create(ctx)
    let font = font_manager_get_font(font_mgr, "Times", 12.0, 400)
    
    // Test basic text measurement
    let width1 = font_measure_text_width(font, "Hello", 5)
    assert(width1 > 0, "Text width should be positive")
    
    // Test that longer text has greater width
    let width2 = font_measure_text_width(font, "Hello World", 11)
    assert(width2 > width1, "Longer text should have greater width")
    
    // Test empty text
    let width_empty = font_measure_text_width(font, "", 0)
    assert(width_empty == 0, "Empty text should have zero width")
    
    // Test single character
    let width_char = font_measure_text_width(font, "A", 1)
    assert(width_char > 0, "Single character should have positive width")
    
    font_manager_destroy(font_mgr)
    destroy_context(ctx)
}

fn test_text_shaping() {
    let ctx = create_context()
    let font_mgr = font_manager_create(ctx)
    let font = font_manager_get_font(font_mgr, "Times", 12.0, 400)
    
    // Test basic text shaping
    let result = text_shape(font, "Hello", 5)
    assert(result != null, "Text shaping should succeed")
    assert(result.glyph_count > 0, "Should produce glyphs")
    assert(result.glyphs != null, "Glyph array should be allocated")
    assert(result.positions != null, "Position array should be allocated")
    assert(result.total_width > 0, "Total width should be positive")
    
    // Test that glyph count matches expected
    assert(result.glyph_count == 5, "Should produce one glyph per character for simple text")
    
    // Test Unicode text shaping
    let unicode_result = text_shape(font, "Café", 5)  // Contains é (UTF-8)
    assert(unicode_result != null, "Unicode text shaping should succeed")
    assert(unicode_result.glyph_count > 0, "Unicode text should produce glyphs")
    
    text_shape_result_destroy(result)
    text_shape_result_destroy(unicode_result)
    font_manager_destroy(font_mgr)
    destroy_context(ctx)
}

fn test_font_fallback() {
    let ctx = create_context()
    let font_mgr = font_manager_create(ctx)
    
    // Test loading non-existent font (should fallback)
    let fallback_font = font_manager_get_font(font_mgr, "NonExistentFont", 12.0, 400)
    assert(fallback_font != null, "Should fallback to default font")
    
    // Test that fallback has reasonable properties
    let metrics = font_get_metrics(fallback_font)
    assert(metrics.ascent > 0, "Fallback font should have valid metrics")
    
    font_manager_destroy(font_mgr)
    destroy_context(ctx)
}

fn test_multiple_font_sizes() {
    let ctx = create_context()
    let font_mgr = font_manager_create(ctx)
    
    let font_small = font_manager_get_font(font_mgr, "Times", 8.0, 400)
    let font_medium = font_manager_get_font(font_mgr, "Times", 12.0, 400)
    let font_large = font_manager_get_font(font_mgr, "Times", 18.0, 400)
    
    let width_small = font_measure_text_width(font_small, "Test", 4)
    let width_medium = font_measure_text_width(font_medium, "Test", 4)
    let width_large = font_measure_text_width(font_large, "Test", 4)
    
    // Larger fonts should produce wider text
    assert(width_large > width_medium, "Large font should be wider than medium")
    assert(width_medium > width_small, "Medium font should be wider than small")
    
    font_manager_destroy(font_mgr)
    destroy_context(ctx)
}

// Run all font system tests
fn run_font_system_tests() {
    print("Running Font System Tests...")
    
    test_font_manager_creation()
    print("✓ Font manager creation")
    
    test_font_loading()
    print("✓ Font loading and caching")
    
    test_font_metrics()
    print("✓ Font metrics calculation")
    
    test_text_measurement()
    print("✓ Text width measurement")
    
    test_text_shaping()
    print("✓ Text shaping")
    
    test_font_fallback()
    print("✓ Font fallback")
    
    test_multiple_font_sizes()
    print("✓ Multiple font sizes")
    
    print("All font system tests passed! ✅")
}

// Execute tests
run_font_system_tests()
