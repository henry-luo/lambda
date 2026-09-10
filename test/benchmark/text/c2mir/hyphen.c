/* Native C2MIR port of text/hyphen.js. */
extern int printf(const char *, ...);

#define HYPHEN_MAX_WORD 160
#define HYPHEN_MAX_TEXT 4096
#define HYPHEN_CACHE_CAPACITY 256

#include "hyphen_patterns_data.h"

typedef struct WordCacheEntry {
    char word[HYPHEN_MAX_WORD];
    char result[HYPHEN_MAX_WORD * 2];
} WordCacheEntry;

typedef struct WordCache {
    WordCacheEntry entries[HYPHEN_CACHE_CAPACITY];
    int count;
} WordCache;

typedef struct MarkerCacheEntry {
    char word[HYPHEN_MAX_WORD];
    int markers[HYPHEN_MAX_WORD];
    int count;
} MarkerCacheEntry;

typedef struct MarkerCache {
    MarkerCacheEntry entries[HYPHEN_CACHE_CAPACITY];
    int count;
} MarkerCache;

typedef struct HyphenCase {
    const char *input;
    const char *expected;
} HyphenCase;

static const HyphenCase hyphen_cases[] = {
    {"A certain king had a beautiful garden, and every morning he walked through it to admire the flowers.",
     "A cer-tain king had a beau-ti-ful gar-den, and every morn-ing he walked through it to ad-mire the flow-ers."},
    {"The tortoise never stopped for a moment, walking slowly but steadily right to the end of the course.",
     "The tor-toise nev-er stopped for a mo-ment, walk-ing slow-ly but steadi-ly right to the end of the course."},
    {"A compiler transforms structured source text into executable instructions while preserving useful diagnostics.",
     "A com-pil-er trans-forms struc-tured source text into ex-e-cutable in-struc-tions while pre-serv-ing use-ful di-ag-nos-tics."},
    {"Text processing includes punctuation, capitalization, multiline paragraphs, and carefully selected exceptions.",
     "Text pro-cess-ing in-cludes punc-tu-a-tion, cap-i-tal-iza-tion, mul-ti-line para-graphs, and care-ful-ly se-lect-ed ex-cep-tions."},
    {"<article><h1>Hyphenation benchmark</h1><p>Beautiful documents require readable typography and consistent line breaking.</p></article>",
     "<article><h1>Hy-phen-ation bench-mark</h1><p>Beau-ti-ful doc-u-ments re-quire read-able ty-pog-ra-phy and con-sis-tent line break-ing.</p></article>"},
    {"The algorithm combines a pattern trie with exception handling and a configurable hyphenation character.",
     "The al-go-rithm com-bines a pat-tern trie with ex-cep-tion han-dling and a con-fig-urable hy-phen-ation char-ac-ter."},
    {"associate associates declination obligatory philanthropic",
     "as-soc-iate as-soc-iates dec-lin-ati-on oblig-at-ory phil-ant-hropic"},
    {"recognizance reformation retribution reciprocity table present projects",
     "re-cogn-iza-nce ref-orm-ati-on ret-rib-uti-on reci-procity ta-ble present projects"},
    {"<article data-note=\"associate\">Hyphenation &amp; typography</article>",
     "<article data-note=\"associate\">Hy-phen-ation &amp; ty-pog-ra-phy</article>"},
    {"co-operate already-hyphenated exceptionally punctuation's boundary",
     "co-operate already-hyphenated ex-cep-tion-al-ly punc-tu-a-tion's bound-ary"},
    {"supercalifragilisticexpialidocious internationalization configuration",
     "su-per-cal-ifrag-ilis-tic-ex-pi-ali-do-cious in-ter-na-tion-al-iza-tion con-fig-u-ra-tion"},
    {"Configuration configuration configuration.",
     "Con-fig-u-ra-tion con-fig-u-ra-tion con-fig-u-ra-tion."},
    {"<3 is not markup, while <em>configuration</em> remains a word.",
     "<3 is not markup, while <em>con-fig-u-ra-tion</em> re-mains a word."}
};

static int text_length(const char *text) {
    int length = 0;
    while (text[length] != 0) length++;
    return length;
}

static int text_equal(const char *left, const char *right) {
    int index = 0;
    while (left[index] != 0 && right[index] != 0) {
        if (left[index] != right[index]) return 0;
        index++;
    }
    return left[index] == right[index];
}

static void copy_text(char *destination, const char *source) {
    int index = 0;
    while (source[index] != 0) {
        destination[index] = source[index];
        index++;
    }
    destination[index] = 0;
}

static void append_char(char *destination, int *length, char value) {
    destination[*length] = value;
    *length = *length + 1;
    destination[*length] = 0;
}

static void append_text(char *destination, int *length, const char *source) {
    int index = 0;
    while (source[index] != 0) {
        append_char(destination, length, source[index]);
        index++;
    }
}

static int is_ascii_letter(char value) {
    return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z');
}

static int is_word_char(char value) {
    return is_ascii_letter(value) || value == '\'';
}

static char lower_ascii(char value) {
    if (value >= 'A' && value <= 'Z') return value + ('a' - 'A');
    return value;
}

static int contains_hyphen(const char *word) {
    int index = 0;
    while (word[index] != 0) {
        if (word[index] == '-') return 1;
        index++;
    }
    return 0;
}

static int trie_child(int node_index, int code) {
    int index;
    int first = hyphen_node_first[node_index];
    int count = hyphen_node_count[node_index];
    for (index = 0; index < count; index++) {
        int edge_index = first + index;
        if (hyphen_edge_code[edge_index] == code) return hyphen_edge_child[edge_index];
    }
    return -1;
}

static int exception_markers(const char *word, int *markers) {
    int exception_index;
    for (exception_index = 0; exception_index < HYPHEN_EXCEPTION_COUNT; exception_index++) {
        if (text_equal(word, hyphen_exception_words[exception_index])) {
            int marker_index;
            int count = hyphen_exception_counts[exception_index];
            for (marker_index = 0; marker_index < count; marker_index++) {
                markers[marker_index] = hyphen_exception_markers[exception_index][marker_index];
            }
            return count;
        }
    }
    return -1;
}

static MarkerCacheEntry *find_marker_cache(MarkerCache *cache, const char *word) {
    int index;
    for (index = 0; index < cache->count; index++) {
        if (text_equal(cache->entries[index].word, word)) return &cache->entries[index];
    }
    return 0;
}

static int markers_for_word(const char *word, MarkerCache *cache, int *markers) {
    char lowered[HYPHEN_MAX_WORD];
    int levels[HYPHEN_MAX_WORD + 1];
    int word_length = text_length(word);
    int index;
    int exception_count;
    MarkerCacheEntry *cached;
    for (index = 0; index < word_length; index++) lowered[index] = lower_ascii(word[index]);
    lowered[word_length] = 0;
    exception_count = exception_markers(lowered, markers);
    if (exception_count >= 0) return exception_count;
    cached = find_marker_cache(cache, lowered);
    if (cached) {
        for (index = 0; index < cached->count; index++) markers[index] = cached->markers[index];
        return cached->count;
    }
    for (index = 0; index <= word_length; index++) levels[index] = 0;
    for (index = 0; index + 2 < word_length + 2; index++) {
        int node_index = HYPHEN_TRIE_ROOT;
        int position = index == 0 ? 0 : index - 1;
        int cursor;
        for (cursor = index; cursor < word_length + 2; cursor++) {
            int code;
            int child;
            int level_index;
            if (cursor == 0 || cursor == word_length + 1) code = '.';
            else code = lowered[cursor - 1];
            child = trie_child(node_index, code);
            if (child < 0) break;
            node_index = child;
            level_index = hyphen_node_level[node_index];
            if (level_index >= 0) {
                int level_offset;
                int level_length = hyphen_level_lengths[level_index];
                int level_start = hyphen_level_offsets[level_index];
                for (level_offset = 0; level_offset < level_length; level_offset++) {
                    int target = position + level_offset;
                    int value = hyphen_level_values[level_start + level_offset];
                    if (target >= 0 && target <= word_length && value > levels[target]) {
                        levels[target] = value;
                    }
                }
            }
        }
    }
    levels[0] = 0;
    levels[1] = 0;
    levels[word_length] = 0;
    levels[word_length - 1] = 0;
    exception_count = 0;
    for (index = 0; index <= word_length; index++) {
        if ((levels[index] & 1) == 1) {
            markers[exception_count] = index;
            exception_count++;
        }
    }
    if (cache->count < HYPHEN_CACHE_CAPACITY) {
        MarkerCacheEntry *entry = &cache->entries[cache->count];
        copy_text(entry->word, lowered);
        entry->count = exception_count;
        for (index = 0; index < exception_count; index++) entry->markers[index] = markers[index];
        cache->count++;
    }
    return exception_count;
}

static WordCacheEntry *find_word_cache(WordCache *cache, const char *word) {
    int index;
    for (index = 0; index < cache->count; index++) {
        if (text_equal(cache->entries[index].word, word)) return &cache->entries[index];
    }
    return 0;
}

static void hyphenate_word(const char *word, WordCache *result_cache, MarkerCache *marker_cache,
                           char *result) {
    WordCacheEntry *cached = find_word_cache(result_cache, word);
    int word_length;
    int markers[HYPHEN_MAX_WORD];
    int marker_count;
    int marker_index = 0;
    int index;
    int result_length = 0;
    if (cached) {
        copy_text(result, cached->result);
        return;
    }
    word_length = text_length(word);
    if (word_length < 5 || contains_hyphen(word)) {
        copy_text(result, word);
    } else {
        marker_count = markers_for_word(word, marker_cache, markers);
        for (index = 0; index < word_length; index++) {
            if (marker_index < marker_count && markers[marker_index] == index) {
                append_char(result, &result_length, '-');
                marker_index++;
            }
            append_char(result, &result_length, word[index]);
        }
        while (marker_index < marker_count) {
            append_char(result, &result_length, '-');
            marker_index++;
        }
    }
    if (result_cache->count < HYPHEN_CACHE_CAPACITY) {
        WordCacheEntry *entry = &result_cache->entries[result_cache->count];
        copy_text(entry->word, word);
        copy_text(entry->result, result);
        result_cache->count++;
    }
}

static int starts_html_tag(const char *text, int index) {
    return text[index] == '<' && text[index + 1] != 0 &&
        (is_ascii_letter(text[index + 1]) || text[index + 1] == '/');
}

static void hyphenate_text(const char *text, WordCache *result_cache, MarkerCache *marker_cache,
                           char *result) {
    int index = 0;
    int result_length = 0;
    result[0] = 0;
    while (text[index] != 0) {
        if (starts_html_tag(text, index)) {
            while (text[index] != 0) {
                char value = text[index];
                append_char(result, &result_length, value);
                index++;
                if (value == '>') break;
            }
        } else if (is_word_char(text[index])) {
            char word[HYPHEN_MAX_WORD];
            char hyphenated[HYPHEN_MAX_WORD * 2];
            int word_length = 0;
            while (text[index] != 0) {
                char value = text[index];
                if (is_word_char(value)) {
                    word[word_length] = value;
                    word_length++;
                    index++;
                } else if (value == '-' && word_length > 0 && text[index + 1] != 0 &&
                           is_ascii_letter(text[index + 1])) {
                    word[word_length] = value;
                    word_length++;
                    index++;
                } else {
                    break;
                }
            }
            word[word_length] = 0;
            hyphenate_word(word, result_cache, marker_cache, hyphenated);
            append_text(result, &result_length, hyphenated);
        } else {
            append_char(result, &result_length, text[index]);
            index++;
        }
    }
}

static int verify_hyphen_cases(void) {
    WordCache result_cache = {{0}, 0};
    MarkerCache marker_cache = {{0}, 0};
    int index;
    for (index = 0; index < (int)(sizeof(hyphen_cases) / sizeof(hyphen_cases[0])); index++) {
        char result[HYPHEN_MAX_TEXT];
        hyphenate_text(hyphen_cases[index].input, &result_cache, &marker_cache, result);
        if (!text_equal(result, hyphen_cases[index].expected)) return 0;
    }
    return 1;
}

int main(void) {
    int checksum = 0;
    int round;
    int index;
    if (!verify_hyphen_cases()) {
        printf("hyphen: FAIL fixture verification\n");
        return 1;
    }
    for (round = 0; round < 32; round++) {
        WordCache result_cache = {{0}, 0};
        MarkerCache marker_cache = {{0}, 0};
        for (index = 0; index < (int)(sizeof(hyphen_cases) / sizeof(hyphen_cases[0])); index++) {
            char result[HYPHEN_MAX_TEXT];
            int result_length;
            hyphenate_text(hyphen_cases[index].input, &result_cache, &marker_cache, result);
            result_length = text_length(result);
            checksum = (checksum + result_length * 29) % 1000000007;
            if (result_length > 0) checksum =
                (checksum + (unsigned char)result[index % result_length]) % 1000000007;
        }
    }
    if (checksum != 1183296) {
        printf("hyphen: FAIL checksum=%d\n", checksum);
        return 1;
    }
    printf("hyphen: CHECKSUM:%d\n", checksum);
    return 0;
}
