// tex_hyphen.cpp - TeX hyphenation using Liang's algorithm (TeXBook Appendix H)

#include "tex_hyphen.hpp"
#include "tex_node.hpp"
#include "tex_tfm.hpp"
#include "lib/log.h"
#include <cstring>
#include <cctype>

namespace tex {

// ============================================================================
// HyphenResult implementation
// ============================================================================

size_t HyphenResult::get_hyphen_positions(size_t* positions, size_t max_positions) const {
    size_t count = 0;
    for (size_t i = LEFT_HYPHEN_MIN - 1; i < word_len - RIGHT_HYPHEN_MIN && count < max_positions; i++) {
        if (points[i] & 1) {  // Odd = allowed
            positions[count++] = i;
        }
    }
    return count;
}

// ============================================================================
// HyphenEngine implementation
// ============================================================================

HyphenEngine::HyphenEngine(Arena* arena)
    : arena_(arena)
    , root_(nullptr)
    , pattern_count_(0)
{
    // Create root node
    root_ = (HyphenTrieNode*)arena_alloc(arena_, sizeof(HyphenTrieNode));
    memset(root_, 0, sizeof(HyphenTrieNode));
}

int HyphenEngine::char_to_index(char c) {
    if (c >= 'a' && c <= 'z') return c - 'a';
    if (c == '.') return 26;
    return -1;  // Invalid
}

HyphenTrieNode* HyphenEngine::get_or_create_child(HyphenTrieNode* node, char c) {
    int idx = char_to_index(c);
    if (idx < 0) return nullptr;

    if (!node->children[idx]) {
        node->children[idx] = (HyphenTrieNode*)arena_alloc(arena_, sizeof(HyphenTrieNode));
        memset(node->children[idx], 0, sizeof(HyphenTrieNode));
    }
    return node->children[idx];
}

const HyphenTrieNode* HyphenEngine::get_child(const HyphenTrieNode* node, char c) const {
    int idx = char_to_index(c);
    if (idx < 0) return nullptr;
    return node->children[idx];
}

void HyphenEngine::add_pattern(const char* pattern, size_t len) {
    // Parse pattern: extract letters and digit values
    // Example: "hy3ph" -> letters="hyph", values at positions [0,0,3,0,0]

    char letters[MAX_HYPHEN_WORD];
    uint8_t values[MAX_HYPHEN_WORD + 1];
    size_t letter_count = 0;
    size_t value_idx = 0;

    memset(values, 0, sizeof(values));

    for (size_t i = 0; i < len; i++) {
        char c = pattern[i];
        if (c >= '0' && c <= '9') {
            values[value_idx] = c - '0';
        } else if ((c >= 'a' && c <= 'z') || c == '.') {
            letters[letter_count++] = c;
            value_idx = letter_count;
        }
    }

    if (letter_count == 0) return;

    // Insert into trie
    HyphenTrieNode* node = root_;
    for (size_t i = 0; i < letter_count; i++) {
        node = get_or_create_child(node, letters[i]);
        if (!node) return;
    }

    // Store values at terminal node
    node->value_count = letter_count + 1;
    node->values = (uint8_t*)arena_alloc(arena_, node->value_count);
    memcpy(node->values, values, node->value_count);

    pattern_count_++;
}

bool HyphenEngine::load_patterns(const char* patterns) {
    if (!patterns) return false;

    const char* p = patterns;
    while (*p) {
        // Skip whitespace
        while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
        if (!*p) break;

        // Find pattern end
        const char* start = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') p++;

        size_t len = p - start;
        if (len > 0 && len < MAX_HYPHEN_WORD) {
            add_pattern(start, len);
        }
    }

    log_debug("tex_hyphen: loaded %zu patterns", pattern_count_);
    return pattern_count_ > 0;
}

HyphenResult HyphenEngine::hyphenate(const char* word, size_t len) const {
    HyphenResult result;
    result.word = word;
    result.word_len = len;
    result.hyphen_count = 0;
    memset(result.points, 0, sizeof(result.points));

    if (len < LEFT_HYPHEN_MIN + RIGHT_HYPHEN_MIN || len >= MAX_HYPHEN_WORD - 2) {
        return result;  // Too short or too long
    }

    // Build search string with boundary markers: ".word."
    char search[MAX_HYPHEN_WORD + 2];
    search[0] = '.';
    memcpy(search + 1, word, len);
    search[len + 1] = '.';
    size_t search_len = len + 2;

    // Values array (one more than letters to handle positions between letters)
    uint8_t values[MAX_HYPHEN_WORD + 3];
    memset(values, 0, sizeof(values));

    // Find all matching patterns
    for (size_t start = 0; start < search_len; start++) {
        const HyphenTrieNode* node = root_;

        for (size_t i = start; i < search_len && node; i++) {
            node = get_child(node, search[i]);
            if (!node) break;

            // If this node has values, apply them
            if (node->values) {
                for (size_t j = 0; j < node->value_count; j++) {
                    size_t pos = start + j;
                    if (node->values[j] > values[pos]) {
                        values[pos] = node->values[j];
                    }
                }
            }
        }
    }

    // Copy values, adjusting for the leading "."
    // Position i in word corresponds to position i+1 in search string
    for (size_t i = 0; i < len; i++) {
        result.points[i] = values[i + 1];
        if ((values[i + 1] & 1) && i >= LEFT_HYPHEN_MIN - 1 && i < len - RIGHT_HYPHEN_MIN) {
            result.hyphen_count++;
        }
    }

    return result;
}

HyphenResult HyphenEngine::hyphenate_word(const char* word, size_t len) const {
    // Convert to lowercase for lookup
    char lower[MAX_HYPHEN_WORD];
    if (len >= MAX_HYPHEN_WORD) {
        HyphenResult result;
        result.word = word;
        result.word_len = len;
        result.hyphen_count = 0;
        memset(result.points, 0, sizeof(result.points));
        return result;
    }

    for (size_t i = 0; i < len; i++) {
        lower[i] = tolower((unsigned char)word[i]);
    }

    HyphenResult result = hyphenate(lower, len);
    result.word = word;  // Keep original word pointer
    return result;
}

// ============================================================================
// US English hyphenation patterns
// ============================================================================

// These patterns are a subset of the standard TeX US English patterns
// Full patterns available from CTAN: https://ctan.org/pkg/hyphen-english
//
// Format: patterns are space-separated, digits indicate hyphenation values
// Odd digits = hyphen allowed, even digits = hyphen forbidden
// Higher digits take precedence

static const char* US_ENGLISH_PATTERNS =
    // Common prefixes
    ".un1 "
    ".re1 "
    ".pre1 "
    ".dis1 "
    ".mis1 "
    ".over1 "
    ".under1 "
    ".anti1 "
    ".semi1 "
    ".super1 "
    ".sub1 "
    ".trans1 "
    ".inter1 "
    ".multi1 "
    ".non1 "
    ".out1 "
    ".up1 "
    ".down1 "
    ".fore1 "
    ".back1 "
    ".self1 "
    ".cross1 "
    ".counter1 "
    ".extra1 "
    ".infra1 "
    ".ultra1 "
    ".micro1 "
    ".macro1 "

    // Common suffixes
    "1tion. "
    "1sion. "
    "1ment. "
    "1ness. "
    "1less. "
    "1able. "
    "1ible. "
    "1ful. "
    "1ing. "
    "1ings. "
    "1ism. "
    "1ist. "
    "1ity. "
    "1ive. "
    "1ize. "
    "1ly. "
    "1ous. "
    "1al. "
    "1er. "
    "1or. "
    "1ary. "
    "1ery. "
    "1ory. "
    "1ward. "
    "1wise. "
    "1dom. "
    "1hood. "
    "1ship. "
    "1work. "
    "1man. "
    "1men. "

    // Double consonant patterns (break between doubles)
    "b1b "
    "c1c "
    "d1d "
    "f1f "
    "g1g "
    "l1l "
    "m1m "
    "n1n "
    "p1p "
    "r1r "
    "s1s "
    "t1t "
    "z1z "

    // Consonant clusters that stay together
    "2bl "
    "2br "
    "2ch "
    "2ck "
    "2cl "
    "2cr "
    "2dr "
    "2fl "
    "2fr "
    "2gh "
    "2gl "
    "2gn "
    "2gr "
    "2kn "
    "2ph "
    "2pl "
    "2pr "
    "2qu "
    "2sc "
    "2sh "
    "2sk "
    "2sl "
    "2sm "
    "2sn "
    "2sp "
    "2st "
    "2sw "
    "2th "
    "2tr "
    "2tw "
    "2wh "
    "2wr "

    // Vowel-consonant-vowel patterns (break before consonant)
    "a1ba "
    "a1be "
    "a1bi "
    "a1bo "
    "a1bu "
    "a1ca "
    "a1ce "
    "a1ci "
    "a1co "
    "a1cu "
    "a1da "
    "a1de "
    "a1di "
    "a1do "
    "a1du "
    "a1fa "
    "a1fe "
    "a1fi "
    "a1fo "
    "a1fu "
    "a1ga "
    "a1ge "
    "a1gi "
    "a1go "
    "a1gu "
    "a1la "
    "a1le "
    "a1li "
    "a1lo "
    "a1lu "
    "a1ma "
    "a1me "
    "a1mi "
    "a1mo "
    "a1mu "
    "a1na "
    "a1ne "
    "a1ni "
    "a1no "
    "a1nu "
    "a1pa "
    "a1pe "
    "a1pi "
    "a1po "
    "a1pu "
    "a1ra "
    "a1re "
    "a1ri "
    "a1ro "
    "a1ru "
    "a1sa "
    "a1se "
    "a1si "
    "a1so "
    "a1su "
    "a1ta "
    "a1te "
    "a1ti "
    "a1to "
    "a1tu "
    "a1va "
    "a1ve "
    "a1vi "
    "a1vo "
    "a1vu "
    "a1za "
    "a1ze "
    "a1zi "
    "a1zo "
    "a1zu "

    "e1ba "
    "e1be "
    "e1bi "
    "e1bo "
    "e1bu "
    "e1ca "
    "e1ce "
    "e1ci "
    "e1co "
    "e1cu "
    "e1da "
    "e1de "
    "e1di "
    "e1do "
    "e1du "
    "e1fa "
    "e1fe "
    "e1fi "
    "e1fo "
    "e1fu "
    "e1ga "
    "e1ge "
    "e1gi "
    "e1go "
    "e1gu "
    "e1la "
    "e1le "
    "e1li "
    "e1lo "
    "e1lu "
    "e1ma "
    "e1me "
    "e1mi "
    "e1mo "
    "e1mu "
    "e1na "
    "e1ne "
    "e1ni "
    "e1no "
    "e1nu "
    "e1pa "
    "e1pe "
    "e1pi "
    "e1po "
    "e1pu "
    "e1ra "
    "e1re "
    "e1ri "
    "e1ro "
    "e1ru "
    "e1sa "
    "e1se "
    "e1si "
    "e1so "
    "e1su "
    "e1ta "
    "e1te "
    "e1ti "
    "e1to "
    "e1tu "
    "e1va "
    "e1ve "
    "e1vi "
    "e1vo "
    "e1vu "
    "e1za "
    "e1ze "
    "e1zi "
    "e1zo "
    "e1zu "

    "i1ba "
    "i1be "
    "i1bi "
    "i1bo "
    "i1bu "
    "i1ca "
    "i1ce "
    "i1ci "
    "i1co "
    "i1cu "
    "i1da "
    "i1de "
    "i1di "
    "i1do "
    "i1du "
    "i1fa "
    "i1fe "
    "i1fi "
    "i1fo "
    "i1fu "
    "i1ga "
    "i1ge "
    "i1gi "
    "i1go "
    "i1gu "
    "i1la "
    "i1le "
    "i1li "
    "i1lo "
    "i1lu "
    "i1ma "
    "i1me "
    "i1mi "
    "i1mo "
    "i1mu "
    "i1na "
    "i1ne "
    "i1ni "
    "i1no "
    "i1nu "
    "i1pa "
    "i1pe "
    "i1pi "
    "i1po "
    "i1pu "
    "i1ra "
    "i1re "
    "i1ri "
    "i1ro "
    "i1ru "
    "i1sa "
    "i1se "
    "i1si "
    "i1so "
    "i1su "
    "i1ta "
    "i1te "
    "i1ti "
    "i1to "
    "i1tu "
    "i1va "
    "i1ve "
    "i1vi "
    "i1vo "
    "i1vu "
    "i1za "
    "i1ze "
    "i1zi "
    "i1zo "
    "i1zu "

    "o1ba "
    "o1be "
    "o1bi "
    "o1bo "
    "o1bu "
    "o1ca "
    "o1ce "
    "o1ci "
    "o1co "
    "o1cu "
    "o1da "
    "o1de "
    "o1di "
    "o1do "
    "o1du "
    "o1fa "
    "o1fe "
    "o1fi "
    "o1fo "
    "o1fu "
    "o1ga "
    "o1ge "
    "o1gi "
    "o1go "
    "o1gu "
    "o1la "
    "o1le "
    "o1li "
    "o1lo "
    "o1lu "
    "o1ma "
    "o1me "
    "o1mi "
    "o1mo "
    "o1mu "
    "o1na "
    "o1ne "
    "o1ni "
    "o1no "
    "o1nu "
    "o1pa "
    "o1pe "
    "o1pi "
    "o1po "
    "o1pu "
    "o1ra "
    "o1re "
    "o1ri "
    "o1ro "
    "o1ru "
    "o1sa "
    "o1se "
    "o1si "
    "o1so "
    "o1su "
    "o1ta "
    "o1te "
    "o1ti "
    "o1to "
    "o1tu "
    "o1va "
    "o1ve "
    "o1vi "
    "o1vo "
    "o1vu "
    "o1za "
    "o1ze "
    "o1zi "
    "o1zo "
    "o1zu "

    "u1ba "
    "u1be "
    "u1bi "
    "u1bo "
    "u1bu "
    "u1ca "
    "u1ce "
    "u1ci "
    "u1co "
    "u1cu "
    "u1da "
    "u1de "
    "u1di "
    "u1do "
    "u1du "
    "u1fa "
    "u1fe "
    "u1fi "
    "u1fo "
    "u1fu "
    "u1ga "
    "u1ge "
    "u1gi "
    "u1go "
    "u1gu "
    "u1la "
    "u1le "
    "u1li "
    "u1lo "
    "u1lu "
    "u1ma "
    "u1me "
    "u1mi "
    "u1mo "
    "u1mu "
    "u1na "
    "u1ne "
    "u1ni "
    "u1no "
    "u1nu "
    "u1pa "
    "u1pe "
    "u1pi "
    "u1po "
    "u1pu "
    "u1ra "
    "u1re "
    "u1ri "
    "u1ro "
    "u1ru "
    "u1sa "
    "u1se "
    "u1si "
    "u1so "
    "u1su "
    "u1ta "
    "u1te "
    "u1ti "
    "u1to "
    "u1tu "
    "u1va "
    "u1ve "
    "u1vi "
    "u1vo "
    "u1vu "
    "u1za "
    "u1ze "
    "u1zi "
    "u1zo "
    "u1zu "

    // Common word patterns
    "hy3ph "
    "1phen "
    "he2n1at "
    "1tio2n "
    "2tio "
    "1ogy "
    "1graph "
    "1phy "
    "1logy "
    "1nomy "
    "1metry "
    "1scope "
    "1cracy "
    "1mania "
    "1phobia "
    "1cide "
    "1gamy "

    // Exception patterns (higher priority)
    "2ck1 "
    "4ck. "
    "4que. "
    "2x1 "
    "4x. "
    "1qu2 "
    "2gue. "
    "4que "

    // Syllable patterns
    "1ble "
    "1cle "
    "1dle "
    "1fle "
    "1gle "
    "1kle "
    "1ple "
    "1sle "
    "1tle "
    "1zle "

    // Keep vowel combinations together
    "2ai "
    "2au "
    "2ea "
    "2ee "
    "2ei "
    "2eu "
    "2ie "
    "2oa "
    "2oe "
    "2oi "
    "2oo "
    "2ou "
    "2ue "
    "2ui "

    // Common exception words (compound patterns)
    ".child1 "
    ".chil3dren "
    ".ev1ery "
    ".moth1er "
    ".fath1er "
    ".broth1er "
    ".sis1ter "
    ".daugh1ter "
    ".rec1ord "
    ".rec3ord. "
    ".pres1ent "
    ".pres3ent. "
    ".pro1ject "
    ".pro3ject. "
    ".ob1ject "
    ".ob3ject. "
    ".per1fect "
    ".per3fect. "
    ".con1duct "
    ".con3duct. "
    ".con1tent "
    ".con3tent. "
    ".con1tract "
    ".con3tract. "
    ".con1trast "
    ".con3trast. "
    ".prog1ress "
    ".prog3ress. "
    ".com1pound "
    ".in1crease "
    ".in3crease. "
    ".de1crease "
    ".de3crease. "
    ".im1port "
    ".im3port. "
    ".ex1port "
    ".ex3port. "
    ".trans1port "
    ".trans3port. "
    ".re1bel "
    ".re3bel. "
    ".des1ert "
    ".des3ert. "
    ".min1ute "
    ".per1mit "
    ".per3mit. "

    // More comprehensive syllable patterns
    "1ace "
    "1ade "
    "1age "
    "1ake "
    "1ale "
    "1ame "
    "1ane "
    "1ape "
    "1are "
    "1ase "
    "1ate "
    "1ave "
    "1aze "
    "1ece "
    "1ede "
    "1ege "
    "1eke "
    "1ele "
    "1eme "
    "1ene "
    "1epe "
    "1ere "
    "1ese "
    "1ete "
    "1eve "
    "1eze "
    "1ice "
    "1ide "
    "1ife "
    "1ige "
    "1ike "
    "1ile "
    "1ime "
    "1ine "
    "1ipe "
    "1ire "
    "1ise "
    "1ite "
    "1ive "
    "1ize "
    "1obe "
    "1ode "
    "1oge "
    "1oke "
    "1ole "
    "1ome "
    "1one "
    "1ope "
    "1ore "
    "1ose "
    "1ote "
    "1ove "
    "1oze "
    "1ube "
    "1ude "
    "1uge "
    "1uke "
    "1ule "
    "1ume "
    "1une "
    "1upe "
    "1ure "
    "1use "
    "1ute "
    "1uze "
    ;

void HyphenEngine::load_us_english() {
    load_patterns(US_ENGLISH_PATTERNS);
    log_info("tex_hyphen: loaded US English patterns (%zu total)", pattern_count_);
}

// ============================================================================
// Global hyphenation engine
// ============================================================================

static HyphenEngine* g_us_english_hyphenator = nullptr;

HyphenEngine* get_us_english_hyphenator(Arena* arena) {
    if (!g_us_english_hyphenator) {
        g_us_english_hyphenator = (HyphenEngine*)arena_alloc(arena, sizeof(HyphenEngine));
        new (g_us_english_hyphenator) HyphenEngine(arena);
        g_us_english_hyphenator->load_us_english();
    }
    return g_us_english_hyphenator;
}

// ============================================================================
// Discretionary hyphen insertion
// ============================================================================

// Helper: check if character is a letter
static bool is_letter(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

// Helper: extract word boundaries from HList
struct WordSpan {
    TexNode* first;     // First char node of word
    TexNode* last;      // Last char node of word
    char word[MAX_HYPHEN_WORD];
    size_t len;
};

TexNode* insert_discretionary_hyphens(
    TexNode* hlist,
    const HyphenEngine* engine,
    const FontSpec& font,
    Arena* arena
) {
    if (!hlist || !engine) return hlist;

    // Find words in the hlist and insert discretionary nodes
    TexNode* node = hlist->first_child;

    while (node) {
        // Look for start of a word (letter character)
        if (node->node_class != NodeClass::Char ||
            !is_letter((char)node->content.ch.codepoint)) {
            node = node->next_sibling;
            continue;
        }

        // Found start of word - collect all letters
        WordSpan span;
        span.first = node;
        span.len = 0;

        TexNode* word_node = node;
        while (word_node &&
               word_node->node_class == NodeClass::Char &&
               is_letter((char)word_node->content.ch.codepoint) &&
               span.len < MAX_HYPHEN_WORD - 1) {
            span.word[span.len++] = (char)word_node->content.ch.codepoint;
            span.last = word_node;
            word_node = word_node->next_sibling;
        }
        span.word[span.len] = '\0';

        // Hyphenate the word
        if (span.len >= LEFT_HYPHEN_MIN + RIGHT_HYPHEN_MIN) {
            HyphenResult result = engine->hyphenate_word(span.word, span.len);

            // Insert discretionary nodes at hyphenation points
            size_t positions[MAX_HYPHEN_WORD];
            size_t num_hyphens = result.get_hyphen_positions(positions, MAX_HYPHEN_WORD);

            if (num_hyphens > 0) {
                // Walk through word again, inserting disc nodes
                TexNode* char_node = span.first;
                size_t char_idx = 0;
                size_t hyphen_idx = 0;

                while (char_node && char_idx < span.len && hyphen_idx < num_hyphens) {
                    if (char_idx == positions[hyphen_idx]) {
                        // Create hyphen character node for pre-break
                        TexNode* hyphen_char = make_char(arena, '-', font);

                        // Create discretionary hyphen after this character
                        // pre_break = hyphen, post_break = nullptr, no_break = nullptr
                        TexNode* disc = make_disc(arena, hyphen_char, nullptr, nullptr);

                        // Insert after current char
                        TexNode* next = char_node->next_sibling;
                        char_node->next_sibling = disc;
                        disc->prev_sibling = char_node;
                        disc->next_sibling = next;
                        if (next) next->prev_sibling = disc;
                        disc->parent = hlist;

                        hyphen_idx++;
                    }

                    char_node = char_node->next_sibling;
                    // Skip any disc nodes we just inserted
                    while (char_node && char_node->node_class == NodeClass::Disc) {
                        char_node = char_node->next_sibling;
                    }
                    char_idx++;
                }
            }
        }

        // Move to next potential word
        node = word_node;
    }

    return hlist;
}

}  // namespace tex
