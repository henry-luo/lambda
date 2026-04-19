// bash_cond.cpp — Conditional Engine (Phase G — Module 10)
//
// Implements regex matching with BASH_REMATCH, shopt-aware pattern matching,
// and file comparison operators.

#include "bash_cond.h"
#include "bash_runtime.h"
#include "bash_pattern.h"
#include "bash_errors.h"
#include "../lambda-data.hpp"
#include "../transpiler.hpp"
#include "../../lib/log.h"
#include "../../lib/memtrack.h"

#include <cstring>
#include <cerrno>
#include <regex.h>
#include <sys/stat.h>

// ============================================================================
// BASH_REMATCH storage
// ============================================================================

#define REMATCH_MAX 50

static char* rematch_groups[REMATCH_MAX];
static int rematch_count = 0;

static void rematch_clear(void) {
    for (int i = 0; i < rematch_count; i++) {
        if (rematch_groups[i]) {
            mem_free(rematch_groups[i]);
            rematch_groups[i] = NULL;
        }
    }
    rematch_count = 0;
}

static void rematch_store(const char* text, regmatch_t* matches, int nmatch) {
    rematch_clear();

    for (int i = 0; i < nmatch && i < REMATCH_MAX; i++) {
        if (matches[i].rm_so < 0) {
            rematch_groups[i] = NULL;
        } else {
            int len = (int)(matches[i].rm_eo - matches[i].rm_so);
            rematch_groups[i] = (char*)mem_alloc(len + 1, MEM_CAT_BASH_RUNTIME);
            memcpy(rematch_groups[i], text + matches[i].rm_so, len);
            rematch_groups[i][len] = '\0';
        }
        rematch_count = i + 1;
    }
}

// ============================================================================
// Regex match with BASH_REMATCH
// ============================================================================

extern "C" Item bash_cond_regex(Item string, Item pattern) {
    Item str_item = bash_to_string(string);
    Item pat_item = bash_to_string(pattern);
    String* str = it2s(str_item);
    String* pat = it2s(pat_item);

    if (!str || !pat) {
        bash_set_exit_code(1);
        return (Item){.item = b2it(false)};
    }

    const char* text = str->chars;
    const char* regex_pat = pat->chars;

    // clear previous BASH_REMATCH
    rematch_clear();

    // compile regex (REG_EXTENDED, no REG_NOSUB so we get capture groups)
    regex_t regex;
    int cflags = REG_EXTENDED;
    if (bash_get_option_nocasematch()) {
        cflags |= REG_ICASE;
    }

    int ret = regcomp(&regex, regex_pat, cflags);
    if (ret != 0) {
        char errbuf[256];
        regerror(ret, &regex, errbuf, sizeof(errbuf));
        bash_errmsg("conditional binary operator expected: %s", errbuf);
        regfree(&regex);
        bash_set_exit_code(2);
        return (Item){.item = b2it(false)};
    }

    // execute with capture groups
    regmatch_t matches[REMATCH_MAX];
    ret = regexec(&regex, text, REMATCH_MAX, matches, 0);
    regfree(&regex);

    if (ret == 0) {
        // match found — populate BASH_REMATCH
        // count actual groups
        int nmatch = 1;  // at least group 0
        for (int i = 1; i < REMATCH_MAX; i++) {
            if (matches[i].rm_so < 0) break;
            nmatch = i + 1;
        }
        rematch_store(text, matches, nmatch);
        log_debug("bash_cond_regex: '%s' =~ '%s' matched (%d groups)", text, regex_pat, rematch_count);
        bash_set_exit_code(0);
        return (Item){.item = b2it(true)};
    }

    log_debug("bash_cond_regex: '%s' =~ '%s' no match", text, regex_pat);
    bash_set_exit_code(1);
    return (Item){.item = b2it(false)};
}

// ============================================================================
// BASH_REMATCH access
// ============================================================================

extern "C" Item bash_get_rematch(Item index) {
    int64_t idx = bash_to_int_val(index);
    if (idx < 0 || idx >= rematch_count || !rematch_groups[idx]) {
        return (Item){.item = s2it(heap_create_name("", 0))};
    }
    return (Item){.item = s2it(heap_create_name(rematch_groups[idx], (int)strlen(rematch_groups[idx])))};
}

extern "C" Item bash_get_rematch_count(void) {
    return bash_int_to_item(rematch_count);
}

extern "C" Item bash_get_rematch_all(void) {
    Item arr = bash_array_new();
    for (int i = 0; i < rematch_count; i++) {
        if (rematch_groups[i]) {
            Item val = (Item){.item = s2it(heap_create_name(rematch_groups[i], (int)strlen(rematch_groups[i])))};
            Item idx = bash_int_to_item(i);
            bash_array_set(arr, idx, val);
        }
    }
    return arr;
}

extern "C" void bash_clear_rematch(void) {
    rematch_clear();
}

// ============================================================================
// Shopt-aware pattern matching
// ============================================================================

extern "C" Item bash_cond_pattern(Item string, Item pattern) {
    Item str_item = bash_to_string(string);
    Item pat_item = bash_to_string(pattern);
    String* str = it2s(str_item);
    String* pat = it2s(pat_item);

    if (!str || !pat) {
        bash_set_exit_code(1);
        return (Item){.item = b2it(false)};
    }

    // build flags from current shopt state
    int flags = 0;
    if (bash_get_option_extglob()) flags |= BASH_PAT_EXTGLOB;
    if (bash_get_option_nocasematch()) flags |= BASH_PAT_NOCASE;

    bool result = (bash_pattern_match(str->chars, pat->chars, flags) == 1);
    bash_set_exit_code(result ? 0 : 1);

    log_debug("bash_cond_pattern: '%s' == '%s' → %s (flags=0x%x)",
              str->chars, pat->chars, result ? "true" : "false", flags);

    return (Item){.item = b2it(result)};
}

// ============================================================================
// File comparison operators
// ============================================================================

// helper: get file mtime (or -1 if file doesn't exist)
static time_t file_mtime(const char* path) {
    struct stat st;
    if (stat(path, &st) < 0) return (time_t)-1;
    return st.st_mtime;
}

extern "C" Item bash_test_nt(Item file1, Item file2) {
    Item s1 = bash_to_string(file1);
    Item s2 = bash_to_string(file2);
    String* p1 = it2s(s1);
    String* p2 = it2s(s2);

    if (!p1 || !p2) {
        bash_set_exit_code(1);
        return (Item){.item = b2it(false)};
    }

    time_t t1 = file_mtime(p1->chars);
    time_t t2 = file_mtime(p2->chars);

    // if either file doesn't exist, result depends:
    // file1 -nt file2: true if file1 exists and file2 doesn't, or file1 newer
    bool result;
    if (t1 == (time_t)-1) {
        result = false;  // file1 doesn't exist
    } else if (t2 == (time_t)-1) {
        result = true;   // file1 exists, file2 doesn't
    } else {
        result = (t1 > t2);
    }

    bash_set_exit_code(result ? 0 : 1);
    return (Item){.item = b2it(result)};
}

extern "C" Item bash_test_ot(Item file1, Item file2) {
    Item s1 = bash_to_string(file1);
    Item s2 = bash_to_string(file2);
    String* p1 = it2s(s1);
    String* p2 = it2s(s2);

    if (!p1 || !p2) {
        bash_set_exit_code(1);
        return (Item){.item = b2it(false)};
    }

    time_t t1 = file_mtime(p1->chars);
    time_t t2 = file_mtime(p2->chars);

    bool result;
    if (t2 == (time_t)-1) {
        result = false;  // file2 doesn't exist
    } else if (t1 == (time_t)-1) {
        result = true;   // file2 exists, file1 doesn't
    } else {
        result = (t1 < t2);
    }

    bash_set_exit_code(result ? 0 : 1);
    return (Item){.item = b2it(result)};
}

extern "C" Item bash_test_ef(Item file1, Item file2) {
    Item s1 = bash_to_string(file1);
    Item s2 = bash_to_string(file2);
    String* p1 = it2s(s1);
    String* p2 = it2s(s2);

    if (!p1 || !p2) {
        bash_set_exit_code(1);
        return (Item){.item = b2it(false)};
    }

    struct stat st1, st2;
    if (stat(p1->chars, &st1) < 0 || stat(p2->chars, &st2) < 0) {
        bash_set_exit_code(1);
        return (Item){.item = b2it(false)};
    }

    bool result = (st1.st_dev == st2.st_dev && st1.st_ino == st2.st_ino);
    bash_set_exit_code(result ? 0 : 1);
    return (Item){.item = b2it(result)};
}
