#include <gtest/gtest.h>
#include "../lib/font_config.h"
#include "../lib/mempool.h"
#include "../lib/arena.h"
#include "../lib/log.h"

class FontConfigTest : public ::testing::Test {
protected:
    void SetUp() override {
        pool = pool_create();
        arena = arena_create(pool, ARENA_MEDIUM_CHUNK_SIZE, ARENA_LARGE_CHUNK_SIZE);
        db = font_database_create(pool, arena);
    }
    
    void TearDown() override {
        if (db) font_database_destroy(db);
        if (arena) arena_destroy(arena);
        if (pool) pool_destroy(pool);
    }
    
    Pool* pool;
    Arena* arena;
    FontDatabase* db;
};

TEST_F(FontConfigTest, DatabaseCreation) {
    ASSERT_NE(db, nullptr);
    EXPECT_EQ(font_database_get_font_count(db), 0);
    EXPECT_EQ(font_database_get_family_count(db), 0);
}

TEST_F(FontConfigTest, AddScanDirectory) {
    font_add_scan_directory(db, "/System/Library/Fonts");
    // Just test that it doesn't crash - we can't easily verify the internal state
}

TEST_F(FontConfigTest, FontScan) {
    // Add a test directory (this may not exist on all systems)
    font_add_scan_directory(db, "/System/Library/Fonts");
    
    // Scan should complete without crashing
    bool result = font_database_scan(db);
    EXPECT_TRUE(result);
    
    // Should have found some fonts (on macOS)
    size_t font_count = font_database_get_font_count(db);
    log_info("Found %zu fonts during scan", font_count);
    
    // Print statistics
    font_database_print_statistics(db);
}

TEST_F(FontConfigTest, FontMatching) {
    // Add system fonts directory
    font_add_scan_directory(db, "/System/Library/Fonts");
    font_database_scan(db);
    
    // Test basic font matching
    FontDatabaseCriteria criteria = {
        .family_name = "Arial",
        .weight = 400,
        .style = FONT_STYLE_NORMAL,
        .prefer_monospace = false,
        .required_codepoint = 0,
        .language = NULL
    };
    
    FontDatabaseResult result = font_database_find_best_match(db, &criteria);
    
    if (result.font) {
        EXPECT_GT(result.match_score, 0.0f);
        log_info("Found font match: %s (score: %.2f)", 
            result.font->family_name, result.match_score);
    } else {
        log_info("No font match found for Arial");
    }
}

TEST_F(FontConfigTest, UtilityFunctions) {
    // Test utility functions
    EXPECT_STREQ(font_format_to_string(FONT_FORMAT_TTF), "TTF");
    EXPECT_STREQ(font_format_to_string(FONT_FORMAT_OTF), "OTF");
    EXPECT_STREQ(font_style_to_string(FONT_STYLE_NORMAL), "Normal");
    EXPECT_STREQ(font_style_to_string(FONT_STYLE_ITALIC), "Italic");
    
    EXPECT_EQ(font_style_from_string("italic"), FONT_STYLE_ITALIC);
    EXPECT_EQ(font_style_from_string("normal"), FONT_STYLE_NORMAL);
    EXPECT_EQ(font_style_from_string("unknown"), FONT_STYLE_NORMAL);
}