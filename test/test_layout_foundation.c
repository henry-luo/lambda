/*
 * C Tests for Text Layout Foundation (Weeks 1-4)
 * Provides low-level testing of the implementation in C
 */

#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Include the implemented headers
#include "../../../typeset/font/font_manager.h"
#include "../../../typeset/font/font_metrics.h"
#include "../../../typeset/font/text_shaper.h"
#include "../../../typeset/layout/line_breaker.h"
#include "../../../typeset/layout/text_flow.h"
#include "../../../typeset/layout/vertical_metrics.h"

// Test data
static Context* test_context = NULL;

// Setup and teardown
void setup_tests(void) {
    test_context = context_create(1024 * 1024);  // 1MB context
    cr_assert_not_null(test_context, "Test context should be created");
}

void teardown_tests(void) {
    if (test_context) {
        context_destroy(test_context);
        test_context = NULL;
    }
}

// Font Manager Tests
Test(font_manager, creation, .init = setup_tests, .fini = teardown_tests) {
    FontManager* mgr = font_manager_create(test_context);
    cr_assert_not_null(mgr, "Font manager should be created");
    cr_assert_eq(mgr->lambda_context, test_context, "Context should be stored");
    cr_assert_not_null(mgr->font_cache, "Font cache should be initialized");
    
    font_manager_destroy(mgr);
}

Test(font_manager, font_loading, .init = setup_tests, .fini = teardown_tests) {
    FontManager* mgr = font_manager_create(test_context);
    
    ViewFont* font = font_manager_get_font(mgr, "Times", 12.0, 400);
    cr_assert_not_null(font, "Font should be loaded");
    cr_assert_str_eq(font->family, "Times", "Font family should be set");
    cr_assert_float_eq(font->size, 12.0, 0.01, "Font size should be set");
    cr_assert_eq(font->weight, 400, "Font weight should be set");
    
    // Test caching - should return same instance
    ViewFont* cached_font = font_manager_get_font(mgr, "Times", 12.0, 400);
    cr_assert_eq(font, cached_font, "Font should be cached");
    
    font_manager_destroy(mgr);
}

Test(font_manager, font_fallback, .init = setup_tests, .fini = teardown_tests) {
    FontManager* mgr = font_manager_create(test_context);
    
    // Test with non-existent font
    ViewFont* fallback = font_manager_get_font(mgr, "NonExistentFont", 12.0, 400);
    cr_assert_not_null(fallback, "Should fallback to default font");
    
    font_manager_destroy(mgr);
}

// Font Metrics Tests
Test(font_metrics, basic_metrics, .init = setup_tests, .fini = teardown_tests) {
    FontManager* mgr = font_manager_create(test_context);
    ViewFont* font = font_manager_get_font(mgr, "Times", 12.0, 400);
    
    FontMetrics metrics = font_get_metrics(font);
    
    cr_assert_gt(metrics.ascent, 0.0, "Ascent should be positive");
    cr_assert_gt(metrics.descent, 0.0, "Descent should be positive");
    cr_assert_gt(metrics.line_height, 0.0, "Line height should be positive");
    cr_assert_gt(metrics.x_height, 0.0, "X-height should be positive");
    cr_assert_gt(metrics.cap_height, 0.0, "Cap height should be positive");
    
    // Logical relationships
    cr_assert_geq(metrics.line_height, metrics.ascent + metrics.descent, 
                  "Line height should be at least ascent + descent");
    cr_assert_leq(metrics.cap_height, metrics.ascent, 
                  "Cap height should not exceed ascent");
    cr_assert_leq(metrics.x_height, metrics.cap_height, 
                  "X-height should not exceed cap height");
    
    font_manager_destroy(mgr);
}

Test(font_metrics, text_measurement, .init = setup_tests, .fini = teardown_tests) {
    FontManager* mgr = font_manager_create(test_context);
    ViewFont* font = font_manager_get_font(mgr, "Times", 12.0, 400);
    
    double width1 = font_measure_text_width(font, "Hello", 5);
    cr_assert_gt(width1, 0.0, "Text width should be positive");
    
    double width2 = font_measure_text_width(font, "Hello World", 11);
    cr_assert_gt(width2, width1, "Longer text should have greater width");
    
    double empty_width = font_measure_text_width(font, "", 0);
    cr_assert_float_eq(empty_width, 0.0, 0.01, "Empty text should have zero width");
    
    font_manager_destroy(mgr);
}

// Text Shaper Tests
Test(text_shaper, basic_shaping, .init = setup_tests, .fini = teardown_tests) {
    FontManager* mgr = font_manager_create(test_context);
    ViewFont* font = font_manager_get_font(mgr, "Times", 12.0, 400);
    
    TextShapeResult* result = text_shape(font, "Hello", 5);
    cr_assert_not_null(result, "Text shaping should succeed");
    cr_assert_gt(result->glyph_count, 0, "Should produce glyphs");
    cr_assert_not_null(result->glyphs, "Glyph array should be allocated");
    cr_assert_not_null(result->positions, "Position array should be allocated");
    cr_assert_gt(result->total_width, 0.0, "Total width should be positive");
    
    text_shape_result_destroy(result);
    font_manager_destroy(mgr);
}

Test(text_shaper, unicode_shaping, .init = setup_tests, .fini = teardown_tests) {
    FontManager* mgr = font_manager_create(test_context);
    ViewFont* font = font_manager_get_font(mgr, "Arial Unicode MS", 12.0, 400);
    
    // Test Unicode text with diacritics
    const char* unicode_text = "Café naïve résumé";
    TextShapeResult* result = text_shape(font, unicode_text, strlen(unicode_text));
    
    cr_assert_not_null(result, "Unicode text shaping should succeed");
    cr_assert_gt(result->glyph_count, 0, "Unicode text should produce glyphs");
    cr_assert_gt(result->total_width, 0.0, "Unicode text should have positive width");
    
    text_shape_result_destroy(result);
    font_manager_destroy(mgr);
}

// Line Breaker Tests
Test(line_breaker, creation, .init = setup_tests, .fini = teardown_tests) {
    LineBreaker* breaker = line_breaker_create(test_context);
    cr_assert_not_null(breaker, "Line breaker should be created");
    cr_assert_eq(breaker->lambda_context, test_context, "Context should be stored");
    
    line_breaker_destroy(breaker);
}

Test(line_breaker, break_detection, .init = setup_tests, .fini = teardown_tests) {
    LineBreaker* breaker = line_breaker_create(test_context);
    
    const char* text = "word1 word2-word3";
    
    cr_assert(is_break_opportunity(text, 5), "Space should be break opportunity");
    cr_assert(is_break_opportunity(text, 11), "Hyphen should be break opportunity");
    cr_assert_not(is_break_opportunity(text, 2), "Middle of word should not be break opportunity");
    
    line_breaker_destroy(breaker);
}

Test(line_breaker, simple_breaking, .init = setup_tests, .fini = teardown_tests) {
    LineBreaker* breaker = line_breaker_create(test_context);
    FontManager* mgr = font_manager_create(test_context);
    ViewFont* font = font_manager_get_font(mgr, "Times", 12.0, 400);
    
    LineBreakContext ctx = {
        .line_width = 100.0,
        .current_width = 0.0,
        .current_font = font,
        .allow_hyphenation = false,
        .hyphen_penalty = 50.0,
        .widow_penalty = 100.0
    };
    
    const char* text = "This is a simple sentence that needs to be broken";
    BreakPointList* breaks = find_line_breaks(breaker, &ctx, text, strlen(text));
    
    cr_assert_not_null(breaks, "Should find line breaks");
    cr_assert_gt(breaks->count, 1, "Should find multiple break points");
    
    // Validate break points
    for (int i = 0; i < breaks->count; i++) {
        BreakPoint* bp = &breaks->points[i];
        cr_assert_geq(bp->text_position, 0, "Break position should be valid");
        cr_assert_leq(bp->text_position, (int)strlen(text), "Break position should be within text");
        cr_assert_geq(bp->width_before, 0.0, "Width before break should be non-negative");
        cr_assert_geq(bp->penalty, 0.0, "Break penalty should be non-negative");
    }
    
    break_point_list_destroy(breaks);
    font_manager_destroy(mgr);
    line_breaker_destroy(breaker);
}

// Text Flow Tests
Test(text_flow, creation, .init = setup_tests, .fini = teardown_tests) {
    TextFlow* flow = text_flow_create(test_context);
    cr_assert_not_null(flow, "Text flow should be created");
    cr_assert_eq(flow->lambda_context, test_context, "Context should be stored");
    
    text_flow_destroy(flow);
}

Test(text_flow, basic_layout, .init = setup_tests, .fini = teardown_tests) {
    TextFlow* flow = text_flow_create(test_context);
    FontManager* mgr = font_manager_create(test_context);
    ViewFont* font = font_manager_get_font(mgr, "Times", 12.0, 400);
    
    TextFlowContext ctx = {
        .content_area = {0.0, 0.0, 200.0, 300.0},
        .current_x = 0.0,
        .current_y = 0.0,
        .line_height = 14.0,
        .text_align = TEXT_ALIGN_LEFT,
        .word_spacing = 0.0,
        .letter_spacing = 0.0,
        .paragraph_indent = 0.0,
        .paragraph_spacing = 12.0
    };
    
    const char* text = "This is a sample paragraph for text flow testing.";
    TextFlowResult* result = text_flow_layout(flow, &ctx, text, strlen(text), font);
    
    cr_assert_not_null(result, "Text flow should succeed");
    cr_assert_gt(result->line_count, 0, "Should create lines");
    cr_assert_gt(result->total_height, 0.0, "Should have positive total height");
    
    text_flow_result_destroy(result);
    font_manager_destroy(mgr);
    text_flow_destroy(flow);
}

// Vertical Metrics Tests
Test(vertical_metrics, creation, .init = setup_tests, .fini = teardown_tests) {
    VerticalMetrics* vm = vertical_metrics_create(test_context);
    cr_assert_not_null(vm, "Vertical metrics should be created");
    cr_assert_eq(vm->lambda_context, test_context, "Context should be stored");
    
    vertical_metrics_destroy(vm);
}

Test(vertical_metrics, baseline_calculation, .init = setup_tests, .fini = teardown_tests) {
    VerticalMetrics* vm = vertical_metrics_create(test_context);
    FontManager* mgr = font_manager_create(test_context);
    ViewFont* font = font_manager_get_font(mgr, "Times", 12.0, 400);
    
    BaselineInfo info = calculate_baseline_info(font);
    
    cr_assert_float_eq(info.alphabetic, 0.0, 0.01, "Alphabetic baseline should be reference");
    cr_assert_lt(info.ideographic, 0.0, "Ideographic baseline should be below alphabetic");
    cr_assert_gt(info.hanging, 0.0, "Hanging baseline should be above alphabetic");
    cr_assert_geq(info.mathematical, 0.0, "Mathematical baseline should be at or above alphabetic");
    
    font_manager_destroy(mgr);
    vertical_metrics_destroy(vm);
}

// Integration Tests
Test(integration, complete_pipeline, .init = setup_tests, .fini = teardown_tests) {
    // Test complete text layout pipeline
    FontManager* mgr = font_manager_create(test_context);
    LineBreaker* breaker = line_breaker_create(test_context);
    TextFlow* flow = text_flow_create(test_context);
    VerticalMetrics* vm = vertical_metrics_create(test_context);
    
    ViewFont* font = font_manager_get_font(mgr, "Times", 12.0, 400);
    cr_assert_not_null(font, "Font should load");
    
    // Test font metrics
    FontMetrics metrics = font_get_metrics(font);
    cr_assert_gt(metrics.ascent, 0.0, "Font metrics should be valid");
    
    // Test text shaping
    TextShapeResult* shape_result = text_shape(font, "Test", 4);
    cr_assert_not_null(shape_result, "Text shaping should work");
    
    // Test line breaking
    LineBreakContext break_ctx = {
        .line_width = 100.0,
        .current_font = font,
        .allow_hyphenation = false
    };
    BreakPointList* breaks = find_line_breaks(breaker, &break_ctx, "Test text", 9);
    cr_assert_not_null(breaks, "Line breaking should work");
    
    // Test text flow
    TextFlowContext flow_ctx = {
        .content_area = {0.0, 0.0, 200.0, 300.0},
        .line_height = 14.0,
        .text_align = TEXT_ALIGN_LEFT
    };
    TextFlowResult* flow_result = text_flow_layout(flow, &flow_ctx, "Test", 4, font);
    cr_assert_not_null(flow_result, "Text flow should work");
    
    // Test baseline calculation
    BaselineInfo baseline = calculate_baseline_info(font);
    cr_assert_float_eq(baseline.alphabetic, 0.0, 0.01, "Baseline calculation should work");
    
    // Cleanup
    text_flow_result_destroy(flow_result);
    break_point_list_destroy(breaks);
    text_shape_result_destroy(shape_result);
    vertical_metrics_destroy(vm);
    text_flow_destroy(flow);
    line_breaker_destroy(breaker);
    font_manager_destroy(mgr);
}

// Performance Tests
Test(performance, font_loading_speed, .init = setup_tests, .fini = teardown_tests) {
    FontManager* mgr = font_manager_create(test_context);
    
    clock_t start = clock();
    for (int i = 0; i < 100; i++) {
        ViewFont* font = font_manager_get_font(mgr, "Times", 12.0, 400);
        cr_assert_not_null(font, "Font loading should succeed");
    }
    clock_t end = clock();
    
    double duration = ((double)(end - start)) / CLOCKS_PER_SEC * 1000.0;  // Convert to ms
    cr_assert_lt(duration, 100.0, "Font loading should be fast (< 100ms for 100 loads)");
    
    font_manager_destroy(mgr);
}

Test(performance, text_measurement_speed, .init = setup_tests, .fini = teardown_tests) {
    FontManager* mgr = font_manager_create(test_context);
    ViewFont* font = font_manager_get_font(mgr, "Times", 12.0, 400);
    
    const char* test_text = "Sample text for measurement performance testing";
    
    clock_t start = clock();
    for (int i = 0; i < 1000; i++) {
        double width = font_measure_text_width(font, test_text, strlen(test_text));
        cr_assert_gt(width, 0.0, "Text measurement should work");
    }
    clock_t end = clock();
    
    double duration = ((double)(end - start)) / CLOCKS_PER_SEC * 1000.0;
    cr_assert_lt(duration, 500.0, "Text measurement should be fast (< 500ms for 1000 measurements)");
    
    font_manager_destroy(mgr);
}

// Memory Tests
Test(memory, no_leaks, .init = setup_tests, .fini = teardown_tests) {
    size_t initial_memory = context_get_used_memory(test_context);
    
    // Create and destroy components multiple times
    for (int i = 0; i < 10; i++) {
        FontManager* mgr = font_manager_create(test_context);
        LineBreaker* breaker = line_breaker_create(test_context);
        TextFlow* flow = text_flow_create(test_context);
        VerticalMetrics* vm = vertical_metrics_create(test_context);
        
        ViewFont* font = font_manager_get_font(mgr, "Times", 12.0, 400);
        TextShapeResult* result = text_shape(font, "Test", 4);
        
        text_shape_result_destroy(result);
        vertical_metrics_destroy(vm);
        text_flow_destroy(flow);
        line_breaker_destroy(breaker);
        font_manager_destroy(mgr);
    }
    
    size_t final_memory = context_get_used_memory(test_context);
    size_t memory_growth = final_memory - initial_memory;
    
    // Allow some memory growth for caches, but it should be reasonable
    cr_assert_lt(memory_growth, initial_memory / 10, "Memory growth should be minimal");
}
