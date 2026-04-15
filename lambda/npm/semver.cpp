// semver.cpp — Semantic versioning parser and range matcher (npm-compatible)

#include "semver.h"
#include "../lib/log.h"
#include "../lib/memtrack.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static const char* skip_ws(const char* s) {
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

// parse a non-negative integer, advance *p past it. Returns -1 on failure.
static int parse_int(const char** p) {
    const char* s = *p;
    if (!isdigit((unsigned char)*s)) return -1;
    int val = 0;
    while (isdigit((unsigned char)*s)) {
        val = val * 10 + (*s - '0');
        s++;
    }
    *p = s;
    return val;
}

// parse a prerelease or build segment (dot-separated identifiers)
static void parse_tag(const char** p, char* out, int out_size) {
    const char* s = *p;
    int i = 0;
    while (*s && *s != '+' && *s != ' ' && *s != '\t' && *s != '|' && i < out_size - 1) {
        out[i++] = *s++;
    }
    out[i] = '\0';
    *p = s;
}

// compare prerelease strings per semver spec:
// - no prerelease > has prerelease
// - numeric identifiers compared as integers
// - string identifiers compared lexically
// - shorter wins if all identifiers match
static int compare_prerelease(const char* a, const char* b) {
    bool a_empty = (a[0] == '\0');
    bool b_empty = (b[0] == '\0');
    if (a_empty && b_empty) return 0;
    if (a_empty) return 1;   // no pre > has pre
    if (b_empty) return -1;

    const char* pa = a;
    const char* pb = b;

    while (*pa && *pb) {
        // extract identifier up to '.' or end
        char ida[64], idb[64];
        int ia = 0, ib = 0;
        while (*pa && *pa != '.') { if (ia < 63) ida[ia++] = *pa; pa++; }
        ida[ia] = '\0';
        while (*pb && *pb != '.') { if (ib < 63) idb[ib++] = *pb; pb++; }
        idb[ib] = '\0';

        // check if both are numeric
        bool a_num = true, b_num = true;
        for (int i = 0; ida[i]; i++) if (!isdigit((unsigned char)ida[i])) { a_num = false; break; }
        for (int i = 0; idb[i]; i++) if (!isdigit((unsigned char)idb[i])) { b_num = false; break; }

        if (a_num && b_num) {
            int na = atoi(ida), nb = atoi(idb);
            if (na != nb) return na < nb ? -1 : 1;
        } else if (a_num) {
            return -1; // numeric < string
        } else if (b_num) {
            return 1;
        } else {
            int cmp = strcmp(ida, idb);
            if (cmp != 0) return cmp;
        }

        if (*pa == '.') pa++;
        if (*pb == '.') pb++;
    }

    if (*pa) return 1;  // a has more identifiers
    if (*pb) return -1;
    return 0;
}

// ---------------------------------------------------------------------------
// SemVer parse / compare / format
// ---------------------------------------------------------------------------

SemVer semver_parse(const char* version_str) {
    SemVer v = {};
    if (!version_str) return v;

    const char* s = version_str;
    // skip leading 'v' or '='
    if (*s == 'v' || *s == 'V') s++;
    if (*s == '=') s++;
    s = skip_ws(s);

    v.major = parse_int(&s);
    if (v.major < 0) return v;

    if (*s != '.') { v.minor = 0; v.patch = 0; v.valid = true; return v; }
    s++;
    v.minor = parse_int(&s);
    if (v.minor < 0) return v;

    if (*s != '.') { v.patch = 0; v.valid = true; return v; }
    s++;
    v.patch = parse_int(&s);
    if (v.patch < 0) return v;

    // prerelease
    if (*s == '-') {
        s++;
        parse_tag(&s, v.prerelease, sizeof(v.prerelease));
    }
    // build metadata
    if (*s == '+') {
        s++;
        parse_tag(&s, v.build, sizeof(v.build));
    }

    v.valid = true;
    return v;
}

int semver_compare(const SemVer* a, const SemVer* b) {
    if (a->major != b->major) return a->major < b->major ? -1 : 1;
    if (a->minor != b->minor) return a->minor < b->minor ? -1 : 1;
    if (a->patch != b->patch) return a->patch < b->patch ? -1 : 1;
    return compare_prerelease(a->prerelease, b->prerelease);
}

void semver_format(const SemVer* v, char* buf, int buf_size) {
    if (v->prerelease[0] && v->build[0]) {
        snprintf(buf, buf_size, "%d.%d.%d-%s+%s", v->major, v->minor, v->patch, v->prerelease, v->build);
    } else if (v->prerelease[0]) {
        snprintf(buf, buf_size, "%d.%d.%d-%s", v->major, v->minor, v->patch, v->prerelease);
    } else if (v->build[0]) {
        snprintf(buf, buf_size, "%d.%d.%d+%s", v->major, v->minor, v->patch, v->build);
    } else {
        snprintf(buf, buf_size, "%d.%d.%d", v->major, v->minor, v->patch);
    }
}

// ---------------------------------------------------------------------------
// Range parsing
// ---------------------------------------------------------------------------

// parse a single comparator like ">=1.2.3" or "1.2.3"
static bool parse_comparator(const char** p, SemVerComparator* cmp) {
    const char* s = skip_ws(*p);

    memset(cmp, 0, sizeof(*cmp));

    // parse operator
    cmp->op = CMP_EQ;
    if (s[0] == '>' && s[1] == '=') { cmp->op = CMP_GE; s += 2; }
    else if (s[0] == '<' && s[1] == '=') { cmp->op = CMP_LE; s += 2; }
    else if (s[0] == '>') { cmp->op = CMP_GT; s += 1; }
    else if (s[0] == '<') { cmp->op = CMP_LT; s += 1; }
    else if (s[0] == '=') { cmp->op = CMP_EQ; s += 1; }

    s = skip_ws(s);

    // skip leading v
    if (*s == 'v' || *s == 'V') s++;

    // parse version (may have x/X/* wildcards)
    int major = -1, minor = -1, patch = -1;

    if (*s == '*' || *s == 'x' || *s == 'X') {
        major = -1; s++;
    } else {
        major = parse_int(&s);
    }

    if (*s == '.') {
        s++;
        if (*s == '*' || *s == 'x' || *s == 'X') {
            minor = -1; s++;
        } else {
            minor = parse_int(&s);
        }
    }

    if (*s == '.') {
        s++;
        if (*s == '*' || *s == 'x' || *s == 'X') {
            patch = -1; s++;
        } else {
            patch = parse_int(&s);
        }
    }

    // prerelease
    char prerelease[64] = "";
    if (*s == '-') {
        s++;
        parse_tag(&s, prerelease, sizeof(prerelease));
    }
    // skip build
    if (*s == '+') {
        s++;
        char build[64];
        parse_tag(&s, build, sizeof(build));
    }

    cmp->version.major = major >= 0 ? major : 0;
    cmp->version.minor = minor >= 0 ? minor : 0;
    cmp->version.patch = patch >= 0 ? patch : 0;
    if (prerelease[0]) {
        strncpy(cmp->version.prerelease, prerelease, sizeof(cmp->version.prerelease) - 1);
    }
    cmp->version.valid = true;

    // handle wildcards: convert to range comparators
    // This is used when no explicit operator was given
    // Wildcards with operators are treated as-is
    // (the caller handles expansion for tilde/caret/x-range)

    *p = s;
    return true;
}

// expand tilde range: ~1.2.3 → >=1.2.3 <1.3.0
static void expand_tilde(const char** p, SemVerComparatorSet* set) {
    const char* s = *p;
    s++; // skip ~
    s = skip_ws(s);
    if (*s == 'v' || *s == 'V') s++;

    int major = parse_int(&s);
    int minor = -1, patch = -1;
    if (*s == '.') { s++; minor = parse_int(&s); }
    if (*s == '.') { s++; patch = parse_int(&s); }

    char pre[64] = "";
    if (*s == '-') { s++; parse_tag(&s, pre, sizeof(pre)); }
    if (*s == '+') { s++; char b[64]; parse_tag(&s, b, sizeof(b)); }

    if (minor < 0) {
        // ~1 → >=1.0.0 <2.0.0
        SemVerComparator* c1 = &set->comparators[set->count++];
        c1->op = CMP_GE;
        c1->version = (SemVer){major, 0, 0, "", "", true};
        SemVerComparator* c2 = &set->comparators[set->count++];
        c2->op = CMP_LT;
        c2->version = (SemVer){major + 1, 0, 0, "", "", true};
    } else {
        // ~1.2.3 → >=1.2.3 <1.3.0
        SemVerComparator* c1 = &set->comparators[set->count++];
        c1->op = CMP_GE;
        c1->version = (SemVer){major, minor, patch >= 0 ? patch : 0, "", "", true};
        if (pre[0]) strncpy(c1->version.prerelease, pre, sizeof(c1->version.prerelease) - 1);
        SemVerComparator* c2 = &set->comparators[set->count++];
        c2->op = CMP_LT;
        c2->version = (SemVer){major, minor + 1, 0, "", "", true};
    }

    *p = s;
}

// expand caret range: ^1.2.3 → >=1.2.3 <2.0.0
static void expand_caret(const char** p, SemVerComparatorSet* set) {
    const char* s = *p;
    s++; // skip ^
    s = skip_ws(s);
    if (*s == 'v' || *s == 'V') s++;

    int major = parse_int(&s);
    int minor = -1, patch = -1;
    if (*s == '.') { s++; minor = parse_int(&s); }
    if (*s == '.') { s++; patch = parse_int(&s); }

    char pre[64] = "";
    if (*s == '-') { s++; parse_tag(&s, pre, sizeof(pre)); }
    if (*s == '+') { s++; char b[64]; parse_tag(&s, b, sizeof(b)); }

    SemVerComparator* c1 = &set->comparators[set->count++];
    c1->op = CMP_GE;
    c1->version = (SemVer){major, minor >= 0 ? minor : 0, patch >= 0 ? patch : 0, "", "", true};
    if (pre[0]) strncpy(c1->version.prerelease, pre, sizeof(c1->version.prerelease) - 1);

    SemVerComparator* c2 = &set->comparators[set->count++];
    c2->op = CMP_LT;

    if (major != 0) {
        // ^1.2.3 → <2.0.0
        c2->version = (SemVer){major + 1, 0, 0, "", "", true};
    } else if (minor >= 0 && minor != 0) {
        // ^0.2.3 → <0.3.0
        c2->version = (SemVer){0, minor + 1, 0, "", "", true};
    } else if (patch >= 0) {
        // ^0.0.3 → <0.0.4
        c2->version = (SemVer){0, 0, patch + 1, "", "", true};
    } else if (minor >= 0) {
        // ^0.0 → <0.1.0
        c2->version = (SemVer){0, 1, 0, "", "", true};
    } else {
        // ^0 → <1.0.0
        c2->version = (SemVer){1, 0, 0, "", "", true};
    }

    *p = s;
}

// parse one comparator set (space-separated comparators, possibly with tilde/caret)
static void parse_comparator_set(const char** p, SemVerComparatorSet* set) {
    const char* s = *p;

    while (*s && *s != '|') {
        s = skip_ws(s);
        if (!*s || *s == '|') break;

        if (*s == '~') {
            expand_tilde(&s, set);
        } else if (*s == '^') {
            expand_caret(&s, set);
        } else {
            // check for standalone wildcard: *, x, X
            if (*s == '*' || *s == 'x' || *s == 'X') {
                // match-all: >=0.0.0
                SemVerComparator* c = &set->comparators[set->count++];
                memset(c, 0, sizeof(*c));
                c->op = CMP_GE;
                c->version = (SemVer){0, 0, 0, "", "", true};
                s++;
                // skip trailing .x.x if present
                while (*s == '.' || *s == '*' || *s == 'x' || *s == 'X' || isdigit((unsigned char)*s)) s++;
                continue;
            }
            // check for hyphen range: "1.2.3 - 2.3.4"
            const char* saved = s;
            SemVerComparator low;
            if (parse_comparator(&s, &low)) {
                s = skip_ws(s);
                if (*s == '-' && s[1] == ' ') {
                    s += 2; // skip "- "
                    SemVerComparator high;
                    if (parse_comparator(&s, &high)) {
                        // hyphen: >=low <=high
                        SemVerComparator* c1 = &set->comparators[set->count++];
                        c1->op = CMP_GE;
                        c1->version = low.version;
                        SemVerComparator* c2 = &set->comparators[set->count++];
                        c2->op = CMP_LE;
                        c2->version = high.version;
                        continue;
                    }
                }
                // not a hyphen range — use the parsed comparator
                if (set->count < SEMVER_MAX_COMPARATORS) {
                    set->comparators[set->count++] = low;
                }
            } else {
                s++; // skip unparseable char
            }
        }

        if (set->count >= SEMVER_MAX_COMPARATORS) break;
    }

    *p = s;
}

SemVerRange semver_range_parse(const char* range_str) {
    SemVerRange range = {};
    if (!range_str || !*range_str) {
        // empty/null → match everything (treat as *)
        range.valid = true;
        range.set_count = 1;
        range.sets[0].count = 1;
        range.sets[0].comparators[0].op = CMP_GE;
        range.sets[0].comparators[0].version = (SemVer){0, 0, 0, "", "", true};
        return range;
    }

    const char* s = range_str;

    while (*s && range.set_count < SEMVER_MAX_SETS) {
        s = skip_ws(s);
        if (!*s) break;

        SemVerComparatorSet* set = &range.sets[range.set_count];
        set->count = 0;
        parse_comparator_set(&s, set);

        if (set->count > 0) {
            range.set_count++;
        }

        // skip "||"
        s = skip_ws(s);
        if (s[0] == '|' && s[1] == '|') {
            s += 2;
        } else if (*s == '|') {
            s++;
        }
    }

    range.valid = (range.set_count > 0);
    return range;
}

// ---------------------------------------------------------------------------
// Matching
// ---------------------------------------------------------------------------

static bool comparator_matches(const SemVerComparator* cmp, const SemVer* v) {
    int c = semver_compare(v, &cmp->version);
    switch (cmp->op) {
        case CMP_EQ: return c == 0;
        case CMP_LT: return c < 0;
        case CMP_LE: return c <= 0;
        case CMP_GT: return c > 0;
        case CMP_GE: return c >= 0;
    }
    return false;
}

bool semver_satisfies(const SemVer* version, const SemVerRange* range) {
    if (!range->valid) return false;

    // any set must match (OR)
    for (int i = 0; i < range->set_count; i++) {
        const SemVerComparatorSet* set = &range->sets[i];
        bool all_match = true;
        for (int j = 0; j < set->count; j++) {
            if (!comparator_matches(&set->comparators[j], version)) {
                all_match = false;
                break;
            }
        }
        if (all_match) return true;
    }
    return false;
}

int semver_max_satisfying(const SemVer* versions, int count, const SemVerRange* range) {
    int best = -1;
    for (int i = 0; i < count; i++) {
        if (!versions[i].valid) continue;
        if (!semver_satisfies(&versions[i], range)) continue;
        if (best < 0 || semver_compare(&versions[i], &versions[best]) > 0) {
            best = i;
        }
    }
    return best;
}
