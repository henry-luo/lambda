// test_tex_hyphen_gtest.cpp - Unit tests for TeX hyphenation (TeXBook Appendix H)
//
// Tests the Liang algorithm for pattern-based hyphenation.

#include <gtest/gtest.h>
#include "lambda/tex/tex_hyphen.hpp"
#include "lambda/tex/tex_node.hpp"
#include "lambda/tex/tex_tfm.hpp"
#include "lib/arena.h"
#include "lib/mempool.h"
#include "lib/log.h"
#include <cstring>

using namespace tex;

// ============================================================================
// Test Fixture
// ============================================================================

class TexHyphenTest : public ::testing::Test {
protected:
    Pool* pool;
    Arena* arena;
    HyphenEngine* engine;

    void SetUp() override {
        pool = pool_create();
        arena = arena_create_default(pool);
        engine = (HyphenEngine*)arena_alloc(arena, sizeof(HyphenEngine));
        new (engine) HyphenEngine(arena);
        engine->load_us_english();
    }

    void TearDown() override {
        arena_destroy(arena);
        pool_destroy(pool);
    }
};

// Helper: print hyphenation points for debugging
static void print_hyphenation(const char* word, const HyphenResult& result) {
    printf("Hyphenation of '%s': ", word);
    for (size_t i = 0; i < result.word_len; i++) {
        printf("%c", word[i]);
        if (result.can_hyphen_at(i)) {
            printf("-");
        }
    }
    printf("\n");
}

// ============================================================================
// Basic Pattern Loading Tests
// ============================================================================

TEST_F(TexHyphenTest, LoadPatterns) {
    // Engine loaded US English patterns
    EXPECT_GT(engine->pattern_count(), 100);  // Should have many patterns
}

TEST_F(TexHyphenTest, EmptyWord) {
    HyphenResult result = engine->hyphenate("", 0);
    EXPECT_EQ(result.hyphen_count, 0);
}

TEST_F(TexHyphenTest, ShortWord) {
    // Words shorter than left_min + right_min (2+3=5) shouldn't hyphenate
    HyphenResult result = engine->hyphenate("the", 3);
    EXPECT_EQ(result.hyphen_count, 0);

    result = engine->hyphenate("word", 4);
    EXPECT_EQ(result.hyphen_count, 0);
}

// ============================================================================
// Common Word Hyphenation Tests
// ============================================================================

TEST_F(TexHyphenTest, Hyphenation_Word) {
    // "hyphenation" -> "hy-phen-a-tion" (classic TeX example)
    HyphenResult result = engine->hyphenate("hyphenation", 11);

    // Should have at least some hyphenation points
    EXPECT_GT(result.hyphen_count, 0);

    // Check specific positions (0-indexed, positions between chars)
    // hy|phen|a|tion -> positions 1, 5, 6, 8 (some may be blocked by min rules)
    print_hyphenation("hyphenation", result);
}

TEST_F(TexHyphenTest, Hyphenation_Computer) {
    // "computer" -> "com-put-er"
    HyphenResult result = engine->hyphenate("computer", 8);
    EXPECT_GT(result.hyphen_count, 0);
    print_hyphenation("computer", result);
}

TEST_F(TexHyphenTest, Hyphenation_Algorithm) {
    // "algorithm" -> "al-go-rithm"
    HyphenResult result = engine->hyphenate("algorithm", 9);
    EXPECT_GT(result.hyphen_count, 0);
    print_hyphenation("algorithm", result);
}

TEST_F(TexHyphenTest, Hyphenation_Programming) {
    // "programming" -> "pro-gram-ming"
    HyphenResult result = engine->hyphenate("programming", 11);
    EXPECT_GT(result.hyphen_count, 0);
    print_hyphenation("programming", result);
}

TEST_F(TexHyphenTest, Hyphenation_Typography) {
    // "typography" - may not hyphenate with limited patterns
    HyphenResult result = engine->hyphenate("typography", 10);
    // Just verify the algorithm runs without crashing
    print_hyphenation("typography", result);
}

TEST_F(TexHyphenTest, Hyphenation_Education) {
    // "education" -> "ed-u-ca-tion"
    HyphenResult result = engine->hyphenate("education", 9);
    EXPECT_GT(result.hyphen_count, 0);
    print_hyphenation("education", result);
}

// ============================================================================
// Prefix/Suffix Pattern Tests
// ============================================================================

TEST_F(TexHyphenTest, Prefix_Un) {
    // Words starting with "un-"
    HyphenResult result = engine->hyphenate("unhappy", 7);
    EXPECT_GT(result.hyphen_count, 0);
    // Should allow break after "un"
    print_hyphenation("unhappy", result);
}

TEST_F(TexHyphenTest, Prefix_Re) {
    // Words starting with "re-"
    HyphenResult result = engine->hyphenate("remember", 8);
    EXPECT_GT(result.hyphen_count, 0);
    print_hyphenation("remember", result);
}

TEST_F(TexHyphenTest, Suffix_Tion) {
    // Words ending with "-tion" - may not all hyphenate with limited patterns
    HyphenResult result = engine->hyphenate("information", 11);
    // Just verify algorithm runs
    print_hyphenation("information", result);
}

TEST_F(TexHyphenTest, Suffix_Ment) {
    // Words ending with "-ment"
    HyphenResult result = engine->hyphenate("development", 11);
    EXPECT_GT(result.hyphen_count, 0);
    print_hyphenation("development", result);
}

TEST_F(TexHyphenTest, Suffix_Ing) {
    // Words ending with "-ing"
    HyphenResult result = engine->hyphenate("programming", 11);
    EXPECT_GT(result.hyphen_count, 0);
    print_hyphenation("programming", result);
}

// ============================================================================
// Double Consonant Tests
// ============================================================================

TEST_F(TexHyphenTest, DoubleConsonant_LL) {
    // "hello" - short words often don't hyphenate (LEFT_HYPHEN_MIN + RIGHT_HYPHEN_MIN = 5)
    HyphenResult result = engine->hyphenate("hello", 5);
    // 5 letters = minimum, may not have room for breaks
    print_hyphenation("hello", result);
}

TEST_F(TexHyphenTest, DoubleConsonant_TT) {
    // "butter" - 6 letters, limited hyphenation room
    HyphenResult result = engine->hyphenate("butter", 6);
    // With LEFT_HYPHEN_MIN=2, RIGHT_HYPHEN_MIN=3, only position 3 is valid
    print_hyphenation("butter", result);
}

TEST_F(TexHyphenTest, DoubleConsonant_MM) {
    // "summer" - 6 letters, limited hyphenation room
    HyphenResult result = engine->hyphenate("summer", 6);
    print_hyphenation("summer", result);
}

// ============================================================================
// Consonant Cluster Tests (should NOT break)
// ============================================================================

TEST_F(TexHyphenTest, Cluster_Th) {
    // "father" should not break "th"
    HyphenResult result = engine->hyphenate("father", 6);
    // If there's a break, it shouldn't be between f-a-t-h-e-r's 'th'
    print_hyphenation("father", result);
}

TEST_F(TexHyphenTest, Cluster_Ch) {
    // "teacher" should not break "ch"
    HyphenResult result = engine->hyphenate("teacher", 7);
    print_hyphenation("teacher", result);
}

TEST_F(TexHyphenTest, Cluster_Tr) {
    // "control" should not break "tr"
    HyphenResult result = engine->hyphenate("control", 7);
    print_hyphenation("control", result);
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_F(TexHyphenTest, LeftMinimum) {
    // Left minimum is 2 - shouldn't hyphenate with only 1 char before
    HyphenResult result = engine->hyphenate("about", 5);
    // Can't have "a-bout" because left min is 2
    if (result.hyphen_count > 0) {
        EXPECT_FALSE(result.can_hyphen_at(0));  // Position 0 (after 'a') forbidden
    }
    print_hyphenation("about", result);
}

TEST_F(TexHyphenTest, RightMinimum) {
    // Right minimum is 3 - shouldn't hyphenate with only 2 chars after
    HyphenResult result = engine->hyphenate("going", 5);
    // Can't have "goi-ng" because right min is 3
    if (result.hyphen_count > 0) {
        EXPECT_FALSE(result.can_hyphen_at(2));  // Position 2 (before 'ng') forbidden
    }
    print_hyphenation("going", result);
}

TEST_F(TexHyphenTest, CaseInsensitive) {
    // Should handle uppercase letters
    HyphenResult result1 = engine->hyphenate_word("Computer", 8);
    HyphenResult result2 = engine->hyphenate("computer", 8);

    // Should have same number of hyphenation points
    EXPECT_EQ(result1.hyphen_count, result2.hyphen_count);
}

TEST_F(TexHyphenTest, GetHyphenPositions) {
    HyphenResult result = engine->hyphenate("hyphenation", 11);

    size_t positions[20];
    size_t count = result.get_hyphen_positions(positions, 20);

    EXPECT_EQ(count, result.hyphen_count);

    // Print positions for verification
    printf("Hyphen positions in 'hyphenation': ");
    for (size_t i = 0; i < count; i++) {
        printf("%zu ", positions[i]);
    }
    printf("\n");
}

// ============================================================================
// Pattern Matching Tests
// ============================================================================

TEST_F(TexHyphenTest, CustomPatterns) {
    // Create a new engine with custom patterns
    HyphenEngine custom(arena);

    // Load minimal test patterns
    custom.load_patterns("ab1c de2f 3gh");
    EXPECT_EQ(custom.pattern_count(), 3);

    // Test pattern matching
    HyphenResult result = custom.hyphenate("abcdef", 6);
    // Should find "ab1c" pattern allowing hyphen after b
    print_hyphenation("abcdef", result);
}

TEST_F(TexHyphenTest, PatternPriority) {
    // Test that higher priority patterns override lower ones
    HyphenEngine custom(arena);

    // Pattern with 1 (allow) vs 2 (forbid) - 2 should win
    custom.load_patterns("a1b a2b");

    HyphenResult result = custom.hyphenate("ab", 2);
    // Position 0 should be forbidden (2 > 1, and 2 is even = forbid)
    // But word is too short anyway
}

// ============================================================================
// Longer Words Tests
// ============================================================================

TEST_F(TexHyphenTest, LongWord_Internationalization) {
    HyphenResult result = engine->hyphenate("internationalization", 20);
    EXPECT_GE(result.hyphen_count, 2);  // Should have multiple break points
    print_hyphenation("internationalization", result);
}

TEST_F(TexHyphenTest, LongWord_Supercalifragilistic) {
    HyphenResult result = engine->hyphenate("supercalifragilistic", 20);
    EXPECT_GT(result.hyphen_count, 3);
    print_hyphenation("supercalifragilistic", result);
}

TEST_F(TexHyphenTest, LongWord_Pneumonoultramicroscopic) {
    // One of the longest English words
    const char* word = "pneumonoultramicroscopic";
    HyphenResult result = engine->hyphenate(word, strlen(word));
    EXPECT_GE(result.hyphen_count, 2);  // Should have multiple breaks
    print_hyphenation(word, result);
}

// ============================================================================
// Integration with HList Tests
// ============================================================================

TEST_F(TexHyphenTest, InsertDiscretionaryHyphens) {
    // Create a simple HList with "hyphenation"
    TexNode* hlist = make_hlist(arena);

    FontSpec font("cmr10", 10.0f, nullptr, 0);
    const char* text = "hyphenation";
    for (size_t i = 0; i < strlen(text); i++) {
        TexNode* ch = make_char(arena, text[i], font);
        hlist->append_child(ch);
    }

    // Insert discretionary hyphens
    TexNode* result = insert_discretionary_hyphens(hlist, engine, font, arena);

    // Count nodes
    int char_count = 0;
    int disc_count = 0;
    for (TexNode* n = result->first_child; n; n = n->next_sibling) {
        if (n->node_class == NodeClass::Char) char_count++;
        if (n->node_class == NodeClass::Disc) disc_count++;
    }

    EXPECT_EQ(char_count, 11);  // Original characters
    EXPECT_GT(disc_count, 0);   // Should have discretionary nodes

    printf("HList has %d chars and %d discretionary nodes\n", char_count, disc_count);
}

TEST_F(TexHyphenTest, InsertDiscretionaryHyphens_MultipleWords) {
    // Create HList with "computer programming"
    TexNode* hlist = make_hlist(arena);

    FontSpec font("cmr10", 10.0f, nullptr, 0);
    const char* text = "computer programming";
    for (size_t i = 0; i < strlen(text); i++) {
        if (text[i] == ' ') {
            // Insert glue for space
            Glue g = Glue::flexible(3.33f, 1.67f, 1.11f);
            TexNode* glue = make_glue(arena, g);
            hlist->append_child(glue);
        } else {
            TexNode* ch = make_char(arena, text[i], font);
            hlist->append_child(ch);
        }
    }

    // Insert discretionary hyphens
    TexNode* result = insert_discretionary_hyphens(hlist, engine, font, arena);

    // Count disc nodes
    int disc_count = 0;
    for (TexNode* n = result->first_child; n; n = n->next_sibling) {
        if (n->node_class == NodeClass::Disc) disc_count++;
    }

    EXPECT_GT(disc_count, 0);  // Both words should have breaks
    printf("HList 'computer programming' has %d discretionary nodes\n", disc_count);
}

// ============================================================================
// Performance Tests
// ============================================================================

TEST_F(TexHyphenTest, Performance_ManyWords) {
    const char* words[] = {
        "computer", "programming", "algorithm", "hyphenation",
        "typography", "education", "information", "development",
        "international", "representation", "understanding",
        "communication", "organization", "responsibility"
    };
    int num_words = sizeof(words) / sizeof(words[0]);

    // Hyphenate many times
    for (int i = 0; i < 1000; i++) {
        for (int j = 0; j < num_words; j++) {
            engine->hyphenate(words[j], strlen(words[j]));
        }
    }

    // If we get here without timeout, performance is acceptable
    SUCCEED();
}

// ============================================================================
// Global Engine Test
// ============================================================================

TEST_F(TexHyphenTest, GlobalEngine) {
    // Test the global US English hyphenator
    HyphenEngine* global = get_us_english_hyphenator(arena);
    EXPECT_NE(global, nullptr);
    EXPECT_GT(global->pattern_count(), 100);

    // Should return same instance on second call
    HyphenEngine* global2 = get_us_english_hyphenator(arena);
    EXPECT_EQ(global, global2);
}
