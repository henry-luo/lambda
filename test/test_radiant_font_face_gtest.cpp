#include <gtest/gtest.h>
#include <memory>
#include <string>

#include "../radiant/font_face.h"
#include "../radiant/font.h"
#include "../radiant/view.hpp"
#include "../radiant/layout.hpp"

// Test fixture for font face tests
class FontFaceTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize font logging
        init_text_flow_logging();
        
        // Initialize layout context for testing
        lycon = (LayoutContext*)calloc(1, sizeof(LayoutContext));
        lycon->width = 800;
        lycon->height = 600;
        lycon->dpi = 96;
        
        // Initialize memory pools
        init_view_pool(lycon);
        
        // Initialize UI context for font testing
        uicon = (UiContext*)calloc(1, sizeof(UiContext));
        uicon->pixel_ratio = 1.0f;
        uicon->fontface_map = nullptr;
        
        // Initialize fallback fonts array
        static char* fallback_fonts[] = {"Arial", "Helvetica", "sans-serif", nullptr};
        uicon->fallback_fonts = fallback_fonts;
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
    }
    
    LayoutContext* lycon = nullptr;
    UiContext* uicon = nullptr;
};

// Test 1: Font face descriptor creation and initialization
TEST_F(FontFaceTest, FontFaceDescriptorCreation) {
    FontFaceDescriptor* descriptor = create_font_face_descriptor(lycon);
    
    ASSERT_NE(descriptor, nullptr) << "FontFaceDescriptor should be created successfully";
    
    // Test default values
    EXPECT_EQ(descriptor->family_name, nullptr);
    EXPECT_EQ(descriptor->src_local_path, nullptr);
    EXPECT_EQ(descriptor->src_local_name, nullptr);
    EXPECT_EQ(descriptor->font_style, LXB_CSS_VALUE_NORMAL);
    EXPECT_EQ(descriptor->font_weight, LXB_CSS_VALUE_NORMAL);
    EXPECT_EQ(descriptor->font_display, LXB_CSS_VALUE_AUTO);
    EXPECT_FALSE(descriptor->is_loaded);
    EXPECT_EQ(descriptor->loaded_face, nullptr);
    EXPECT_FALSE(descriptor->metrics_computed);
    EXPECT_EQ(descriptor->char_width_cache, nullptr);
    
    free(descriptor);
}

// Test 2: Font face descriptor with custom properties
TEST_F(FontFaceTest, FontFaceDescriptorCustomProperties) {
    FontFaceDescriptor* descriptor = create_font_face_descriptor(lycon);
    ASSERT_NE(descriptor, nullptr);
    
    // Set custom properties
    descriptor->family_name = strdup("CustomFont");
    descriptor->src_local_path = strdup("/path/to/font.ttf");
    descriptor->font_style = LXB_CSS_VALUE_ITALIC;
    descriptor->font_weight = LXB_CSS_VALUE_BOLD;
    descriptor->font_display = LXB_CSS_VALUE_SWAP;
    
    // Verify properties
    EXPECT_STREQ(descriptor->family_name, "CustomFont");
    EXPECT_STREQ(descriptor->src_local_path, "/path/to/font.ttf");
    EXPECT_EQ(descriptor->font_style, LXB_CSS_VALUE_ITALIC);
    EXPECT_EQ(descriptor->font_weight, LXB_CSS_VALUE_BOLD);
    EXPECT_EQ(descriptor->font_display, LXB_CSS_VALUE_SWAP);
    
    // Cleanup
    free(descriptor->family_name);
    free(descriptor->src_local_path);
    free(descriptor);
}

// Test 3: Character width caching functionality
TEST_F(FontFaceTest, CharacterWidthCaching) {
    FontFaceDescriptor* descriptor = create_font_face_descriptor(lycon);
    ASSERT_NE(descriptor, nullptr);
    
    // Test initial cache state
    EXPECT_EQ(descriptor->char_width_cache, nullptr);
    
    // Test cache miss
    int width = get_cached_char_width(descriptor, 'A');
    EXPECT_EQ(width, -1) << "Should return -1 for cache miss";
    
    // Test cache store
    cache_character_width(descriptor, 'A', 12);
    EXPECT_NE(descriptor->char_width_cache, nullptr) << "Cache should be created";
    
    // Test cache hit
    width = get_cached_char_width(descriptor, 'A');
    EXPECT_EQ(width, 12) << "Should return cached width";
    
    // Test different character (cache miss)
    width = get_cached_char_width(descriptor, 'B');
    EXPECT_EQ(width, -1) << "Should return -1 for different character";
    
    // Test multiple characters
    cache_character_width(descriptor, 'B', 10);
    cache_character_width(descriptor, 'C', 11);
    
    EXPECT_EQ(get_cached_char_width(descriptor, 'A'), 12);
    EXPECT_EQ(get_cached_char_width(descriptor, 'B'), 10);
    EXPECT_EQ(get_cached_char_width(descriptor, 'C'), 11);
    
    free(descriptor);
}

// Test 4: Font matching criteria and scoring
TEST_F(FontFaceTest, FontMatchingCriteria) {
    FontMatchCriteria criteria = {
        .family_name = "Arial",
        .weight = LXB_CSS_VALUE_NORMAL,
        .style = LXB_CSS_VALUE_NORMAL,
        .size = 16,
        .required_codepoint = 0
    };
    
    FontFaceDescriptor descriptor = {0};
    descriptor.family_name = strdup("Arial");
    descriptor.font_style = LXB_CSS_VALUE_NORMAL;
    descriptor.font_weight = LXB_CSS_VALUE_NORMAL;
    
    // Test perfect match
    float score = calculate_font_match_score(&descriptor, &criteria);
    EXPECT_FLOAT_EQ(score, 1.0f) << "Perfect match should score 1.0";
    
    // Test family name mismatch
    free(descriptor.family_name);
    descriptor.family_name = strdup("Helvetica");
    score = calculate_font_match_score(&descriptor, &criteria);
    EXPECT_LT(score, 1.0f) << "Family mismatch should reduce score";
    
    // Test weight mismatch
    descriptor.font_weight = LXB_CSS_VALUE_BOLD;
    score = calculate_font_match_score(&descriptor, &criteria);
    EXPECT_LT(score, 1.0f) << "Weight mismatch should reduce score";
    
    // Test style mismatch
    descriptor.font_style = LXB_CSS_VALUE_ITALIC;
    score = calculate_font_match_score(&descriptor, &criteria);
    EXPECT_LT(score, 1.0f) << "Style mismatch should reduce score";
    
    free(descriptor.family_name);
}

// Test 5: Font matching with best match selection
TEST_F(FontFaceTest, FontBestMatchSelection) {
    FontMatchCriteria criteria = {
        .family_name = "Arial",
        .weight = LXB_CSS_VALUE_NORMAL,
        .style = LXB_CSS_VALUE_NORMAL,
        .size = 16,
        .required_codepoint = 0
    };
    
    FontMatchResult result = find_best_font_match(uicon, &criteria);
    
    // Test result structure
    EXPECT_GE(result.match_score, 0.0f) << "Match score should be non-negative";
    EXPECT_LE(result.match_score, 1.0f) << "Match score should not exceed 1.0";
    
    // Note: In a real test environment with fonts installed, we would test:
    // - result.face != nullptr for successful matches
    // - result.is_exact_match for perfect matches
    // - result.requires_synthesis for synthetic styles
    // - result.supports_codepoint for character support
}

// Test 6: Font fallback chain construction
TEST_F(FontFaceTest, FontFallbackChain) {
    FontFallbackChain* chain = build_fallback_chain(uicon, "CustomFont");
    
    ASSERT_NE(chain, nullptr) << "Fallback chain should be created";
    EXPECT_GT(chain->family_count, 0) << "Should have at least one family";
    EXPECT_NE(chain->family_names, nullptr) << "Family names should be allocated";
    EXPECT_TRUE(chain->cache_enabled) << "Cache should be enabled by default";
    
    if (chain->family_names && chain->family_count > 0) {
        EXPECT_STREQ(chain->family_names[0], "CustomFont") << "First family should be the requested font";
    }
    
    // Test that fallback fonts are included
    bool found_fallback = false;
    for (int i = 0; i < chain->family_count; ++i) {
        if (strcmp(chain->family_names[i], "Arial") == 0 ||
            strcmp(chain->family_names[i], "Helvetica") == 0 ||
            strcmp(chain->family_names[i], "sans-serif") == 0) {
            found_fallback = true;
            break;
        }
    }
    EXPECT_TRUE(found_fallback) << "Should include system fallback fonts";
    
    free(chain);
}

// Test 7: Codepoint font mapping cache
TEST_F(FontFaceTest, CodepointFontMappingCache) {
    FontFallbackChain* chain = build_fallback_chain(uicon, "TestFont");
    ASSERT_NE(chain, nullptr);
    
    // Test initial cache state
    EXPECT_EQ(chain->codepoint_font_cache, nullptr);
    
    // Note: In a full test with actual fonts loaded, we would test:
    // - cache_codepoint_font_mapping(chain, 'A', face);
    // - FT_Face resolved_face = resolve_font_for_codepoint(chain, 'A', &style);
    // - Verify caching behavior
    
    free(chain);
}

// Test 8: Enhanced font metrics computation
TEST_F(FontFaceTest, EnhancedFontMetrics) {
    EnhancedFontBox fbox = {0};
    
    // Test initial state
    EXPECT_FALSE(fbox.metrics_computed);
    EXPECT_EQ(fbox.face, nullptr);
    
    // Test metrics computation without face (should handle gracefully)
    compute_enhanced_font_metrics(&fbox);
    EXPECT_FALSE(fbox.metrics_computed) << "Should not be computed without face";
    
    // Test with basic setup
    fbox.current_font_size = 16;
    fbox.cache_enabled = true;
    fbox.pixel_ratio = 1.0f;
    
    EXPECT_EQ(fbox.current_font_size, 16);
    EXPECT_TRUE(fbox.cache_enabled);
    EXPECT_FLOAT_EQ(fbox.pixel_ratio, 1.0f);
}

// Test 9: High-DPI font scaling
TEST_F(FontFaceTest, HighDPIFontScaling) {
    EnhancedFontBox fbox = {0};
    
    // Test pixel ratio application
    apply_pixel_ratio_to_font_metrics(&fbox, 2.0f);
    EXPECT_FLOAT_EQ(fbox.pixel_ratio, 2.0f);
    EXPECT_TRUE(fbox.high_dpi_aware);
    
    // Test font size scaling
    int scaled_size = scale_font_size_for_display(16, 2.0f);
    EXPECT_EQ(scaled_size, 32) << "Font size should be scaled by pixel ratio";
    
    // Test with 1.5x scaling
    scaled_size = scale_font_size_for_display(16, 1.5f);
    EXPECT_EQ(scaled_size, 24) << "Font size should be scaled correctly";
    
    // Test with no scaling
    scaled_size = scale_font_size_for_display(16, 1.0f);
    EXPECT_EQ(scaled_size, 16) << "Font size should not change with 1.0 ratio";
    
    // Test edge cases
    scaled_size = scale_font_size_for_display(16, 0.0f);
    EXPECT_EQ(scaled_size, 16) << "Should return original size for invalid ratio";
    
    scaled_size = scale_font_size_for_display(16, -1.0f);
    EXPECT_EQ(scaled_size, 16) << "Should return original size for negative ratio";
}

// Test 10: Character metrics scaling
TEST_F(FontFaceTest, CharacterMetricsScaling) {
    CharacterMetrics metrics = {0};
    metrics.codepoint = 'A';
    metrics.advance_x = 12;
    metrics.advance_y = 0;
    metrics.width = 10;
    metrics.height = 16;
    metrics.pixel_ratio = 1.0f;
    metrics.scaled_for_display = false;
    
    // Test initial values
    EXPECT_EQ(metrics.codepoint, 'A');
    EXPECT_EQ(metrics.advance_x, 12);
    EXPECT_EQ(metrics.width, 10);
    EXPECT_EQ(metrics.height, 16);
    EXPECT_FLOAT_EQ(metrics.pixel_ratio, 1.0f);
    EXPECT_FALSE(metrics.scaled_for_display);
    
    // Test scaling for display
    scale_character_metrics_for_display(&metrics, 2.0f);
    EXPECT_FLOAT_EQ(metrics.pixel_ratio, 2.0f);
    EXPECT_TRUE(metrics.scaled_for_display);
    
    // Note: In a full implementation, we would also test that
    // advance_x, width, and height are properly scaled
}

// Test 11: CSS line height calculation
TEST_F(FontFaceTest, CSSLineHeightCalculation) {
    EnhancedFontBox fbox = {0};
    fbox.current_font_size = 16;
    
    // Test normal line height
    int line_height = calculate_line_height_from_css(&fbox, LXB_CSS_VALUE_NORMAL);
    EXPECT_GT(line_height, 0) << "Line height should be positive";
    
    // Test with different CSS values
    // Note: In a full implementation, we would test various CSS line-height values
    // like LXB_CSS_VALUE_1_2 (1.2), specific pixel values, etc.
}

// Test 12: Font loading logging
TEST_F(FontFaceTest, FontLoadingLogging) {
    // Test that logging functions don't crash and can be called safely
    EXPECT_NO_THROW({
        log_font_loading_attempt("TestFont", "/path/to/font.ttf");
        log_font_loading_result("TestFont", true, nullptr);
        log_font_loading_result("TestFont", false, "File not found");
        log_font_cache_hit("TestFont", 16);
        log_font_fallback_triggered("RequestedFont", "FallbackFont");
    });
}

// Test 13: Memory management and cleanup
TEST_F(FontFaceTest, MemoryManagement) {
    // Test creating multiple descriptors
    std::vector<FontFaceDescriptor*> descriptors;
    
    for (int i = 0; i < 5; ++i) {
        FontFaceDescriptor* desc = create_font_face_descriptor(lycon);
        ASSERT_NE(desc, nullptr) << "Should create descriptor " << i;
        
        // Add some data to test cleanup
        desc->family_name = strdup("TestFont");
        cache_character_width(desc, 'A' + i, 10 + i);
        
        descriptors.push_back(desc);
    }
    
    // Verify all descriptors are valid
    for (size_t i = 0; i < descriptors.size(); ++i) {
        EXPECT_NE(descriptors[i], nullptr);
        EXPECT_STREQ(descriptors[i]->family_name, "TestFont");
        EXPECT_EQ(get_cached_char_width(descriptors[i], 'A' + i), 10 + i);
    }
    
    // Cleanup
    for (auto desc : descriptors) {
        free(desc->family_name);
        free(desc);
    }
    
    SUCCEED() << "Memory management test completed without crashes";
}

// Using gtest_main - no custom main needed
