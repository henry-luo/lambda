// tex_hyphen.hpp - TeX hyphenation using Liang's algorithm (TeXBook Appendix H)
//
// This implements the pattern-based hyphenation algorithm used by TeX.
// Patterns encode where hyphens can and cannot be inserted in words.
//
// Algorithm overview:
// 1. Surround word with boundary markers: ".word."
// 2. Find all matching patterns in the word
// 3. Patterns contain digits indicating hyphenation priority
// 4. Take maximum digit at each inter-letter position
// 5. Odd digits = hyphen allowed, even digits = hyphen forbidden
//
// Example: "hyphenation" with patterns:
//   hy3ph  -> ...3........ (allows hyphen after "hy")
//   he2n   -> ........2... (forbids hyphen before "n")
//   1na    -> .........1.. (allows hyphen before "na")
//   tio2n  -> ..........2. (forbids hyphen before final "n")

#ifndef TEX_HYPHEN_HPP
#define TEX_HYPHEN_HPP

#include "lib/arena.h"
#include <cstdint>
#include <cstddef>

namespace tex {

// Maximum word length for hyphenation
constexpr size_t MAX_HYPHEN_WORD = 64;

// Minimum characters before first hyphen (TeX default: 2)
constexpr int LEFT_HYPHEN_MIN = 2;

// Minimum characters after last hyphen (TeX default: 3)
constexpr int RIGHT_HYPHEN_MIN = 3;

// Hyphenation result for a word
struct HyphenResult {
    const char* word;           // Original word
    size_t word_len;            // Word length
    uint8_t points[MAX_HYPHEN_WORD];  // Hyphenation values at each position
    size_t hyphen_count;        // Number of valid hyphenation points

    // Check if hyphenation is allowed at position i (between chars i and i+1)
    bool can_hyphen_at(size_t i) const {
        if (i < LEFT_HYPHEN_MIN - 1) return false;
        if (i >= word_len - RIGHT_HYPHEN_MIN) return false;
        return (points[i] & 1) != 0;  // Odd = allowed
    }

    // Get all hyphenation positions (returns count, fills positions array)
    size_t get_hyphen_positions(size_t* positions, size_t max_positions) const;
};

// Trie node for pattern storage
struct HyphenTrieNode {
    HyphenTrieNode* children[27];  // a-z + boundary marker (.)
    uint8_t* values;               // Hyphenation values if this is end of pattern
    uint8_t value_count;           // Number of values
};

// Hyphenation engine
class HyphenEngine {
public:
    // Initialize with arena for allocations
    explicit HyphenEngine(Arena* arena);

    // Load patterns from a string (TeX .pat format)
    // Format: patterns separated by whitespace, e.g., "hy3ph he2n 1na"
    bool load_patterns(const char* patterns);

    // Load built-in US English patterns
    void load_us_english();

    // Hyphenate a single word (lowercase ASCII)
    HyphenResult hyphenate(const char* word, size_t len) const;

    // Hyphenate a word, handling case conversion
    HyphenResult hyphenate_word(const char* word, size_t len) const;

    // Get pattern count
    size_t pattern_count() const { return pattern_count_; }

private:
    Arena* arena_;
    HyphenTrieNode* root_;
    size_t pattern_count_;

    // Add a single pattern to the trie
    void add_pattern(const char* pattern, size_t len);

    // Get or create child node
    HyphenTrieNode* get_or_create_child(HyphenTrieNode* node, char c);

    // Get child node (const)
    const HyphenTrieNode* get_child(const HyphenTrieNode* node, char c) const;

    // Convert character to index (0-25 for a-z, 26 for .)
    static int char_to_index(char c);
};

// ============================================================================
// Discretionary hyphen nodes for line breaking
// ============================================================================

struct DiscNode {
    const char* pre_break;    // Text before break (e.g., "-")
    const char* post_break;   // Text after break (e.g., "")
    const char* no_break;     // Text if no break (e.g., "")
    float pre_width;          // Width of pre_break text
    float post_width;         // Width of post_break text
    float no_break_width;     // Width of no_break text
};

// Insert discretionary hyphen nodes into an HList
// Returns new HList with \discretionary nodes at hyphenation points
struct TexNode;
struct TFMFont;
struct FontSpec;

TexNode* insert_discretionary_hyphens(
    TexNode* hlist,
    const HyphenEngine* engine,
    const FontSpec& font,
    Arena* arena
);

// ============================================================================
// Global hyphenation engine (lazy initialized)
// ============================================================================

// Get the default US English hyphenation engine
HyphenEngine* get_us_english_hyphenator(Arena* arena);

}  // namespace tex

#endif  // TEX_HYPHEN_HPP
