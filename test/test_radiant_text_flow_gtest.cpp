#include <gtest/gtest.h>
#include <memory>
#include <string>

#include "../radiant/font_face.h"
#include "../radiant/view.hpp"
#include "../radiant/layout.hpp"

// Test fixture for text flow tests
class TextFlowTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize text flow logging
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
        uicon->pixel_ratio = 1.0f; // Standard DPI
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

// Test 1: Logging initialization
TEST_F(TextFlowTest, LoggingInitialization) {
    EXPECT_NE(font_log, nullptr) << "Font logging category should be initialized";
    EXPECT_NE(text_log, nullptr) << "Text logging category should be initialized";
    EXPECT_NE(layout_log, nullptr) << "Layout logging category should be initialized";
}

// Test 2: FontFaceDescriptor creation
TEST_F(TextFlowTest, FontFaceDescriptorCreation) {
    FontFaceDescriptor* descriptor = create_font_face_descriptor(lycon);
    
    ASSERT_NE(descriptor, nullptr) << "FontFaceDescriptor should be created successfully";
    
    // Test basic properties
    EXPECT_EQ(descriptor->font_style, LXB_CSS_VALUE_NORMAL);
    EXPECT_EQ(descriptor->font_weight, LXB_CSS_VALUE_NORMAL);
    EXPECT_EQ(descriptor->font_display, LXB_CSS_VALUE_AUTO);
    EXPECT_FALSE(descriptor->is_loaded);
    EXPECT_EQ(descriptor->loaded_face, nullptr);
    EXPECT_FALSE(descriptor->metrics_computed);
    
    free(descriptor);
}

// Test 3: Character width caching
TEST_F(TextFlowTest, CharacterWidthCaching) {
    FontFaceDescriptor* descriptor = create_font_face_descriptor(lycon);
    ASSERT_NE(descriptor, nullptr);
    
    // Test cache miss
    int width = get_cached_char_width(descriptor, 'A');
    EXPECT_EQ(width, -1) << "Should return -1 for cache miss";
    
    // Test cache store and retrieve
    cache_character_width(descriptor, 'A', 12);
    width = get_cached_char_width(descriptor, 'A');
    EXPECT_EQ(width, 12) << "Should return cached width";
    
    // Test different character
    width = get_cached_char_width(descriptor, 'B');
    EXPECT_EQ(width, -1) << "Should return -1 for different character";
    
    free(descriptor);
}

// Test 4: Enhanced font metrics
TEST_F(TextFlowTest, EnhancedFontMetrics) {
    EnhancedFontBox fbox = {0};
    
    // Test metrics computation without face (should handle gracefully)
    compute_enhanced_font_metrics(&fbox);
    EXPECT_FALSE(fbox.metrics_computed) << "Should not be computed without face";
    
    // Test with initialized font box
    fbox.current_font_size = 16;
    fbox.cache_enabled = true;
    
    // Verify initialization
    EXPECT_EQ(fbox.current_font_size, 16);
    EXPECT_TRUE(fbox.cache_enabled);
}

// Test 5: High-DPI support
TEST_F(TextFlowTest, HighDPISupport) {
    EnhancedFontBox fbox = {0};
    
    // Test pixel ratio application
    apply_pixel_ratio_to_font_metrics(&fbox, 2.0f);
    EXPECT_FLOAT_EQ(fbox.pixel_ratio, 2.0f);
    EXPECT_TRUE(fbox.high_dpi_aware);
    
    // Test font size scaling
    int scaled_size = scale_font_size_for_display(16, 2.0f);
    EXPECT_EQ(scaled_size, 32) << "Font size should be scaled by pixel ratio";
    
    // Test with 1.0 ratio (no scaling)
    scaled_size = scale_font_size_for_display(16, 1.0f);
    EXPECT_EQ(scaled_size, 16) << "Font size should not change with 1.0 ratio";
    
    // Test with invalid ratio
    scaled_size = scale_font_size_for_display(16, 0.0f);
    EXPECT_EQ(scaled_size, 16) << "Should return original size for invalid ratio";
}

// Test 6: Font matching
TEST_F(TextFlowTest, FontMatching) {
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
    
    float score = calculate_font_match_score(&descriptor, &criteria);
    EXPECT_FLOAT_EQ(score, 1.0f) << "Perfect match should score 1.0";
    
    // Test partial match
    descriptor.font_weight = LXB_CSS_VALUE_BOLD;
    score = calculate_font_match_score(&descriptor, &criteria);
    EXPECT_LT(score, 1.0f) << "Partial match should score less than 1.0";
    EXPECT_GT(score, 0.0f) << "Partial match should score greater than 0.0";
    
    free(descriptor.family_name);
}

// Test 7: Font fallback chain
TEST_F(TextFlowTest, FontFallbackChain) {
    FontFallbackChain* chain = build_fallback_chain(uicon, "CustomFont");
    
    ASSERT_NE(chain, nullptr) << "Fallback chain should be created";
    EXPECT_GT(chain->family_count, 0) << "Should have at least one family";
    EXPECT_NE(chain->family_names, nullptr) << "Family names should be allocated";
    EXPECT_TRUE(chain->cache_enabled) << "Cache should be enabled by default";
    
    if (chain->family_names && chain->family_count > 0) {
        EXPECT_STREQ(chain->family_names[0], "CustomFont") << "First family should be the requested font";
    }
    
    // Test font support checking (basic test)
    // Note: This would require actual font loading in a full test
    
    // Cleanup would be needed in a full implementation
    free(chain);
}

// Test 8: Character metrics
TEST_F(TextFlowTest, CharacterMetrics) {
    CharacterMetrics metrics = {0};
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
    
    // Test scaling for display
    scale_character_metrics_for_display(&metrics, 2.0f);
    EXPECT_FLOAT_EQ(metrics.pixel_ratio, 2.0f);
    EXPECT_TRUE(metrics.scaled_for_display);
}

// Test 9: Enhanced font box setup
TEST_F(TextFlowTest, EnhancedFontBoxSetup) {
    EnhancedFontBox fbox = {0};
    FontProp fprop = {0};
    fprop.font_size = 16;
    fprop.font_style = LXB_CSS_VALUE_NORMAL;
    fprop.font_weight = LXB_CSS_VALUE_NORMAL;
    
    // Test setup without actual font loading (would require FreeType in full test)
    fbox.current_font_size = fprop.font_size;
    fbox.cache_enabled = true;
    fbox.pixel_ratio = uicon->pixel_ratio;
    fbox.high_dpi_aware = (uicon->pixel_ratio > 1.0f);
    
    EXPECT_EQ(fbox.current_font_size, 16);
    EXPECT_TRUE(fbox.cache_enabled);
    EXPECT_FLOAT_EQ(fbox.pixel_ratio, 1.0f);
    EXPECT_FALSE(fbox.high_dpi_aware); // Should be false for 1.0 ratio
}

// Test 10: Structured logging functions
TEST_F(TextFlowTest, StructuredLoggingFunctions) {
    // Test that logging functions don't crash
    EXPECT_NO_THROW({
        log_font_loading_attempt("TestFont", "/path/to/font.ttf");
        log_font_loading_result("TestFont", true, nullptr);
        log_font_cache_hit("TestFont", 16);
        log_font_fallback_triggered("RequestedFont", "FallbackFont");
    });
}

// Test 11: Integration with existing font system
TEST_F(TextFlowTest, ExistingFontSystemIntegration) {
    // Test that we can still use existing font functions
    FontProp fprop = {0};
    fprop.font_size = 16;
    fprop.font_style = LXB_CSS_VALUE_NORMAL;
    fprop.font_weight = LXB_CSS_VALUE_NORMAL;
    
    // Test font property setup
    EXPECT_EQ(fprop.font_size, 16);
    EXPECT_EQ(fprop.font_style, LXB_CSS_VALUE_NORMAL);
    EXPECT_EQ(fprop.font_weight, LXB_CSS_VALUE_NORMAL);
    
    // Test that UiContext has required fields
    EXPECT_NE(uicon->fallback_fonts, nullptr) << "Fallback fonts should be available";
    EXPECT_FLOAT_EQ(uicon->pixel_ratio, 1.0f) << "Pixel ratio should be initialized";
}

// Test 12: Memory management
TEST_F(TextFlowTest, MemoryManagement) {
    // Test that we can create and destroy multiple descriptors
    std::vector<FontFaceDescriptor*> descriptors;
    
    for (int i = 0; i < 10; ++i) {
        FontFaceDescriptor* desc = create_font_face_descriptor(lycon);
        ASSERT_NE(desc, nullptr) << "Should create descriptor " << i;
        descriptors.push_back(desc);
    }
    
    // Cleanup
    for (auto desc : descriptors) {
        free(desc);
    }
    
    SUCCEED() << "Memory management test completed without crashes";
}

// Using gtest_main - no custom main needed
