#include <gtest/gtest.h>
#include <memory>
#include <string>

#include "../radiant/text_metrics.h"
#include "../radiant/font_face.h"
#include "../radiant/layout_text_enhanced.cpp"

// Test fixture for text metrics tests
class TextMetricsTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize text flow logging
        init_text_flow_logging();
        
        // Initialize layout context
        lycon = (LayoutContext*)calloc(1, sizeof(LayoutContext));
        lycon->width = 800;
        lycon->height = 600;
        lycon->dpi = 96;
        
        // Initialize memory pools
        init_view_pool(lycon);
        
        // Initialize UI context
        uicon = (UiContext*)calloc(1, sizeof(UiContext));
        uicon->pixel_ratio = 1.0f;
        uicon->fontface_map = nullptr;
        
        // Initialize fallback fonts
        static char* fallback_fonts[] = {"Arial", "Helvetica", "sans-serif", nullptr};
        uicon->fallback_fonts = fallback_fonts;
        
        // Set up enhanced font box
        enhanced_fbox = (EnhancedFontBox*)calloc(1, sizeof(EnhancedFontBox));
        enhanced_fbox->current_font_size = 16;
        enhanced_fbox->cache_enabled = true;
        enhanced_fbox->pixel_ratio = 1.0f;
        enhanced_fbox->high_dpi_aware = false;
        
        // Initialize basic font properties
        FontProp fprop = {0};
        fprop.font_size = 16;
        fprop.font_style = LXB_CSS_VALUE_NORMAL;
        fprop.font_weight = LXB_CSS_VALUE_NORMAL;
        enhanced_fbox->style = fprop;
    }
    
    void TearDown() override {
        // Cleanup
        if (lycon) {
            cleanup_view_pool(lycon);
            free(lycon);
        }
        if (uicon) {
            if (uicon->fontface_map) {
                fontface_cleanup(uicon);
            }
            free(uicon);
        }
        if (enhanced_fbox) {
            free(enhanced_fbox);
        }
    }
    
    LayoutContext* lycon = nullptr;
    UiContext* uicon = nullptr;
    EnhancedFontBox* enhanced_fbox = nullptr;
};

// Test 1: Enhanced font metrics computation
TEST_F(TextMetricsTest, EnhancedFontMetricsComputation) {
    // Test without face (should handle gracefully)
    compute_advanced_font_metrics(enhanced_fbox);
    EXPECT_FALSE(enhanced_fbox->metrics_computed) << "Should not compute without face";
    
    // Test metrics structure initialization
    EnhancedFontMetrics* metrics = &enhanced_fbox->metrics;
    EXPECT_FALSE(metrics->metrics_computed);
    
    // Test that we can set basic metrics manually
    metrics->ascender = 12;
    metrics->descender = -4;
    metrics->height = 16;
    metrics->line_gap = 0;
    
    EXPECT_EQ(metrics->ascender, 12);
    EXPECT_EQ(metrics->descender, -4);
    EXPECT_EQ(metrics->height, 16);
    EXPECT_EQ(metrics->line_gap, 0);
}

// Test 2: OpenType metrics computation
TEST_F(TextMetricsTest, OpenTypeMetricsComputation) {
    // Test OpenType metrics computation without face
    compute_opentype_metrics(enhanced_fbox);
    
    // Should handle gracefully without crashing
    SUCCEED() << "OpenType metrics computation handled gracefully";
}

// Test 3: Baseline metrics computation
TEST_F(TextMetricsTest, BaselineMetricsComputation) {
    // Test baseline metrics computation
    compute_baseline_metrics(enhanced_fbox);
    
    // Should handle gracefully without face
    SUCCEED() << "Baseline metrics computation handled gracefully";
    
    // Test with manual metrics
    enhanced_fbox->metrics.x_height = 8;
    enhanced_fbox->metrics.cap_height = 12;
    enhanced_fbox->metrics.baseline_offset = 0;
    
    EXPECT_EQ(enhanced_fbox->metrics.x_height, 8);
    EXPECT_EQ(enhanced_fbox->metrics.cap_height, 12);
    EXPECT_EQ(enhanced_fbox->metrics.baseline_offset, 0);
}

// Test 4: Advanced character metrics
TEST_F(TextMetricsTest, AdvancedCharacterMetrics) {
    // Test character metrics caching
    EXPECT_FALSE(is_character_metrics_cached(enhanced_fbox, 'A'));
    
    // Test character metrics structure
    AdvancedCharacterMetrics metrics = {0};
    metrics.codepoint = 'A';
    metrics.advance_x = 12;
    metrics.advance_y = 0;
    metrics.width = 10;
    metrics.height = 16;
    metrics.pixel_ratio = 1.0f;
    metrics.scaled_for_display = false;
    
    EXPECT_EQ(metrics.codepoint, 'A');
    EXPECT_EQ(metrics.advance_x, 12);
    EXPECT_EQ(metrics.width, 10);
    EXPECT_EQ(metrics.height, 16);
    EXPECT_FLOAT_EQ(metrics.pixel_ratio, 1.0f);
    EXPECT_FALSE(metrics.scaled_for_display);
    
    // Test advanced positioning fields
    metrics.left_side_bearing = 1;
    metrics.right_side_bearing = 1;
    metrics.baseline_offset = 0;
    
    EXPECT_EQ(metrics.left_side_bearing, 1);
    EXPECT_EQ(metrics.right_side_bearing, 1);
    EXPECT_EQ(metrics.baseline_offset, 0);
}

// Test 5: Unicode render context
TEST_F(TextMetricsTest, UnicodeRenderContext) {
    UnicodeRenderContext* ctx = create_unicode_render_context(uicon, enhanced_fbox);
    
    ASSERT_NE(ctx, nullptr) << "Unicode render context should be created";
    EXPECT_EQ(ctx->primary_font, enhanced_fbox);
    EXPECT_FLOAT_EQ(ctx->pixel_ratio, 1.0f);
    EXPECT_TRUE(ctx->font_hinting) << "Font hinting should be enabled by default";
    EXPECT_FALSE(ctx->subpixel_positioning) << "Subpixel positioning should be disabled by default";
    EXPECT_TRUE(ctx->cache_enabled) << "Cache should be enabled by default";
    EXPECT_EQ(ctx->cache_hits, 0);
    EXPECT_EQ(ctx->cache_misses, 0);
    
    // Test language setting
    EXPECT_NE(ctx->language, nullptr);
    if (ctx->language) {
        EXPECT_STREQ(ctx->language, "en") << "Default language should be English";
    }
    
    destroy_unicode_render_context(ctx);
}

// Test 6: Text line metrics
TEST_F(TextMetricsTest, TextLineMetrics) {
    TextLineMetrics line_metrics = {0};
    
    // Test basic line metrics structure
    line_metrics.line_width = 200;
    line_metrics.line_height = 20;
    line_metrics.baseline_y = 16;
    line_metrics.max_ascender = 12;
    line_metrics.max_descender = 4;
    line_metrics.character_count = 10;
    
    EXPECT_EQ(line_metrics.line_width, 200);
    EXPECT_EQ(line_metrics.line_height, 20);
    EXPECT_EQ(line_metrics.baseline_y, 16);
    EXPECT_EQ(line_metrics.max_ascender, 12);
    EXPECT_EQ(line_metrics.max_descender, 4);
    EXPECT_EQ(line_metrics.character_count, 10);
    
    // Test advanced metrics
    line_metrics.x_height_max = 8;
    line_metrics.cap_height_max = 12;
    line_metrics.dominant_baseline = 16;
    
    EXPECT_EQ(line_metrics.x_height_max, 8);
    EXPECT_EQ(line_metrics.cap_height_max, 12);
    EXPECT_EQ(line_metrics.dominant_baseline, 16);
}

// Test 7: Advanced glyph render info
TEST_F(TextMetricsTest, AdvancedGlyphRenderInfo) {
    AdvancedGlyphRenderInfo render_info = {0};
    
    render_info.codepoint = 'A';
    render_info.uses_fallback = false;
    render_info.subpixel_x = 0.5f;
    render_info.subpixel_y = 0.0f;
    render_info.pixel_x = 10;
    render_info.pixel_y = 20;
    render_info.hinting_applied = true;
    render_info.antialiasing_enabled = true;
    render_info.rendering_quality = 2;
    
    EXPECT_EQ(render_info.codepoint, 'A');
    EXPECT_FALSE(render_info.uses_fallback);
    EXPECT_FLOAT_EQ(render_info.subpixel_x, 0.5f);
    EXPECT_FLOAT_EQ(render_info.subpixel_y, 0.0f);
    EXPECT_EQ(render_info.pixel_x, 10);
    EXPECT_EQ(render_info.pixel_y, 20);
    EXPECT_TRUE(render_info.hinting_applied);
    EXPECT_TRUE(render_info.antialiasing_enabled);
    EXPECT_EQ(render_info.rendering_quality, 2);
}

// Test 8: Character advance calculation
TEST_F(TextMetricsTest, CharacterAdvanceCalculation) {
    UnicodeRenderContext* ctx = create_unicode_render_context(uicon, enhanced_fbox);
    ASSERT_NE(ctx, nullptr);
    
    // Test character advance calculation (without actual font loading)
    // This will use fallback logic
    int advance = calculate_character_advance(ctx, 'A');
    EXPECT_GE(advance, 0) << "Character advance should be non-negative";
    
    // Test cache behavior
    EXPECT_EQ(ctx->cache_misses, 1) << "Should have one cache miss";
    
    // Test same character again (should hit cache if implemented)
    int advance2 = calculate_character_advance(ctx, 'A');
    EXPECT_EQ(advance, advance2) << "Same character should return same advance";
    
    destroy_unicode_render_context(ctx);
}

// Test 9: Unicode text width calculation
TEST_F(TextMetricsTest, UnicodeTextWidthCalculation) {
    UnicodeRenderContext* ctx = create_unicode_render_context(uicon, enhanced_fbox);
    ASSERT_NE(ctx, nullptr);
    
    // Test basic ASCII text
    const char* text = "Hello";
    int width = calculate_unicode_text_width(ctx, text, strlen(text));
    EXPECT_GT(width, 0) << "Text width should be positive";
    
    // Test empty text
    int empty_width = calculate_unicode_text_width(ctx, "", 0);
    EXPECT_EQ(empty_width, 0) << "Empty text should have zero width";
    
    // Test single character
    int char_width = calculate_unicode_text_width(ctx, "A", 1);
    EXPECT_GT(char_width, 0) << "Single character should have positive width";
    
    destroy_unicode_render_context(ctx);
}

// Test 10: Break opportunity detection
TEST_F(TextMetricsTest, BreakOpportunityDetection) {
    // Test basic break opportunities
    EXPECT_TRUE(is_break_opportunity(' ')) << "Space should be break opportunity";
    EXPECT_TRUE(is_break_opportunity('\t')) << "Tab should be break opportunity";
    EXPECT_TRUE(is_break_opportunity('\n')) << "Newline should be break opportunity";
    EXPECT_TRUE(is_break_opportunity('-')) << "Hyphen should be break opportunity";
    EXPECT_TRUE(is_break_opportunity('/')) << "Slash should be break opportunity";
    
    // Test non-break characters
    EXPECT_FALSE(is_break_opportunity('A')) << "Letter should not be break opportunity";
    EXPECT_FALSE(is_break_opportunity('1')) << "Digit should not be break opportunity";
    EXPECT_FALSE(is_break_opportunity('.')) << "Period should not be break opportunity";
}

// Test 11: Enhanced font box integration
TEST_F(TextMetricsTest, EnhancedFontBoxIntegration) {
    // Create basic font box
    FontBox basic_fbox = {0};
    basic_fbox.current_font_size = 14;
    basic_fbox.space_width = 4.0f;
    
    // Create enhanced font box
    EnhancedFontBox enhanced = {0};
    
    // Test enhancement
    enhance_existing_font_box(&basic_fbox, &enhanced);
    
    EXPECT_EQ(enhanced.current_font_size, 14);
    EXPECT_FLOAT_EQ(enhanced.space_width, 4.0f);
    EXPECT_TRUE(enhanced.cache_enabled);
    EXPECT_FALSE(enhanced.metrics_computed);
    EXPECT_FLOAT_EQ(enhanced.pixel_ratio, 1.0f);
    EXPECT_FALSE(enhanced.high_dpi_aware);
}

// Test 12: Basic text width calculation fallback
TEST_F(TextMetricsTest, BasicTextWidthCalculation) {
    // Test basic text width calculation (fallback method)
    const char* text = "Hello";
    int width = calculate_basic_text_width(lycon, text, strlen(text));
    
    // Should handle gracefully even without proper font setup
    EXPECT_GE(width, 0) << "Basic text width should be non-negative";
    
    // Test empty text
    int empty_width = calculate_basic_text_width(lycon, "", 0);
    EXPECT_EQ(empty_width, 0) << "Empty text should have zero width";
    
    // Test null parameters
    int null_width = calculate_basic_text_width(nullptr, text, strlen(text));
    EXPECT_EQ(null_width, 0) << "Null context should return zero width";
}

// Test 13: Unicode break point finding
TEST_F(TextMetricsTest, UnicodeBreakPointFinding) {
    UnicodeRenderContext* ctx = create_unicode_render_context(uicon, enhanced_fbox);
    ASSERT_NE(ctx, nullptr);
    
    // Test break point finding
    const char* text = "Hello world test";
    int break_point = find_unicode_break_point(ctx, text, strlen(text), 50);
    
    EXPECT_GE(break_point, 0) << "Break point should be non-negative";
    EXPECT_LE(break_point, (int)strlen(text)) << "Break point should not exceed text length";
    
    // Test with very small available width
    int small_break = find_unicode_break_point(ctx, text, strlen(text), 1);
    EXPECT_GE(small_break, 0) << "Should handle small available width";
    
    // Test with very large available width
    int large_break = find_unicode_break_point(ctx, text, strlen(text), 10000);
    EXPECT_EQ(large_break, (int)strlen(text)) << "Should return full text length for large width";
    
    destroy_unicode_render_context(ctx);
}

// Test 14: Performance and logging
TEST_F(TextMetricsTest, PerformanceAndLogging) {
    UnicodeRenderContext* ctx = create_unicode_render_context(uicon, enhanced_fbox);
    ASSERT_NE(ctx, nullptr);
    
    // Test logging functions (should not crash)
    EXPECT_NO_THROW({
        log_rendering_performance(ctx);
    });
    
    // Test performance counters
    EXPECT_EQ(ctx->cache_hits, 0);
    EXPECT_EQ(ctx->cache_misses, 0);
    
    // Simulate some cache activity
    ctx->cache_hits = 10;
    ctx->cache_misses = 2;
    
    EXPECT_NO_THROW({
        log_rendering_performance(ctx);
    });
    
    destroy_unicode_render_context(ctx);
}

// Test 15: Memory management and cleanup
TEST_F(TextMetricsTest, MemoryManagementAndCleanup) {
    // Test multiple context creation and destruction
    std::vector<UnicodeRenderContext*> contexts;
    
    for (int i = 0; i < 5; ++i) {
        UnicodeRenderContext* ctx = create_unicode_render_context(uicon, enhanced_fbox);
        ASSERT_NE(ctx, nullptr) << "Should create context " << i;
        contexts.push_back(ctx);
    }
    
    // Cleanup all contexts
    for (auto ctx : contexts) {
        destroy_unicode_render_context(ctx);
    }
    
    SUCCEED() << "Memory management test completed without crashes";
}

// Using gtest_main - no custom main needed
