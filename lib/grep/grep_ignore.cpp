// grep_ignore.cpp — gitignore-style globs and ignore files (GRP14, GRP22).
//
// Patterns follow gitignore: a pattern without a '/' (other than a trailing
// one) matches an entry's name at any depth; one with a '/' is anchored to the
// directory of its ignore file (or to the walk root for caller globs). A
// trailing '/' matches directories only, a leading '!' re-includes, and the
// last matching pattern wins, deeper ignore files before shallower ones.

#include "grep_walk.hpp"
#include "../file.h"
#include "../log.h"
#include "../memtrack.h"

#include <string.h>

#define GREP_GLOB_MAX_DEPTH 64
#define GREP_IGNORE_PATH_MAX 4096

// one bracket expression at p (just past '['); on success *end is past ']'
static bool glob_class(const char* p, const char* pe, unsigned char c, bool* matched, const char** end) {
    bool negate = false;
    if (p < pe && (*p == '!' || *p == '^')) {
        negate = true;
        p++;
    }
    bool hit = false;
    bool first = true;
    while (p < pe && (*p != ']' || first)) {
        first = false;
        unsigned char lo = (unsigned char)*p;
        if (lo == '\\' && p + 1 < pe) lo = (unsigned char)*++p;
        p++;
        unsigned char hi = lo;
        if (p + 1 < pe && *p == '-' && p[1] != ']') {
            hi = (unsigned char)p[1];
            if (hi == '\\' && p + 2 < pe) {
                hi = (unsigned char)p[2];
                p++;
            }
            p += 2;
        }
        if (c >= lo && c <= hi) hit = true;
    }
    if (p >= pe) return false;  // no closing ']': not a class
    *matched = hit != negate;
    *end = p + 1;
    return true;
}

static bool glob_rec(const char* ps, const char* p, const char* pe, const char* t, const char* te, int depth) {
    if (depth > GREP_GLOB_MAX_DEPTH) return false;
    while (p < pe) {
        char c = *p;
        if (c == '*') {
            if (p + 1 < pe && p[1] == '*') {
                const char* q = p + 2;
                bool at_start = (p == ps || p[-1] == '/');
                bool at_end = (q == pe || *q == '/');
                if (at_start && at_end) {
                    if (q == pe) return true;  // trailing "**": everything below
                    q++;                       // "**/": zero or more directories
                    if (glob_rec(ps, q, pe, t, te, depth + 1)) return true;
                    for (const char* s = t; s < te; s++) {
                        if (*s == '/' && glob_rec(ps, q, pe, s + 1, te, depth + 1)) return true;
                    }
                    return false;
                }
                while (p + 1 < pe && p[1] == '*') p++;  // "**" inside a name acts as '*'
            }
            p++;
            if (p == pe) return memchr(t, '/', (size_t)(te - t)) == NULL;
            for (const char* s = t;; s++) {
                if (glob_rec(ps, p, pe, s, te, depth + 1)) return true;
                if (s == te || *s == '/') return false;
            }
        }
        if (t == te) return false;
        if (c == '?') {
            if (*t == '/') return false;
            p++;
            t++;
            continue;
        }
        if (c == '[') {
            bool matched = false;
            const char* after = NULL;
            if (*t != '/' && glob_class(p + 1, pe, (unsigned char)*t, &matched, &after)) {
                if (!matched) return false;
                p = after;
                t++;
                continue;
            }
            if (*t == '/') return false;
            // an unterminated '[' is literal
        }
        if (c == '\\' && p + 1 < pe) c = *++p;
        if (*t != c) return false;
        p++;
        t++;
    }
    return t == te;
}

bool grep_glob_match(const char* pattern, size_t pattern_len, const char* text, size_t text_len) {
    if (!pattern || !text) return false;
    return glob_rec(pattern, pattern, pattern + pattern_len, text, text + text_len, 0);
}

bool grep_rule_parse(const char* line, size_t len, bool allow_negate, GrepIgnoreRule* out) {
    memset(out, 0, sizeof(*out));
    while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n')) len--;
    // trailing spaces are dropped unless escaped
    while (len > 0 && line[len - 1] == ' ' && !(len >= 2 && line[len - 2] == '\\')) len--;
    if (len == 0 || line[0] == '#') return false;
    if (allow_negate && line[0] == '!') {
        out->negate = true;
        line++;
        len--;
    } else if (line[0] == '\\' && len > 1 && (line[1] == '#' || line[1] == '!')) {
        line++;
        len--;
    }
    if (len > 0 && line[len - 1] == '/') {
        out->dir_only = true;
        len--;
    }
    if (len == 0) return false;
    out->anchored = memchr(line, '/', len) != NULL;
    if (line[0] == '/') {
        line++;
        len--;
    }
    if (len == 0) return false;
    out->pat = line;
    out->len = len;
    return true;
}

static const char* base_name(const char* rel) {
    const char* slash = strrchr(rel, '/');
    return slash ? slash + 1 : rel;
}

bool grep_rule_match(const GrepIgnoreRule* rule, const char* rel, bool is_dir) {
    if (rule->dir_only && !is_dir) return false;
    const char* text = rule->anchored ? rel : base_name(rel);
    return grep_glob_match(rule->pat, rule->len, text, strlen(text));
}

static bool append_file(const char* dir, const char* name, char** data, size_t* size) {
    char* path = file_path_join(dir, name);
    if (!path) return false;
    bool ok = file_exists(path) && file_read_all(path, MEM_CAT_TEMP, data, size);
    mem_free(path);
    return ok;
}

GrepIgnoreNode* grep_ignore_load(const char* dir_full, const char* dir_rel, const char* up_prefix,
                                 GrepIgnoreNode* parent) {
    char* text[2] = {NULL, NULL};
    size_t size[2] = {0, 0};
    // .ignore after .gitignore, so with last-match-wins it takes precedence
    append_file(dir_full, ".gitignore", &text[0], &size[0]);
    append_file(dir_full, ".ignore", &text[1], &size[1]);
    if (!text[0] && !text[1]) return NULL;

    size_t rel_len = dir_rel ? strlen(dir_rel) : 0;
    size_t up_len = up_prefix ? strlen(up_prefix) : 0;
    size_t total = size[0] + size[1] + rel_len + up_len + 4;
    int lines = 1;
    for (int k = 0; k < 2; k++) {
        for (size_t i = 0; i < size[k]; i++) lines += text[k][i] == '\n';
    }
    GrepIgnoreNode* node = (GrepIgnoreNode*)mem_calloc(1, sizeof(GrepIgnoreNode), MEM_CAT_TEMP);
    char* storage = (char*)mem_alloc(total, MEM_CAT_TEMP);
    GrepIgnoreRule* rules = (GrepIgnoreRule*)mem_calloc((size_t)lines + 1, sizeof(GrepIgnoreRule), MEM_CAT_TEMP);
    if (!node || !storage || !rules) {
        if (node) mem_free(node);
        if (storage) mem_free(storage);
        if (rules) mem_free(rules);
        for (int k = 0; k < 2; k++) if (text[k]) mem_free(text[k]);
        return NULL;
    }
    size_t at = 0;
    char* strings = storage;
    if (dir_rel) {
        memcpy(strings + at, dir_rel, rel_len + 1);
        node->dir_rel = strings + at;
        at += rel_len + 1;
    }
    if (up_prefix) {
        memcpy(strings + at, up_prefix, up_len + 1);
        node->up_prefix = strings + at;
        at += up_len + 1;
    }
    for (int k = 0; k < 2; k++) {
        if (!text[k]) continue;
        char* body = storage + at;
        memcpy(body, text[k], size[k]);
        at += size[k];
        size_t i = 0;
        while (i < size[k]) {
            size_t j = i;
            while (j < size[k] && body[j] != '\n') j++;
            GrepIgnoreRule rule;
            if (grep_rule_parse(body + i, j - i, true, &rule)) rules[node->count++] = rule;
            i = j + 1;
        }
        mem_free(text[k]);
    }
    node->parent = parent;
    node->rules = rules;
    node->storage = storage;
    return node;
}

void grep_ignore_free(GrepIgnoreNode* node) {
    if (!node) return;
    mem_free(node->rules);
    mem_free(node->storage);
    mem_free(node);
}

int grep_ignore_decide(const GrepIgnoreNode* node, const char* rel, bool is_dir) {
    char joined[GREP_IGNORE_PATH_MAX];
    for (const GrepIgnoreNode* n = node; n; n = n->parent) {
        if (n->count == 0) continue;
        const char* sub = rel;
        if (n->up_prefix) {
            // an ignore file above the walk root: paths are relative to it
            size_t up_len = strlen(n->up_prefix), rel_len = strlen(rel);
            if (up_len + 1 + rel_len + 1 > sizeof(joined)) continue;
            memcpy(joined, n->up_prefix, up_len);
            joined[up_len] = '/';
            memcpy(joined + up_len + 1, rel, rel_len + 1);
            sub = joined;
        } else if (n->dir_rel && n->dir_rel[0]) {
            size_t dl = strlen(n->dir_rel);
            if (strncmp(rel, n->dir_rel, dl) != 0 || rel[dl] != '/') continue;
            sub = rel + dl + 1;
        }
        for (int i = n->count - 1; i >= 0; i--) {
            if (grep_rule_match(&n->rules[i], sub, is_dir)) return n->rules[i].negate ? 0 : 1;
        }
    }
    return -1;
}
