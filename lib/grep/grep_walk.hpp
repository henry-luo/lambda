// grep_walk.hpp — ignore rules and globs for the directory walk (GRP14, GRP22).
// Internal to lib/grep.
#pragma once

#include "grep_internal.hpp"

struct GrepIgnoreRule {
    const char* pat;     // glob text, borrowed from its owner
    size_t len;
    bool negate;         // "!pattern" re-includes
    bool dir_only;       // "pattern/" matches directories only
    bool anchored;       // contains '/': relative to the rule's directory, not any depth
};

// The rules of one directory's .gitignore and .ignore, chained to the
// directory above. A node above the walk root (from the enclosing repository)
// carries `up_prefix`, the path from it down to the root.
struct GrepIgnoreNode {
    GrepIgnoreNode* parent;
    const char* dir_rel;     // this directory relative to the walk root ("" for the root)
    const char* up_prefix;
    GrepIgnoreRule* rules;
    int count;
    char* storage;           // owns the strings and the rule text
};

// Parses one gitignore line; false for blanks and comments.
bool grep_rule_parse(const char* line, size_t len, bool allow_negate, GrepIgnoreRule* out);
// `rel` is relative to the rule's directory, with '/' separators.
bool grep_rule_match(const GrepIgnoreRule* rule, const char* rel, bool is_dir);

// NULL when the directory has no ignore file (or on allocation failure).
GrepIgnoreNode* grep_ignore_load(const char* dir_full, const char* dir_rel, const char* up_prefix,
                                 GrepIgnoreNode* parent);
void grep_ignore_free(GrepIgnoreNode* node);
// -1 when no rule decides, 0 when a negation re-includes, 1 when ignored.
int grep_ignore_decide(const GrepIgnoreNode* node, const char* rel, bool is_dir);

// Opens `open_path` but reports `label` (a symlinked root is opened resolved).
// `cap` (0 = none) ends the input after that many selected records or lines,
// on top of max_matches_per_file.
GrepStatus grep_search_file_as(GrepSearcher* searcher, const char* open_path, const char* label,
                               const GrepSink* sink, uint64_t cap);
