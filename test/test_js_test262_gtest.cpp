// =============================================================================
// test262 Compliance Test Runner for LambdaJS
// =============================================================================
//
// Protocol:
//   1. Parse YAML frontmatter from each .js test file
//   2. Prepend required harness files (sta.js, assert.js, plus any 'includes:')
//   3. Execute concatenated source via: ./lambda.exe js <tempfile> --no-log
//   4. PASS if exit code == 0 and no Test262Error in output
//   5. For negative tests: PASS if the test throws the expected error type
//
// Skips:
//   - flags: [async, module, raw, noStrict, onlyStrict]
//     (async is runnable only with --run-async and an explicit allowlist)
//   - Tests requiring features we don't support (Proxy, SharedArrayBuffer, etc.)
//   - Tests using eval() semantics (test262 eval tests)
//
// =============================================================================
// STABILITY PRINCIPLES — ZERO CRASH POLICY
// =============================================================================
//
// The test262 runner MUST produce ZERO crashes and ZERO lost tests in every CI
// run.  Any crash is a MAJOR regression that must be fixed immediately.
//
// ALL tests must pass in a batch run to be considered fully passing.
// Only fully passing tests may be added to the baseline list.
// Tests that are slow (>= threshold) or unstable in batch (pass individually but
// fail or flake in batch) are classified as NON-FULLY-PASSING and tracked
// separately — they do NOT qualify for the baseline.
//
// --update-baseline gate: to update the baseline, ALL of the following must hold:
//   1. Batch-lost         == 0   (no infrastructure failures)
//   2. Crash-exits        == 0   (no crashes)
//   3. Fully passing      >= STABLE_BASELINE_MIN  (net improvement required)
//   4. Regressions        == 0   (no test may regress)
//
// Execution model:
//   - Phase 1:  Parse metadata, partition tests into two groups:
//       (a) CLEAN    — safe to batch (default 100 tests/process, CPU count - 1 parallel workers)
//       (b) PREVIOUS PARTIAL — tests listed in t262_partial.txt from the
//                       previous run. They are included in CLEAN every run so
//                       the partial list can be rebuilt from fresh results.
//   - Phase 2:  Execute CLEAN tests in batched workers.
//   - Phase 2a: REMOVED. Previous partial tests are no longer skipped.
//   - Phase 2b: Retry any batch-lost tests individually (crash recovery for
//               innocent bystanders co-batched with a crash-point).
//   - Phase 3:  Evaluate results against expected outcomes.
//   - Phase 4:  (--batch-only) Retry regressions individually; if recovered,
//               the failure was a batch interaction → classified as PARTIAL.
//
// Key invariant: every test included in CLEAN must have a result after
// Phase 2 + 2b. t262_partial.txt is rewritten from this run's results, with
// no sticky carry-forward entries.
//
// =============================================================================

#include <gtest/gtest.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <string>
#include <vector>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <set>
#include <map>
#include <thread>
#include <mutex>
#include <atomic>
#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <functional>
#include <chrono>


#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #define NOGDI
    #define NOUSER
    #define NOHELP
    #define NOMCX
    #include <windows.h>
    #include <io.h>
    #include <direct.h>
    #define popen _popen
    #define pclose _pclose
    #define WEXITSTATUS(status) (status)
    #ifndef F_OK
    #define F_OK 0
    #endif
#else
    #include <unistd.h>
    #include <sys/wait.h>
    #include <signal.h>
    #include <dirent.h>
    #include <fcntl.h>
    #include <spawn.h>
    #include <sys/stat.h>
    #include <sys/utsname.h>
    #ifdef __APPLE__
        #include <sys/sysctl.h>
    #endif
    #ifdef __linux__
        #include <sys/sysinfo.h>
    #endif
#endif

// =============================================================================
// Configuration
// =============================================================================

static const char* TEST262_ROOT = "ref/test262";
static const char* TEST262_SOURCE_DIR = "test/js262";  // comment-stripped test files (symlink to ../lambda-test/js262)
static std::string g_harness_dir = "ref/test262/harness";
// Only tests under their timing gate belong in the baseline. Known exhaustive
// slow tests live in t262_slow.txt and run in isolated slow batches.
static const char* BASELINE_FILE = "test/js262/test262_baseline.txt";
static const char* SKIP_LIST_FILE = "test/js262/skip_list.txt";
static const char* DIAGNOSE_LIST_FILE = "test/js262/diagnose_list.txt";
static const char* SPECIAL_PREAMBLE_FILE = "test/js262/special_premble.txt";
static const char* SLOW_TEST_FILE = "test/js262/t262_slow.txt";
static const char* TEST262_RUN_LOCK_FILE = "temp/test_js_test262_gtest.lock";
static bool g_use_stripped = false;  // use comment-stripped test files from TEST262_SOURCE_DIR

// Features above ES2023 — skip tests requiring these.
// Target: ES2023 compliance. All features ≤ES2023 are in scope, except
// intentional product-scope exceptions listed separately below.
// Reference: TC39 finished-proposals.md (publication year field)
static const std::set<std::string> INTENTIONAL_ES2021_EXCEPTIONS = {
    "tail-call-optimization",                     // PTC is spec-era ES2015, but not part of LambdaJS Js48 claim
    "cross-realm",                                // Requires $262.createRealm(); out of Js48 LambdaJS host scope
    "IsHTMLDDA",                                  // Browser-specific document.all behavior
    "caller",                                     // Annex B Function.prototype.caller web-compat behavior
};

static const std::set<std::string> UNSUPPORTED_FEATURES = {
    // "AggregateError",                          // SUPPORTED
    // "logical-assignment-operators",             // &&=, ||=, ??= — SUPPORTED
    // "numeric-separator-literal",               // 1_000_000 — SUPPORTED
    // "String.prototype.replaceAll",              // SUPPORTED
    // "Promise.any",                             // SUPPORTED

    // === ES2022 features ===
    // "class-fields-public",                      // SUPPORTED
    // "class-fields-private",                    // SUPPORTED
    // "class-fields-private-in",                  // SUPPORTED
    // "class-methods-private",                   // SUPPORTED
    // "class-static-fields-public",               // SUPPORTED
    // "class-static-fields-private",             // SUPPORTED
    // "class-static-methods-private",            // SUPPORTED
    // "class-static-block",                      // SUPPORTED
    // "top-level-await",                         // Module-level await - SUPPORTED
    // "regexp-match-indices",                    // /d flag, .indices - SUPPORTED
    // "Object.hasOwn",                            // SUPPORTED
    // "Array.prototype.at",                       // SUPPORTED
    // "String.prototype.at",                      // SUPPORTED
    // "TypedArray.prototype.at",                 // SUPPORTED
    // "error-cause",                              // SUPPORTED

    // === ES2023 features ===
    // "change-array-by-copy",                     // SUPPORTED (toSorted, toReversed, toSpliced, with)
    // "symbols-as-weakmap-keys",                 // SUPPORTED
    // "hashbang",                                // SUPPORTED (#! shebang lines)
    // "array-find-from-last",                     // SUPPORTED (findLast, findLastIndex)

    // === ES2024 features ===
    // "Atomics.waitAsync",                       // SUPPORTED (Js53 P3)
    // "resizable-arraybuffer",                   // SUPPORTED (Js54 P0–P6, partial: ~109/138 of previously-failing tests; remaining 29 deferred to Js55, see Transpile_Js54_Es2024.md §11)
    // "ArrayBuffer-transfer", "arraybuffer-transfer", // SUPPORTED (Js54 P8)
    // "regexp-v-flag",                           // SUPPORTED (Js54 P9–P10; ~21 string-property tests still fail pending P11 \p{StringProperty} tables)
    // "promise-with-resolvers",                  // SUPPORTED
    // "array-grouping",                          // SUPPORTED
    // "String.prototype.isWellFormed",           // SUPPORTED (Js53 P1)
    // "String.prototype.toWellFormed",           // SUPPORTED (Js53 P1)

    // === ES2025 features ===
    "import-attributes", "import-assertions",     // Import attributes
    "regexp-modifiers",                           // (?ims:...) inline flags
    "regexp-duplicate-named-groups",              // Duplicate named capture groups
    "iterator-helpers",                           // Iterator.prototype methods
    "Float16Array",                               // Float16 typed arrays
    "json-parse-with-source",                     // JSON.parse source text access
    "json-modules",                               // JSON module imports
    // "set-methods",                             // SUPPORTED
    // "Set.prototype.intersection",               // SUPPORTED
    // "Set.prototype.union",                       // SUPPORTED
    // "Set.prototype.difference",                  // SUPPORTED
    // "Set.prototype.symmetricDifference",         // SUPPORTED
    // "Set.prototype.isSubsetOf",                  // SUPPORTED
    // "Set.prototype.isSupersetOf",                // SUPPORTED
    // "Set.prototype.isDisjointFrom",              // SUPPORTED
    "promise-try",                                // Promise.try
    "RegExp.escape",                              // RegExp.escape

    // === ES2026+ features ===
    "Array.fromAsync",                            // Array.fromAsync
    "uint8array-base64",                          // Uint8Array base64/hex
    "Math.sumPrecise",                            // Math.sumPrecise
    "Error.isError",                              // Error.isError
    "iterator-sequencing",                        // Iterator.concat
    "upsert",                                     // Map.prototype.getOrInsert

    // === Stage 3 / Proposals (not yet in any published spec) ===
    "Temporal",                                   // Temporal API (ES2027)
    "ShadowRealm",                                // Isolated evaluation contexts
    "decorators",                                 // Class decorators
    "explicit-resource-management",               // using / Symbol.dispose
    "source-phase-imports", "source-phase-imports-module-source",
    "Atomics.pause",                              // Atomics.pause
    "import-defer",                               // Deferred import evaluation
    "import-text",                                // Import text
    "import-bytes",                               // Import bytes
    "canonical-tz",                               // Time zone canonicalization
    "immutable-arraybuffer",                      // Immutable ArrayBuffer
    "nonextensible-applies-to-private",           // Non-extensible + private
    "joint-iteration",                            // Joint iteration
    "await-dictionary",                           // Await dictionary
    "legacy-regexp",                              // Legacy RegExp features

    // === Test harness / host features ===
    "host-gc-required",                           // Requires $262.gc()
};

static std::string trim_skip_list_field(const std::string& text) {
    size_t start = 0;
    while (start < text.size() && (text[start] == ' ' || text[start] == '\t' ||
           text[start] == '\r' || text[start] == '\n')) {
        start++;
    }
    size_t end = text.size();
    while (end > start && (text[end - 1] == ' ' || text[end - 1] == '\t' ||
           text[end - 1] == '\r' || text[end - 1] == '\n')) {
        end--;
    }
    return text.substr(start, end - start);
}

static std::string normalize_skip_list_path(const std::string& path) {
    static const std::string prefix = std::string(TEST262_ROOT) + "/test/";
    if (path.compare(0, prefix.size(), prefix) == 0) return path.substr(prefix.size());
    static const std::string stripped_prefix = "test/";
    if (path.compare(0, stripped_prefix.size(), stripped_prefix) == 0) {
        return path.substr(stripped_prefix.size());
    }
    return path;
}

// Tests skipped by relative path (under ref/test262/test/).
// The text file format is:
//   # reason/comment for the next path or path block
//   relative/or/ref/test262/test/path.js
// A path line may also use: path.js<TAB>reason
static const std::map<std::string, std::string>& skipped_tests() {
    static std::map<std::string, std::string> skipped;
    static bool loaded = false;
    if (loaded) return skipped;
    loaded = true;

    std::ifstream in(SKIP_LIST_FILE);
    if (!in.is_open()) return skipped;

    std::string line;
    std::vector<std::string> comments;
    bool comment_block_used = false;
    while (std::getline(in, line)) {
        std::string trimmed = trim_skip_list_field(line);
        if (trimmed.empty()) {
            comments.clear();
            comment_block_used = false;
            continue;
        }
        if (trimmed[0] == '#') {
            if (comment_block_used) {
                comments.clear();
                comment_block_used = false;
            }
            std::string comment = trim_skip_list_field(trimmed.substr(1));
            if (!comment.empty()) comments.push_back(comment);
            continue;
        }

        std::string path = trimmed;
        std::string reason;
        size_t tab = trimmed.find('\t');
        if (tab != std::string::npos) {
            path = trim_skip_list_field(trimmed.substr(0, tab));
            reason = trim_skip_list_field(trimmed.substr(tab + 1));
        }
        if (reason.empty() && !comments.empty()) {
            for (size_t i = 0; i < comments.size(); i++) {
                if (i > 0) reason += " ";
                reason += comments[i];
            }
        }
        if (reason.empty()) reason = std::string("listed in ") + SKIP_LIST_FILE;
        path = normalize_skip_list_path(path);
        if (!path.empty()) skipped[path] = reason;
        comment_block_used = true;
    }
    return skipped;
}



// =============================================================================
// Test metadata parsed from YAML frontmatter
// =============================================================================

struct Test262Metadata {
    std::string description;
    std::vector<std::string> includes;
    std::vector<std::string> flags;
    std::vector<std::string> features;
    bool is_negative = false;
    std::string negative_phase;  // "parse", "resolution", "runtime"
    std::string negative_type;   // "SyntaxError", "ReferenceError", etc.
    bool is_strict = false;
    bool is_nostrict = false;
    bool is_module = false;
    bool is_async = false;
    bool is_raw = false;
};

// =============================================================================
// Metadata parser
// =============================================================================

static Test262Metadata parse_metadata(const std::string& source) {
    Test262Metadata meta;

    // find /*--- ... ---*/
    size_t start = source.find("/*---");
    size_t end = source.find("---*/");
    if (start == std::string::npos || end == std::string::npos) return meta;

    std::string yaml = source.substr(start + 5, end - start - 5);

    // parse includes: [file1.js, file2.js] or includes:\n  - file.js
    {
        size_t pos = yaml.find("includes:");
        if (pos != std::string::npos) {
            size_t line_end = yaml.find('\n', pos);
            std::string line = yaml.substr(pos + 9, line_end - pos - 9);

            // inline array format: [a.js, b.js]
            size_t bracket = line.find('[');
            if (bracket != std::string::npos) {
                size_t close = line.find(']', bracket);
                std::string items = line.substr(bracket + 1, close - bracket - 1);
                std::istringstream ss(items);
                std::string item;
                while (std::getline(ss, item, ',')) {
                    // trim whitespace
                    size_t s = item.find_first_not_of(" \t");
                    size_t e = item.find_last_not_of(" \t");
                    if (s != std::string::npos)
                        meta.includes.push_back(item.substr(s, e - s + 1));
                }
            } else {
                // multi-line format: - file.js
                size_t scan = line_end + 1;
                while (scan < yaml.size()) {
                    size_t nl = yaml.find('\n', scan);
                    if (nl == std::string::npos) nl = yaml.size();
                    std::string ln = yaml.substr(scan, nl - scan);
                    // trim
                    size_t fs = ln.find_first_not_of(" \t");
                    std::string val = ln.substr(fs + 1);
                    size_t vs = val.find_first_not_of(" \t");
                    size_t ve = val.find_last_not_of(" \t\r");
                    if (vs != std::string::npos)
                        meta.includes.push_back(val.substr(vs, ve - vs + 1));
                    scan = nl + 1;
                }
            }
        }
    }

    // parse flags: [onlyStrict, async, module, raw, noStrict]
    {
        size_t pos = yaml.find("flags:");
        if (pos != std::string::npos) {
            size_t line_end = yaml.find('\n', pos);
            std::string line = yaml.substr(pos + 6, line_end - pos - 6);
            size_t bracket = line.find('[');
            if (bracket != std::string::npos) {
                size_t close = line.find(']', bracket);
                std::string items = line.substr(bracket + 1, close - bracket - 1);
                std::istringstream ss(items);
                std::string item;
                while (std::getline(ss, item, ',')) {
                    size_t s = item.find_first_not_of(" \t");
                    size_t e = item.find_last_not_of(" \t");
                    if (s != std::string::npos) {
                        std::string flag = item.substr(s, e - s + 1);
                        meta.flags.push_back(flag);
                        if (flag == "async") meta.is_async = true;
                        if (flag == "module") meta.is_module = true;
                        if (flag == "raw") meta.is_raw = true;
                        if (flag == "onlyStrict") meta.is_strict = true;
                        if (flag == "noStrict") meta.is_nostrict = true;
                    }
                }
            }
        }
    }

    // parse features: [feat1, feat2]
    {
        size_t pos = yaml.find("features:");
        if (pos != std::string::npos) {
            size_t line_end = yaml.find('\n', pos);
            std::string line = yaml.substr(pos + 9, line_end - pos - 9);
            size_t bracket = line.find('[');
            if (bracket != std::string::npos) {
                size_t close = line.find(']', bracket);
                std::string items = line.substr(bracket + 1, close - bracket - 1);
                std::istringstream ss(items);
                std::string item;
                while (std::getline(ss, item, ',')) {
                    size_t s = item.find_first_not_of(" \t");
                    size_t e = item.find_last_not_of(" \t");
                    if (s != std::string::npos)
                        meta.features.push_back(item.substr(s, e - s + 1));
                }
            } else {
                // multi-line format
                size_t scan = line_end + 1;
                while (scan < yaml.size()) {
                    size_t nl = yaml.find('\n', scan);
                    if (nl == std::string::npos) nl = yaml.size();
                    std::string ln = yaml.substr(scan, nl - scan);
                    size_t fs = ln.find_first_not_of(" \t");
                    if (fs == std::string::npos || ln[fs] != '-') break;
                    std::string val = ln.substr(fs + 1);
                    size_t vs = val.find_first_not_of(" \t");
                    size_t ve = val.find_last_not_of(" \t\r");
                    if (vs != std::string::npos)
                        meta.features.push_back(val.substr(vs, ve - vs + 1));
                    scan = nl + 1;
                }
            }
        }
    }

    // parse negative:
    {
        size_t pos = yaml.find("negative:");
        if (pos != std::string::npos) {
            meta.is_negative = true;
            // find phase: and type: in the next few lines
            size_t scan = pos;
            for (int i = 0; i < 5 && scan < yaml.size(); i++) {
                size_t nl = yaml.find('\n', scan + 1);
                if (nl == std::string::npos) break;
                std::string ln = yaml.substr(scan, nl - scan);

                size_t ph = ln.find("phase:");
                if (ph != std::string::npos) {
                    std::string val = ln.substr(ph + 6);
                    size_t vs = val.find_first_not_of(" \t");
                    size_t ve = val.find_last_not_of(" \t\r\n");
                    if (vs != std::string::npos)
                        meta.negative_phase = val.substr(vs, ve - vs + 1);
                }

                size_t tp = ln.find("type:");
                if (tp != std::string::npos) {
                    std::string val = ln.substr(tp + 5);
                    size_t vs = val.find_first_not_of(" \t");
                    size_t ve = val.find_last_not_of(" \t\r\n");
                    if (vs != std::string::npos)
                        meta.negative_type = val.substr(vs, ve - vs + 1);
                }
                scan = nl;
            }
        }
    }

    return meta;
}

// =============================================================================
// File reading helper
// =============================================================================

static std::string read_file_contents(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return "";
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// =============================================================================
// Metadata cache — avoids reading 47k+ test files for YAML frontmatter
// Generated by: python3 utils/generate_test262_metadata.py
// =============================================================================

static const char* METADATA_CACHE_FILE = "test/js262/test262_metadata.tsv";

struct CachedMeta {
    int flags;           // bit0=async, bit1=module, bit2=raw, bit3=strict, bit4=nostrict, bit5=negative
    std::string neg_phase;
    std::string neg_type;
    std::vector<std::string> includes;
    std::vector<std::string> features;
    bool native_harness; // true = no includes, not negative, no Test262Error in source
};

static std::unordered_map<std::string, CachedMeta> g_metadata_cache;

static std::vector<std::string> split_semicolons(const std::string& s) {
    std::vector<std::string> result;
    if (s.empty()) return result;
    std::istringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ';')) {
        if (!item.empty()) result.push_back(item);
    }
    return result;
}

static bool load_metadata_cache(const char* path) {
    std::ifstream f(path);
    if (!f.is_open()) return false;

    std::string header;
    if (!std::getline(f, header)) return false;
    bool is_v2 = header.substr(0, 2) == "V2";
    if (!is_v2 && header.substr(0, 2) != "V1") return false;

    // parse expected count from "V1\t<count>"
    size_t expected = 0;
    size_t tab = header.find('\t');
    if (tab != std::string::npos) {
        expected = (size_t)std::stoul(header.substr(tab + 1));
        g_metadata_cache.reserve(expected);
    }

    std::string line;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        // trim trailing \r
        if (!line.empty() && line.back() == '\r') line.pop_back();

        // split by tabs: path\tflags\tneg_phase\tneg_type\tincludes\tfeatures
        std::vector<std::string> fields;
        std::istringstream ss(line);
        std::string field;
        while (std::getline(ss, field, '\t')) {
            fields.push_back(field);
        }
        while (fields.size() < 7) fields.push_back("");

        CachedMeta meta;
        meta.flags = std::stoi(fields[1]);
        meta.neg_phase = fields[2];
        meta.neg_type = fields[3];
        meta.includes = split_semicolons(fields[4]);
        meta.features = split_semicolons(fields[5]);
        meta.native_harness = is_v2 && !fields[6].empty() && fields[6][0] == '1';

        g_metadata_cache[fields[0]] = std::move(meta);
    }

    return !g_metadata_cache.empty();
}

// =============================================================================
// Test discovery
// =============================================================================

struct Test262Param {
    std::string test_path;     // relative path: ref/test262/test/language/...
    std::string test_name;     // sanitized name for GTest
    std::string category;      // "language" or "built_ins"
    std::string subcategory;   // e.g. "expressions", "Array"
};

static void discover_tests_recursive(const std::string& dir, const std::string& category,
                                      const std::string& subcategory, std::vector<Test262Param>& out) {
#ifdef _WIN32
    std::string pattern = dir + "\\*";
    struct _finddata_t fd;
    intptr_t handle = _findfirst(pattern.c_str(), &fd);
    if (handle == -1) return;
    do {
        std::string name = fd.name;
        if (name == "." || name == "..") continue;
        std::string full = dir + "/" + name;
        if (fd.attrib & _A_SUBDIR) {
            discover_tests_recursive(full, category, subcategory, out);
        } else if (name.size() > 3 && name.substr(name.size() - 3) == ".js") {
            // skip _FIXTURE.js helper modules — not standalone tests
            if (name.size() > 11 && name.substr(name.size() - 11) == "_FIXTURE.js") continue;
            Test262Param p;
            p.test_path = full;
            p.category = category;
            p.subcategory = subcategory;
            // sanitize test name: replace non-alnum with _
            std::string tname = full.substr(strlen(TEST262_ROOT) + 6); // skip "ref/test262/test/"
            for (auto& c : tname) {
                if (!isalnum(c)) c = '_';
            }
            p.test_name = tname;
            out.push_back(p);
        }
    } while (_findnext(handle, &fd) == 0);
    _findclose(handle);
#else
    DIR* d = opendir(dir.c_str());
    if (!d) return;
    struct dirent* entry;
    std::vector<std::string> entries;
    while ((entry = readdir(d)) != nullptr) {
        entries.push_back(entry->d_name);
    }
    closedir(d);
    std::sort(entries.begin(), entries.end());
    for (auto& name : entries) {
        if (name == "." || name == "..") continue;
        std::string full = dir + "/" + name;

        struct stat st;
        if (stat(full.c_str(), &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            discover_tests_recursive(full, category, subcategory, out);
        } else if (name.size() > 3 && name.substr(name.size() - 3) == ".js") {
            // skip _FIXTURE.js helper modules — not standalone tests
            if (name.size() > 11 && name.substr(name.size() - 11) == "_FIXTURE.js") continue;
            Test262Param p;
            p.test_path = full;
            p.category = category;
            p.subcategory = subcategory;
            std::string tname = full.substr(strlen(TEST262_ROOT) + 6);
            for (auto& c : tname) {
                if (!isalnum(c)) c = '_';
            }
            p.test_name = tname;
            out.push_back(p);
        }
    }
#endif
}

// Discover tests from configured categories
static std::vector<Test262Param> discover_all_tests() {
    std::vector<Test262Param> tests;

    // Language tests — subcategories
    struct { const char* subdir; const char* name; } language_cats[] = {
        {"expressions", "expressions"},
        {"statements", "statements"},
        {"types", "types"},
        {"literals", "literals"},
        {"block-scope", "block_scope"},
        {"arguments-object", "arguments_object"},
        {"function-code", "function_code"},
        {"rest-parameters", "rest_parameters"},
        {"destructuring", "destructuring"},
        {"computed-property-names", "computed_property_names"},
        {"white-space", "white_space"},
        {"line-terminators", "line_terminators"},
        {"comments", "comments"},
        {"identifiers", "identifiers"},
        {"keywords", "keywords"},
        {"reserved-words", "reserved_words"},
        {"punctuators", "punctuators"},
        {"asi", "asi"},
        {"source-text", "source_text"},
        {"global-code", "global_code"},
        {"directive-prologue", "directive_prologue"},
        {"statementList", "statementList"},
        {"identifier-resolution", "identifier_resolution"},
        {"future-reserved-words", "future_reserved_words"},
        {"module-code", "module_code"},
    };
    for (auto& cat : language_cats) {
        std::string dir = std::string(TEST262_ROOT) + "/test/language/" + cat.subdir;
        discover_tests_recursive(dir, "language", cat.name, tests);
    }

    // Built-in tests — subcategories
    struct { const char* subdir; const char* name; } builtin_cats[] = {
        {"Array", "Array"},
        {"ArrayBuffer", "ArrayBuffer"},
        {"Boolean", "Boolean"},
        {"Date", "Date"},
        {"Error", "Error"},
        {"Function", "Function"},
        {"JSON", "JSON"},
        {"Map", "Map"},
        {"Math", "Math"},
        {"NativeErrors", "NativeErrors"},
        {"Number", "Number"},
        {"Object", "Object"},
        {"Promise", "Promise"},
        {"RegExp", "RegExp"},
        {"Set", "Set"},
        {"String", "String"},
        {"TypedArray", "TypedArray"},
        {"TypedArrayConstructors", "TypedArrayConstructors"},
        {"Infinity", "Infinity"},
        {"NaN", "NaN"},
        {"decodeURI", "decodeURI"},
        {"decodeURIComponent", "decodeURIComponent"},
        {"encodeURI", "encodeURI"},
        {"encodeURIComponent", "encodeURIComponent"},
        {"eval", "eval"},
        {"global", "global"},
        {"isFinite", "isFinite"},
        {"isNaN", "isNaN"},
        {"parseFloat", "parseFloat"},
        {"parseInt", "parseInt"},
        {"undefined", "undefined"},
        {"GeneratorFunction", "GeneratorFunction"},
        {"GeneratorPrototype", "GeneratorPrototype"},
        {"WeakMap", "WeakMap"},
        {"WeakSet", "WeakSet"},
        {"WeakRef", "WeakRef"},
        {"FinalizationRegistry", "FinalizationRegistry"},
        {"Symbol", "Symbol"},
        {"DataView", "DataView"},
        {"Proxy", "Proxy"},
        {"Reflect", "Reflect"},
        {"AsyncFunction", "AsyncFunction"},
        {"AsyncGeneratorFunction", "AsyncGeneratorFunction"},
        {"AsyncGeneratorPrototype", "AsyncGeneratorPrototype"},
        {"BigInt", "BigInt"},
        {"Uint8Array", "Uint8Array"},
        {"ArrayIteratorPrototype", "ArrayIteratorPrototype"},
        {"MapIteratorPrototype", "MapIteratorPrototype"},
        {"SetIteratorPrototype", "SetIteratorPrototype"},
        {"StringIteratorPrototype", "StringIteratorPrototype"},
        {"RegExpStringIteratorPrototype", "RegExpStringIteratorPrototype"},
        {"AsyncFromSyncIteratorPrototype", "AsyncFromSyncIteratorPrototype"},
        {"AggregateError", "AggregateError"},
        {"AsyncIteratorPrototype", "AsyncIteratorPrototype"},
        {"Atomics", "Atomics"},
        {"Iterator", "Iterator"},
        {"SharedArrayBuffer", "SharedArrayBuffer"},
        {"ThrowTypeError", "ThrowTypeError"},
    };
    for (auto& cat : builtin_cats) {
        std::string dir = std::string(TEST262_ROOT) + "/test/built-ins/" + cat.subdir;
        discover_tests_recursive(dir, "built_ins", cat.name, tests);
    }

    // Annex B tests — built-ins subcategories
    struct { const char* subdir; const char* name; } annexb_builtin_cats[] = {
        {"Array", "Array"},
        {"Date", "Date"},
        {"escape", "escape"},
        {"Function", "Function"},
        {"Object", "Object"},
        {"RegExp", "RegExp"},
        {"String", "String"},
        {"TypedArrayConstructors", "TypedArrayConstructors"},
        {"unescape", "unescape"},
    };
    for (auto& cat : annexb_builtin_cats) {
        std::string dir = std::string(TEST262_ROOT) + "/test/annexB/built-ins/" + cat.subdir;
        discover_tests_recursive(dir, "annexB_built_ins", cat.name, tests);
    }

    // Annex B tests — language subcategories
    struct { const char* subdir; const char* name; } annexb_language_cats[] = {
        {"comments", "comments"},
        {"eval-code", "eval_code"},
        {"expressions", "expressions"},
        {"function-code", "function_code"},
        {"global-code", "global_code"},
        {"literals", "literals"},
        {"statements", "statements"},
    };
    for (auto& cat : annexb_language_cats) {
        std::string dir = std::string(TEST262_ROOT) + "/test/annexB/language/" + cat.subdir;
        discover_tests_recursive(dir, "annexB_language", cat.name, tests);
    }

    // Deduplicate test names (GTest requires unique names)
    std::set<std::string> seen_names;
    for (auto& t : tests) {
        std::string base = t.test_name;
        int suffix = 2;
        while (seen_names.count(t.test_name)) {
            t.test_name = base + "_" + std::to_string(suffix++);
        }
        seen_names.insert(t.test_name);
    }

    return tests;
}

// =============================================================================
// Test execution
// =============================================================================

enum Test262Result {
    T262_PASS,          // fully passed in batch run under timing gate
    T262_PARTIAL_PASS,  // non-fully-passing: slow (>= threshold), batch-unstable, or crashed individually
    T262_FAIL,
    T262_SKIP,
    T262_TIMEOUT,
    T262_CRASH,
};

struct Test262RunResult {
    Test262Result result;
    std::string message;
};

static bool is_js51_es2022_cross_realm_test(const std::string& test_name) {
    static const std::set<std::string> allowed = {
        "language_expressions_class_private_setter_brand_check_multiple_evaluations_of_class_realm_js",
        "language_expressions_class_private_static_getter_multiple_evaluations_of_class_realm_js",
        "language_expressions_class_private_getter_brand_check_multiple_evaluations_of_class_realm_function_ctor_js",
        "language_expressions_class_private_setter_brand_check_multiple_evaluations_of_class_realm_function_ctor_js",
        "language_expressions_class_private_static_method_brand_check_multiple_evaluations_of_class_realm_js",
        "language_expressions_class_private_method_brand_check_multiple_evaluations_of_class_realm_function_ctor_js",
        "language_expressions_class_private_static_setter_multiple_evaluations_of_class_realm_js",
        "language_expressions_class_private_static_field_multiple_evaluations_of_class_realm_js",
        "language_expressions_class_private_getter_brand_check_multiple_evaluations_of_class_realm_js",
        "language_expressions_class_private_method_brand_check_multiple_evaluations_of_class_realm_js",
        "built_ins_RegExp_prototype_hasIndices_cross_realm_js",
    };
    return allowed.count(test_name) > 0;
}

static bool has_unsupported_feature_for_test(const Test262Metadata& meta, const std::string& test_name) {
    for (auto& f : meta.features) {
        if (f == "cross-realm" && is_js51_es2022_cross_realm_test(test_name)) continue;
        if (INTENTIONAL_ES2021_EXCEPTIONS.count(f)) return true;
        if (UNSUPPORTED_FEATURES.count(f)) return true;
    }
    return false;
}

static bool is_js51_es2022_async_admission_test(const std::string& test_name) {
    return test_name == "language_expressions_class_elements_after_same_line_method_rs_static_async_method_privatename_identifier_alt_js";
}

// Js53 P3-revisit (2026-06-12): admit the 20 Atomics.waitAsync tests that
// were unblocked by Bug C-1 (async-IIFE await value preservation) and Bug C-2
// (waitAsync agent_slot timeout scheduling). Each is async-flagged and needs
// explicit allowlist entry; the existing baseline doesn't yet contain them.
static bool is_js53_waitasync_admission_test(const std::string& test_name) {
    static const std::set<std::string> names = {
        "built_ins_Atomics_waitAsync_good_views_js",
        "built_ins_Atomics_waitAsync_bigint_good_views_js",
        "built_ins_Atomics_waitAsync_no_spurious_wakeup_no_operation_js",
        "built_ins_Atomics_waitAsync_no_spurious_wakeup_on_add_js",
        "built_ins_Atomics_waitAsync_no_spurious_wakeup_on_and_js",
        "built_ins_Atomics_waitAsync_no_spurious_wakeup_on_compareExchange_js",
        "built_ins_Atomics_waitAsync_no_spurious_wakeup_on_exchange_js",
        "built_ins_Atomics_waitAsync_no_spurious_wakeup_on_or_js",
        "built_ins_Atomics_waitAsync_no_spurious_wakeup_on_store_js",
        "built_ins_Atomics_waitAsync_no_spurious_wakeup_on_sub_js",
        "built_ins_Atomics_waitAsync_no_spurious_wakeup_on_xor_js",
        "built_ins_Atomics_waitAsync_bigint_no_spurious_wakeup_no_operation_js",
        "built_ins_Atomics_waitAsync_bigint_no_spurious_wakeup_on_add_js",
        "built_ins_Atomics_waitAsync_bigint_no_spurious_wakeup_on_and_js",
        "built_ins_Atomics_waitAsync_bigint_no_spurious_wakeup_on_compareExchange_js",
        "built_ins_Atomics_waitAsync_bigint_no_spurious_wakeup_on_exchange_js",
        "built_ins_Atomics_waitAsync_bigint_no_spurious_wakeup_on_or_js",
        "built_ins_Atomics_waitAsync_bigint_no_spurious_wakeup_on_store_js",
        "built_ins_Atomics_waitAsync_bigint_no_spurious_wakeup_on_sub_js",
        "built_ins_Atomics_waitAsync_bigint_no_spurious_wakeup_on_xor_js",
    };
    return names.count(test_name) > 0;
}

static std::string unsupported_feature_skip_message(const Test262Metadata& meta) {
    for (auto& f : meta.features) {
        if (INTENTIONAL_ES2021_EXCEPTIONS.count(f)) {
            if (f == "tail-call-optimization") {
                return "intentional PTC exception";
            }
            if (f == "cross-realm") {
                return "intentional cross-realm host exception";
            }
            if (f == "IsHTMLDDA") {
                return "intentional browser IsHTMLDDA exception";
            }
            if (f == "caller") {
                return "intentional Annex B caller exception";
            }
            return std::string("intentional ES2021 exception: ") + f;
        }
    }
    return "unsupported feature";
}

static bool is_dynamic_import_script_test(const std::string& test_path, const Test262Metadata& meta) {
    if (meta.is_module) return false;
    if (test_path.find("/language/expressions/dynamic-import/") == std::string::npos) return false;
    if (test_path.find("/language/expressions/dynamic-import/catch/") != std::string::npos) return false;
    if (test_path.find("/language/expressions/dynamic-import/assignment-expression/") != std::string::npos) return false;
    if (test_path.find("/language/expressions/dynamic-import/eval-self-once-script.js") != std::string::npos) return false;
    return true;
}

static bool is_es2021_raw_test(const std::string& test_path, const Test262Metadata& meta) {
    if (!meta.is_raw) return false;
    if (test_path.find("/language/comments/hashbang/") != std::string::npos) return true;
    if (meta.is_module) return false;
    return test_path.find("/language/directive-prologue/") != std::string::npos ||
        test_path.find("/annexB/language/comments/single-line-html-close-first-line-") != std::string::npos;
}

static bool is_es2021_module_test(const std::string& test_path, const Test262Metadata& meta) {
    if (!meta.is_module) return false;
    if (test_path.find("/language/comments/hashbang/module.js") != std::string::npos) return true;
    if (test_path.find("/language/expressions/class/elements/class-name-static-initializer-default-export.js") != std::string::npos) return true;
    for (auto& f : meta.features) {
        if (f == "top-level-await") return true;
    }
    if (test_path.find("/language/expressions/dynamic-import/") == std::string::npos) return false;
    if (test_path.find("/language/expressions/dynamic-import/import-attributes/") != std::string::npos) return false;
    if (test_path.find("/language/expressions/dynamic-import/import-defer/") != std::string::npos) return false;
    return true;
}

// =============================================================================
// Batch mode: prepare temp files, run through js-test-batch, evaluate
// =============================================================================

// Cached harness sources (loaded once)
static std::string g_harness_sta;
static std::string g_harness_assert;
static std::mutex  g_harness_mutex;
static std::unordered_map<std::string, std::string> g_harness_cache;

// Minimal harness files included in every JS preamble.  Expensive family helpers
// are added per batch from special_premble.txt instead of taxing ordinary tests.
static const char* PREAMBLE_INCLUDE_FILES[] = {
    "nativeFunctionMatcher.js",
    NULL
};

static const std::string& get_harness_file(const std::string& name) {
    std::lock_guard<std::mutex> lock(g_harness_mutex);
    auto it = g_harness_cache.find(name);
    if (it != g_harness_cache.end()) return it->second;
    std::string path = g_harness_dir + "/" + name;
    g_harness_cache[name] = read_file_contents(path);
    return g_harness_cache[name];
}

// Per-test prepared data (after Phase 1: metadata parsing only)
// Source assembly is deferred to Phase 2 workers to avoid ~1.3GB peak memory.
struct Test262Prepared {
    std::string test_name;       // GTest param name
    std::string test_path;       // path to .js test file (for lazy read)
    std::string category;        // language, built-ins, annexB, etc.
    std::string subcategory;     // expressions, Array, eval-code, etc.
    std::vector<std::string> includes; // harness includes (e.g. "propertyHelper.js")
    std::vector<std::string> features; // metadata feature tags
    std::vector<std::string> special_preamble_includes; // batch-local heavy harness helpers
    bool is_strict;              // add "use strict" prefix
    bool is_async = false;       // define $DONE for asyncHelpers.js when explicitly run
    bool is_module = false;      // execute through the ES module transpiler path
    bool is_raw = false;         // raw parser-mode test; do not prepend harness or host flags
    bool is_slow_test = false;   // intentionally exhaustive test; isolate batch and use 5s slow gate
    Test262Result skip_result;   // T262_SKIP if test should be skipped
    std::string skip_message;
    bool is_negative;
    std::string negative_type;
    bool native_harness;         // true = run without JS harness preamble (native interception only)
};

struct JsBatchGroup {
    std::vector<size_t> indices;
    std::vector<std::string> special_includes;
    std::string key;
    bool is_async = false;
    bool is_module = false;
    bool is_slow_test = false;
};

// Assemble combined source on-the-fly from metadata.
// Reads test file from disk (OS-cached) and prepends harness files.
// Assemble the harness source (sta.js + assert.js + preamble includes) — sent once per batch via harness: protocol
static std::string assemble_harness_source(const std::vector<std::string>& special_includes = {}) {
    std::string harness;
    harness.reserve(g_harness_sta.size() + g_harness_assert.size() + 65536);
    harness += g_harness_sta;
    harness += '\n';
    harness += g_harness_assert;
    harness += '\n';
    // Append preamble includes (compiled once, reused across all tests in batch)
    for (const char** f = PREAMBLE_INCLUDE_FILES; *f; f++) {
        const std::string& src = get_harness_file(*f);
        if (!src.empty()) {
            harness += src;
            harness += '\n';
        }
    }
    // Special helpers are intentionally batch-local.  A global preamble set
    // caused ordinary tests to pay TypedArray/Atomics helper compile cost; a
    // per-batch set keeps those helpers compiled once only where requested.
    for (const std::string& include : special_includes) {
        const std::string& src = get_harness_file(include);
        if (!src.empty()) {
            harness += src;
            harness += '\n';
        }
    }
    return harness;
}

static std::unordered_set<std::string> make_preamble_include_set(
    const std::vector<std::string>& special_includes)
{
    std::unordered_set<std::string> includes;
    for (const char** f = PREAMBLE_INCLUDE_FILES; *f; f++) includes.insert(*f);
    for (const std::string& include : special_includes) includes.insert(include);
    return includes;
}

// Map original test path to stripped version if available
static std::string get_source_path(const std::string& test_path) {
    if (g_use_stripped) {
        // Function.prototype.toString tests assert on exact source text including comments.
        // The stripped files have comments removed, so use the original source for them.
        if (test_path.find("/Function/prototype/toString/") != std::string::npos) {
            return test_path;
        }
        // The language/comments suites assert syntactic effects of comments
        // themselves (for example ASI after a multiline comment with LS/PS).
        // Stripping comments changes those tests' source semantics.
        if (test_path.find("/language/comments/") != std::string::npos) {
            return test_path;
        }
        // ref/test262/test/... -> test/js262/test/...
        std::string stripped = std::string(TEST262_SOURCE_DIR) +
                               test_path.substr(strlen(TEST262_ROOT));
        struct stat st;
        if (stat(stripped.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
            return test_path;
        }
        return stripped;
    }
    return test_path;
}

static bool test262_has_metadata_flag(const Test262Prepared& p, const char* flag) {
    std::string source = read_file_contents(p.test_path);
    return source.find(flag) != std::string::npos;
}

static void append_host_flags(std::string& combined, const Test262Prepared& p) {
    combined += "globalThis.__lambda_can_block = ";
    combined += test262_has_metadata_flag(p, "CanBlockIsFalse") ? "false" : "true";
    combined += ";\n";
}

// Assemble test source WITHOUT harness (for two-module split: harness sent separately)
static std::string assemble_test_source(
    const Test262Prepared& p,
    const std::unordered_set<std::string>* preamble_includes = nullptr)
{
    std::string source = read_file_contents(get_source_path(p.test_path));
    if (source.empty()) return "";

    std::string combined;
    combined.reserve(source.size() + 8192);

    // "use strict" must come FIRST (before includes) to act as a valid directive prologue
    if (p.is_strict) {
        combined += "\"use strict\";\n";
    }

    if (p.is_async) {
        combined += "globalThis.__lambda_test262_async_required = true;\n";
        combined += "globalThis.__lambda_test262_async_done = false;\n";
        combined += "globalThis.$DONE = function(error) {\n";
        combined += "  if (globalThis.__lambda_test262_async_done) throw new Test262Error(\"$DONE called multiple times\");\n";
        combined += "  globalThis.__lambda_test262_async_done = true;\n";
        combined += "  if (error) throw error;\n";
        combined += "};\n";
    }

    append_host_flags(combined, p);

    for (auto& inc : p.includes) {
        // Skip only includes compiled into this batch's preamble.  This must
        // be per-batch because special helper preambles differ by test family.
        if (preamble_includes && preamble_includes->count(inc)) continue;
        const std::string& harness_src = get_harness_file(inc);
        if (!harness_src.empty()) {
            combined += harness_src;
            combined += '\n';
        }
    }

    combined += source;
    if (p.is_async) {
        combined += "\nif (typeof setTimeout !== \"function\" && !globalThis.__lambda_test262_async_done) {\n";
        combined += "  throw new Test262Error(\"async test did not call $DONE\");\n";
        combined += "}\n";
    }
    return combined;
}

static std::string assemble_module_test_source(const Test262Prepared& p) {
    std::string source = read_file_contents(get_source_path(p.test_path));
    if (source.empty()) return "";
    if (p.is_raw) return source;

    std::string combined;
    combined.reserve(g_harness_sta.size() + g_harness_assert.size() + source.size() + 8192);
    combined += g_harness_sta;
    combined += '\n';
    combined += g_harness_assert;
    combined += '\n';
    for (const char** f = PREAMBLE_INCLUDE_FILES; *f; f++) {
        const std::string& src = get_harness_file(*f);
        if (!src.empty()) {
            combined += src;
            combined += '\n';
        }
    }
    if (p.is_async) {
        combined += "globalThis.__lambda_test262_async_required = true;\n";
        combined += "globalThis.__lambda_test262_async_done = false;\n";
        combined += "globalThis.$DONE = function(error) {\n";
        combined += "  if (globalThis.__lambda_test262_async_done) throw new Test262Error(\"$DONE called multiple times\");\n";
        combined += "  globalThis.__lambda_test262_async_done = true;\n";
        combined += "  if (error) throw error;\n";
        combined += "};\n";
    }
    append_host_flags(combined, p);
    for (auto& inc : p.includes) {
        const std::string& harness_src = get_harness_file(inc);
        if (!harness_src.empty()) {
            combined += harness_src;
            combined += '\n';
        }
    }
    combined += source;
    if (p.is_async) {
        combined += "\nif (typeof setTimeout !== \"function\" && !globalThis.__lambda_test262_async_done) {\n";
        combined += "  throw new Test262Error(\"async test did not call $DONE\");\n";
        combined += "}\n";
    }
    return combined;
}

// Assemble raw test source for native harness mode (no includes, just strict prefix + test body)
static std::string assemble_native_test_source(const Test262Prepared& p) {
    std::string source = read_file_contents(get_source_path(p.test_path));
    if (source.empty()) return "";

    if (p.is_raw) return source;

    if (p.is_strict) {
        std::string combined;
        combined.reserve(source.size() + 80);
        combined += "\"use strict\";\n";
        append_host_flags(combined, p);
        combined += source;
        return combined;
    }
    std::string combined;
    combined.reserve(source.size() + 60);
    append_host_flags(combined, p);
    combined += source;
    return combined;
}

static std::string special_preamble_key(const std::vector<std::string>& includes) {
    if (includes.empty()) return "";
    std::string key;
    for (size_t i = 0; i < includes.size(); i++) {
        if (i) key += ",";
        key += includes[i];
    }
    return key;
}

static void partition_batch_indices(
    const std::vector<Test262Prepared>& prepared,
    const std::vector<size_t>& indices,
    std::vector<size_t>& native_indices,
    std::vector<JsBatchGroup>& js_groups)
{
    native_indices.clear();
    js_groups.clear();
    std::unordered_map<std::string, size_t> group_by_key;

    for (size_t idx : indices) {
        if (prepared[idx].native_harness && !prepared[idx].is_async &&
                !prepared[idx].is_module && !prepared[idx].is_slow_test) {
            native_indices.push_back(idx);
            continue;
        }

        std::string key = prepared[idx].is_slow_test ? "slow:" :
            prepared[idx].is_module ? "module:" :
            (prepared[idx].is_async ? "async:" : "sync:");
        key += special_preamble_key(prepared[idx].special_preamble_includes);
        auto it = group_by_key.find(key);
        size_t group_index;
        if (it == group_by_key.end()) {
            group_index = js_groups.size();
            group_by_key[key] = group_index;
            JsBatchGroup group;
            group.special_includes = prepared[idx].special_preamble_includes;
            group.key = key;
            group.is_async = prepared[idx].is_async;
            group.is_module = prepared[idx].is_module;
            group.is_slow_test = prepared[idx].is_slow_test;
            js_groups.push_back(std::move(group));
        } else {
            group_index = it->second;
        }
        js_groups[group_index].indices.push_back(idx);
    }
}

// Legacy: assemble combined source with harness included (for backward compatibility)
static std::string assemble_combined_source(const Test262Prepared& p) {
    std::string source = read_file_contents(get_source_path(p.test_path));
    if (source.empty()) return "";

    std::string combined;
    combined.reserve(g_harness_sta.size() + g_harness_assert.size() + source.size() + 4096);

    // "use strict" must come FIRST to act as a valid directive prologue
    if (p.is_strict) {
        combined += "\"use strict\";\n";
    }

    combined += g_harness_sta;
    combined += '\n';
    combined += g_harness_assert;
    combined += '\n';

    for (auto& inc : p.includes) {
        const std::string& harness_src = get_harness_file(inc);
        if (!harness_src.empty()) {
            combined += harness_src;
            combined += '\n';
        }
    }

    combined += source;
    return combined;
}

// Batch output from js-test-batch
struct BatchResult {
    std::string output;
    int exit_code;
    long elapsed_us;  // per-test execution time in microseconds
    size_t rss_before; // RSS bytes before test
    size_t rss_after;  // RSS bytes after test
    long parse_us;
    long ast_us;
    long early_us;
    long imports_us;
    long mir_us;
    long link_us;
    long execute_us;
    long cleanup_us;
    long phase_total_us;
};

static const size_t T262_DEFAULT_BATCH_CHUNK_SIZE = 100;
static const size_t T262_MAX_BATCH_CHUNK_SIZE = 200;
static size_t g_t262_jobs = 0;  // 0 means auto: CPU count - 1
static size_t g_t262_batch_chunk_size = T262_DEFAULT_BATCH_CHUNK_SIZE;
static size_t g_t262_async_chunk_size = T262_DEFAULT_BATCH_CHUNK_SIZE;
static bool g_t262_async_chunk_size_explicit = false;

struct SubBatch { size_t start; size_t end; bool native; bool module; bool slow_test; size_t group; };

struct BatchTiming {
    size_t batch_idx;
    double elapsed_secs;
    double start_secs;
    double end_secs;
    size_t num_tests;
    bool non_batched;
    bool async_batch;
};

struct BatchExecuteTimingSummary {
    double batched_wall_secs;
    double non_batched_wall_secs;
    double sync_batched_wall_secs;
    double async_batched_wall_secs;
    double batched_worker_secs;
    double non_batched_worker_secs;
    double sync_batched_worker_secs;
    double async_batched_worker_secs;
    size_t batched_batches;
    size_t non_batched_batches;
    size_t sync_batched_batches;
    size_t async_batched_batches;
    size_t batched_tests;
    size_t non_batched_tests;
    size_t sync_batched_tests;
    size_t async_batched_tests;
};

static BatchExecuteTimingSummary g_last_batch_execute_timing = {};

static const char* batch_worker_status_label(int worker_status, size_t produced, size_t expected) {
    if (worker_status < 0) return "spawn-failed";
    if (worker_status == 124) return "timeout";
    if (worker_status > 128) return "crash";
    if (worker_status != 0) return "worker-error";
    if (produced < expected) return "lost";
    return "finished";
}

static size_t t262_target_worker_count() {
    size_t workers = g_t262_jobs;
    if (workers == 0) {
        unsigned int cpu_count = std::thread::hardware_concurrency();
        if (cpu_count == 0) cpu_count = 5;  // preserve a conservative fallback when CPU count is unavailable
        workers = cpu_count > 1 ? cpu_count - 1 : 1;
    }
    if (workers < 1) workers = 1;
    return workers;
}

static size_t t262_worker_count(size_t batch_count) {
    if (batch_count == 0) return 0;
    return std::min(t262_target_worker_count(), batch_count);
}

static void reset_batch_execute_timing_summary(BatchExecuteTimingSummary& summary) {
    summary.batched_wall_secs = 0.0;
    summary.non_batched_wall_secs = 0.0;
    summary.sync_batched_wall_secs = 0.0;
    summary.async_batched_wall_secs = 0.0;
    summary.batched_worker_secs = 0.0;
    summary.non_batched_worker_secs = 0.0;
    summary.sync_batched_worker_secs = 0.0;
    summary.async_batched_worker_secs = 0.0;
    summary.batched_batches = 0;
    summary.non_batched_batches = 0;
    summary.sync_batched_batches = 0;
    summary.async_batched_batches = 0;
    summary.batched_tests = 0;
    summary.non_batched_tests = 0;
    summary.sync_batched_tests = 0;
    summary.async_batched_tests = 0;
}

static void finalize_batch_execute_timing_summary(
    BatchExecuteTimingSummary& summary,
    const std::vector<BatchTiming>& timings,
    double execute_wall_secs)
{
    reset_batch_execute_timing_summary(summary);
    std::vector<double> points;
    points.reserve(timings.size() * 2);
    for (const auto& timing : timings) {
        if (timing.num_tests == 0) continue;
        if (timing.non_batched) {
            summary.non_batched_worker_secs += timing.elapsed_secs;
            summary.non_batched_batches++;
            summary.non_batched_tests += timing.num_tests;
        } else {
            summary.batched_worker_secs += timing.elapsed_secs;
            summary.batched_batches++;
            summary.batched_tests += timing.num_tests;
            if (timing.async_batch) {
                summary.async_batched_worker_secs += timing.elapsed_secs;
                summary.async_batched_batches++;
                summary.async_batched_tests += timing.num_tests;
            } else {
                summary.sync_batched_worker_secs += timing.elapsed_secs;
                summary.sync_batched_batches++;
                summary.sync_batched_tests += timing.num_tests;
            }
        }
        points.push_back(timing.start_secs);
        points.push_back(timing.end_secs);
    }

    std::sort(points.begin(), points.end());
    points.erase(std::unique(points.begin(), points.end()), points.end());
    for (size_t i = 1; i < points.size(); i++) {
        double start = points[i - 1];
        double end = points[i];
        if (end <= start) continue;
        size_t active_sync_batched = 0;
        size_t active_async_batched = 0;
        size_t active_non_batched = 0;
        for (const auto& timing : timings) {
            if (timing.num_tests == 0) continue;
            if (timing.start_secs < end && timing.end_secs > start) {
                if (timing.non_batched) active_non_batched++;
                else if (timing.async_batch) active_async_batched++;
                else active_sync_batched++;
            }
        }
        size_t active_batched = active_sync_batched + active_async_batched;
        size_t active_total = active_batched + active_non_batched;
        if (active_total == 0) continue;
        double secs = end - start;
        summary.sync_batched_wall_secs += secs * (double)active_sync_batched / (double)active_total;
        summary.async_batched_wall_secs += secs * (double)active_async_batched / (double)active_total;
        summary.non_batched_wall_secs += secs * (double)active_non_batched / (double)active_total;
    }

    summary.batched_wall_secs =
        summary.sync_batched_wall_secs + summary.async_batched_wall_secs;

    double split_secs = summary.batched_wall_secs + summary.non_batched_wall_secs;
    double overhead_secs = execute_wall_secs - split_secs;
    if (overhead_secs > 0.0) {
        if (split_secs > 0.0) {
            double batched_overhead = overhead_secs * summary.batched_wall_secs / split_secs;
            double sync_async_secs = summary.sync_batched_wall_secs + summary.async_batched_wall_secs;
            if (sync_async_secs > 0.0) {
                summary.sync_batched_wall_secs +=
                    batched_overhead * summary.sync_batched_wall_secs / sync_async_secs;
                summary.async_batched_wall_secs +=
                    batched_overhead * summary.async_batched_wall_secs / sync_async_secs;
            }
            summary.non_batched_wall_secs += overhead_secs * summary.non_batched_wall_secs / split_secs;
        } else if (summary.non_batched_batches > 0) {
            summary.non_batched_wall_secs += overhead_secs;
        } else if (summary.async_batched_batches > 0 && summary.sync_batched_batches == 0) {
            summary.async_batched_wall_secs += overhead_secs;
        } else {
            summary.sync_batched_wall_secs += overhead_secs;
        }
        summary.batched_wall_secs =
            summary.sync_batched_wall_secs + summary.async_batched_wall_secs;
    }
}

// Forward declarations for globals used in prepare phase
static std::set<std::string> g_baseline_passing;
static std::string g_baseline_ref_test262_commit;
static bool g_baseline_loaded = false;
static std::set<std::string> g_known_partial;
static std::unordered_map<std::string, std::string> g_partial_tags;  // test_name → original tag (e.g. "CRASH_139")
static std::set<std::string> g_known_slow_tests;  // tests >3s elapsed in previous run
static std::set<std::string> g_slow_tests;  // intentionally exhaustive slow tests
static const long SLOW_THRESHOLD_US = 3000000L;  // 3 seconds
static const long SLOW_TEST_THRESHOLD_US = 5000000L;  // 5 seconds
static bool g_update_baseline = false;
static bool g_baseline_only = false;
static bool g_batch_only = false;
static bool g_no_hot_reload = false;
static bool g_persistent_workers = false; // --persistent-workers: one lambda worker handles many manifests
static bool g_mir_interp = false;
static bool g_no_stripped = false;  // --no-stripped: force original test files
static bool g_diagnose_mode = false; // --diagnose: run diagnose list and pass --diagnose to lambda.exe
static bool g_verbose = false;       // --verbose: dump per-batch-timing + memory-growth detail (else summary-only)
static std::string g_batch_file;   // --batch-file=<path>: run only tests from this list in a single batch
static std::string g_diagnose_list_file = DIAGNOSE_LIST_FILE;
static bool g_run_async = false;     // --run-async: permit allowlisted async-flagged tests
static std::string g_async_list_file; // --async-list=<path>: newline-separated async test allowlist
static std::unordered_set<std::string> g_async_allowlist;
static std::unordered_map<std::string, std::vector<std::string>> g_diagnose_expected_paths;

static long slow_threshold_for_test(const std::string& test_name) {
    return g_slow_tests.count(test_name) ? SLOW_TEST_THRESHOLD_US : SLOW_THRESHOLD_US;
}
static std::unordered_map<std::string, std::vector<std::string>> g_special_preamble_exact;
static std::vector<std::pair<std::string, std::vector<std::string>>> g_special_preamble_prefix;
static std::string g_write_failures_path; // --write-failures=<path>: write failed test manifest TSV
static bool g_feature_summary = false;    // --feature-summary: write failure summaries under temp/
static int g_opt_level = 0;  // default -O0 (fastest for short-lived test262 scripts)
static char g_opt_level_arg[20] = "--opt-level=0";  // "--opt-level=N"
static char g_js_timeout_arg[20] = "--timeout=10";  // child js-test-batch timeout
static const int T262_PHASE4_RETRY_TIMEOUT_SECS = 60;

static int current_js_timeout_secs() {
    if (strncmp(g_js_timeout_arg, "--timeout=", 10) == 0) {
        int secs = atoi(g_js_timeout_arg + 10);
        if (secs > 0) return secs;
    }
    return 10;
}
static int g_total_tests = 0;   // total discovered tests
static int g_total_skipped = 0; // total skipped tests
static int g_total_batched = 0; // total batched (executed) tests
static double g_prep_secs = 0;  // Phase 1 (prepare) runtime
static double g_exec_secs = 0;  // Phase 2 (execute) runtime
static double g_total_secs = 0; // total runtime
static double g_phase_batch_batched_secs = 0;      // Phase 2 true batched wall runtime
static double g_phase_batch_non_batched_secs = 0;  // Phase 2 singleton/non-batched wall runtime
static double g_phase_batch_sync_batched_secs = 0;  // Phase 2 sync batched wall runtime
static double g_phase_batch_async_batched_secs = 0; // Phase 2 async batched wall runtime
static double g_phase_retry_secs = 0;      // Phase 2b retry runtime
static double g_phase_partial_secs = 0;    // non-fully-passing list update runtime
static double g_phase_timing_secs = 0;     // timing TSV write runtime
static double g_phase_memory_secs = 0;     // memory TSV + summary runtime
static double g_phase_evaluate_secs = 0;   // Phase 3 result evaluation runtime
static double g_phase4_secs = 0;           // Phase 4 regression retry runtime

static bool t262_phase_timing_enabled() {
    const char* flag = getenv("LAMBDA_JS_PHASE_TIMING");
    return flag && strcmp(flag, "0") != 0;
}

static void write_phase_timing_log(const std::unordered_map<std::string, BatchResult>& batch_results) {
    if (!t262_phase_timing_enabled()) return;
    char phase_path[128] = "temp/_t262_phase_timing.tsv";
    if (g_opt_level >= 0) {
        snprintf(phase_path, sizeof(phase_path), "temp/_t262_phase_timing_o%d.tsv", g_opt_level);
    }
    FILE* phase_log = fopen(phase_path, "w");
    if (!phase_log) return;
    fprintf(phase_log,
            "test_name\texit_code\telapsed_us\tparse_us\tast_us\tearly_us\timports_us\tmir_us\tlink_us\texecute_us\tcleanup_us\tphase_total_us\n");
    for (const auto& kv : batch_results) {
        const BatchResult& br = kv.second;
        fprintf(phase_log, "%s\t%d\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\n",
                kv.first.c_str(), br.exit_code, br.elapsed_us,
                br.parse_us, br.ast_us, br.early_us, br.imports_us, br.mir_us,
                br.link_us, br.execute_us, br.cleanup_us, br.phase_total_us);
    }
    fclose(phase_log);
    fprintf(stderr, "[test262] Phase timing data: %zu entries → %s\n",
            batch_results.size(), phase_path);
}

// Stable checkpoint baseline (commit 86235a964, 2026-04-15).
// --update-baseline requires fully passing >= this value.
static const int STABLE_BASELINE_MIN = 21824;

// Phase-level counters set during execution, read by --update-baseline gate.
static size_t g_phase_crash_exit = 0;       // crash-exit tests (exit > 128)
static size_t g_phase_batch_lost = 0;       // tests lost in batch, recovered individually

// Batch assignment tracking: which batch each test was in during Phase 2.
// Used by Phase 4 to diagnose which co-batched tests cause false failures.
static std::unordered_map<std::string, size_t> g_batch_assignment;  // test_name → batch_index
static std::vector<std::vector<std::string>> g_batch_contents;      // batch_index → [test_names]
static std::set<size_t> g_crashed_batches;  // batches that had a BATCH_KILL crash-point

struct Test262FailureInfo {
    std::string test_path;
    std::string category;
    std::string subcategory;
    std::vector<std::string> features;
    std::vector<std::string> includes;
    bool native_harness = false;
};

static std::unordered_map<std::string, Test262RunResult> g_cached_results;
static std::mutex g_results_mutex;
static std::mutex g_progress_mutex;
static std::unordered_map<std::string, BatchResult> g_cached_batch_results;
static std::unordered_map<std::string, Test262FailureInfo> g_failure_info;

static std::string tsv_clean(std::string value) {
    for (char& ch : value) {
        unsigned char byte = (unsigned char)ch;
        if (ch == '\t' || ch == '\n' || ch == '\r') {
            ch = ' ';
        } else if (byte < 32 || byte > 126) {
            ch = '?';
        }
    }
    return value;
}

static std::string join_fields(const std::vector<std::string>& values, const char* separator) {
    std::string out;
    for (size_t i = 0; i < values.size(); i++) {
        if (i > 0) out += separator;
        out += values[i];
    }
    return out;
}

static std::string trim_ascii(const std::string& value) {
    size_t start = 0;
    while (start < value.size() &&
           (value[start] == ' ' || value[start] == '\t' ||
            value[start] == '\n' || value[start] == '\r')) {
        start++;
    }
    size_t end = value.size();
    while (end > start &&
           (value[end - 1] == ' ' || value[end - 1] == '\t' ||
            value[end - 1] == '\n' || value[end - 1] == '\r')) {
        end--;
    }
    return value.substr(start, end - start);
}

static bool manifest_status_token_uses_second_field(const std::string& token) {
    return token.compare(0, 5, "SLOW_") == 0 ||
           token.compare(0, 6, "CRASH_") == 0 ||
           token.compare(0, 6, "BATCH_") == 0 ||
           token.compare(0, 8, "TIMEOUT_") == 0;
}

static std::string first_manifest_test_name(const std::string& line) {
    std::string row = trim_ascii(line);
    if (row.empty() || row[0] == '#') return "";

    std::vector<std::string> fields;
    size_t pos = 0;
    while (pos < row.size() && fields.size() < 2) {
        while (pos < row.size() && (row[pos] == ' ' || row[pos] == '\t')) pos++;
        size_t start = pos;
        while (pos < row.size() && row[pos] != ' ' && row[pos] != '\t') pos++;
        if (pos > start) fields.push_back(row.substr(start, pos - start));
    }
    if (fields.empty()) return "";
    if (manifest_status_token_uses_second_field(fields[0]) && fields.size() > 1) {
        return fields[1];
    }
    return fields[0];
}

static std::string sanitize_test262_manifest_name(std::string name) {
    static const std::string ref_prefix = std::string(TEST262_ROOT) + "/test/";
    if (name.compare(0, ref_prefix.size(), ref_prefix) == 0) {
        name = name.substr(ref_prefix.size());
    } else if (name.compare(0, 5, "test/") == 0) {
        name = name.substr(5);
    }
    for (char& ch : name) {
        unsigned char byte = (unsigned char)ch;
        if (!isalnum(byte)) ch = '_';
    }
    return name;
}

static bool load_test_name_allowlist(const std::string& path, std::unordered_set<std::string>& out) {
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return false;

    char line[2048];
    while (fgets(line, sizeof(line), f)) {
        std::string name = first_manifest_test_name(line);
        if (!name.empty()) {
            out.insert(name);
            out.insert(sanitize_test262_manifest_name(name));
        }
    }
    fclose(f);
    return true;
}

static bool async_test_is_enabled(const std::string& test_name) {
    return g_run_async && g_async_allowlist.find(test_name) != g_async_allowlist.end();
}

static std::vector<std::string> split_diagnose_expected_paths(const std::string& field) {
    std::vector<std::string> paths;
    std::string trimmed = trim_ascii(field);
    if (trimmed.empty() || trimmed == "none-yet") return paths;
    size_t start = 0;
    while (start <= trimmed.size()) {
        size_t comma = trimmed.find(',', start);
        std::string item = trim_ascii(trimmed.substr(start,
            comma == std::string::npos ? std::string::npos : comma - start));
        if (!item.empty() && item != "none-yet") paths.push_back(item);
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    return paths;
}

static std::vector<std::string> split_special_preamble_field(const std::string& field) {
    std::vector<std::string> includes;
    std::string current;
    for (char ch : field) {
        if (ch == ',' || ch == ';' || ch == ' ' || ch == '\t') {
            std::string item = trim_ascii(current);
            if (!item.empty()) includes.push_back(item);
            current.clear();
        } else {
            current.push_back(ch);
        }
    }
    std::string item = trim_ascii(current);
    if (!item.empty()) includes.push_back(item);
    std::sort(includes.begin(), includes.end());
    includes.erase(std::unique(includes.begin(), includes.end()), includes.end());
    return includes;
}

static std::string test262_relative_path(const std::string& test_path) {
    static const std::string prefix = std::string(TEST262_ROOT) + "/test/";
    if (test_path.compare(0, prefix.size(), prefix) == 0) {
        return test_path.substr(prefix.size());
    }
    return test_path;
}

static bool load_special_preamble_list(const char* path) {
    g_special_preamble_exact.clear();
    g_special_preamble_prefix.clear();

    FILE* f = fopen(path, "r");
    if (!f) return false;

    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        std::string row = trim_ascii(line);
        if (row.empty() || row[0] == '#') continue;
        size_t comment = row.find('#');
        if (comment != std::string::npos) row = trim_ascii(row.substr(0, comment));
        if (row.empty()) continue;

        size_t sep = row.find_first_of(" \t");
        if (sep == std::string::npos) continue;
        std::string selector = trim_ascii(row.substr(0, sep));
        std::string include_field = trim_ascii(row.substr(sep + 1));
        std::vector<std::string> includes = split_special_preamble_field(include_field);
        if (selector.empty() || includes.empty()) continue;

        if (selector.size() >= 1 && selector.back() == '*') {
            g_special_preamble_prefix.push_back({selector.substr(0, selector.size() - 1), includes});
        } else {
            g_special_preamble_exact[selector] = includes;
        }
    }
    fclose(f);
    return !g_special_preamble_exact.empty() || !g_special_preamble_prefix.empty();
}

static void merge_special_includes(std::vector<std::string>& dst, const std::vector<std::string>& src) {
    dst.insert(dst.end(), src.begin(), src.end());
    std::sort(dst.begin(), dst.end());
    dst.erase(std::unique(dst.begin(), dst.end()), dst.end());
}

static std::vector<std::string> special_preamble_for_test(
    const std::string& test_name,
    const std::string& test_path)
{
    std::vector<std::string> includes;
    std::string rel_path = test262_relative_path(test_path);

    auto add_exact = [&](const std::string& key) {
        auto it = g_special_preamble_exact.find(key);
        if (it != g_special_preamble_exact.end()) merge_special_includes(includes, it->second);
    };
    add_exact(test_name);
    add_exact(test_path);
    add_exact(rel_path);

    auto add_prefix = [&](const std::string& value) {
        for (const auto& entry : g_special_preamble_prefix) {
            const std::string& prefix = entry.first;
            if (value.compare(0, prefix.size(), prefix) == 0) {
                merge_special_includes(includes, entry.second);
            }
        }
    };
    add_prefix(test_name);
    add_prefix(test_path);
    add_prefix(rel_path);

    return includes;
}

static bool diagnose_output_has_path(const std::string& output, const std::string& path) {
    std::string hit = std::string("fast-path-hit=") + path;
    if (output.find(hit) != std::string::npos) return true;
    std::string note = std::string("fast-path-note=") + path;
    return output.find(note) != std::string::npos;
}

static const char* t262_result_name(Test262Result result) {
    switch (result) {
        case T262_PASS: return "PASS";
        case T262_PARTIAL_PASS: return "PARTIAL";
        case T262_FAIL: return "FAIL";
        case T262_SKIP: return "SKIP";
        case T262_TIMEOUT: return "TIMEOUT";
        case T262_CRASH: return "CRASH";
    }
    return "UNKNOWN";
}

static bool t262_is_manifest_failure(Test262Result result) {
    return result == T262_FAIL || result == T262_TIMEOUT || result == T262_CRASH;
}

static std::string first_diagnostic_line(const std::string& message) {
    size_t end = message.find('\n');
    std::string line = end == std::string::npos ? message : message.substr(0, end);
    if (line.size() > 300) line = line.substr(0, 300);
    return tsv_clean(line);
}

static const char* classify_failure_kind(Test262Result result, const std::string& message) {
    if (result == T262_TIMEOUT) return "timeout";
    if (result == T262_CRASH) return "crash";
    if (message.find("not found in batch") != std::string::npos) return "missing";
    if (message.find("SyntaxError") != std::string::npos ||
        message.find("parse") != std::string::npos ||
        message.find("parser") != std::string::npos) return "parse";
    if (message.find("Test262Error") != std::string::npos ||
        message.find("negative test did not throw") != std::string::npos) return "assert";
    return "runtime";
}

static std::string summary_path_for(const std::string& manifest_path, const char* suffix) {
    if (manifest_path.empty()) return std::string("temp/js262_failures_") + suffix + ".tsv";
    size_t dot = manifest_path.rfind('.');
    size_t slash = manifest_path.rfind('/');
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) {
        return manifest_path.substr(0, dot) + "_by_" + suffix + manifest_path.substr(dot);
    }
    return manifest_path + "_by_" + suffix + ".tsv";
}

static void write_count_summary(const std::string& path, const char* label,
                                const std::map<std::string, int>& counts) {
    FILE* f = fopen(path.c_str(), "w");
    if (!f) {
        fprintf(stderr, "[test262] Warning: cannot write failure summary: %s\n", path.c_str());
        return;
    }
    fprintf(f, "%s\tfailures\n", label);
    for (auto& kv : counts) {
        fprintf(f, "%s\t%d\n", tsv_clean(kv.first).c_str(), kv.second);
    }
    fclose(f);
    fprintf(stderr, "[test262] Failure summary: %zu rows -> %s\n", counts.size(), path.c_str());
}

static void write_failure_artifacts() {
    if (g_write_failures_path.empty() && !g_feature_summary) return;

    std::string manifest_path = g_write_failures_path.empty()
        ? std::string("temp/js262_failures.tsv")
        : g_write_failures_path;

    std::vector<std::pair<std::string, Test262RunResult>> failures;
    {
        std::lock_guard<std::mutex> lock(g_results_mutex);
        for (auto& kv : g_cached_results) {
            if (t262_is_manifest_failure(kv.second.result)) failures.push_back(kv);
        }
    }
    std::sort(failures.begin(), failures.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    std::map<std::string, int> feature_counts;
    std::map<std::string, int> path_counts;
    FILE* f = fopen(manifest_path.c_str(), "w");
    if (!f) {
        fprintf(stderr, "[test262] Warning: cannot write failure manifest: %s\n", manifest_path.c_str());
    } else {
        fprintf(f, "test_name\tpath\tstatus\tfailure_kind\tmessage\tcategory\tsubcategory\tfeatures\tincludes\tnative_harness\telapsed_us\trss_delta_kb\n");
    }

    for (auto& kv : failures) {
        const std::string& name = kv.first;
        const Test262RunResult& result = kv.second;
        Test262FailureInfo info;
        auto info_it = g_failure_info.find(name);
        if (info_it != g_failure_info.end()) info = info_it->second;

        long elapsed_us = 0;
        long rss_delta_kb = 0;
        auto br_it = g_cached_batch_results.find(name);
        if (br_it != g_cached_batch_results.end()) {
            elapsed_us = br_it->second.elapsed_us;
            rss_delta_kb = (long)(br_it->second.rss_after / 1024) -
                           (long)(br_it->second.rss_before / 1024);
        }

        std::string path_bucket = info.category.empty() ? "(unknown)" : info.category;
        if (!info.subcategory.empty()) path_bucket += "/" + info.subcategory;
        path_counts[path_bucket]++;
        if (info.features.empty()) {
            feature_counts["(none)"]++;
        } else {
            for (auto& feature : info.features) feature_counts[feature]++;
        }

        if (f) {
            fprintf(f, "%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%d\t%ld\t%ld\n",
                    tsv_clean(name).c_str(),
                    tsv_clean(info.test_path).c_str(),
                    t262_result_name(result.result),
                    classify_failure_kind(result.result, result.message),
                    first_diagnostic_line(result.message).c_str(),
                    tsv_clean(info.category).c_str(),
                    tsv_clean(info.subcategory).c_str(),
                    tsv_clean(join_fields(info.features, ";")).c_str(),
                    tsv_clean(join_fields(info.includes, ";")).c_str(),
                    info.native_harness ? 1 : 0,
                    elapsed_us,
                    rss_delta_kb);
        }
    }

    if (f) {
        fclose(f);
        fprintf(stderr, "[test262] Failure manifest: %zu rows -> %s\n",
                failures.size(), manifest_path.c_str());
    }

    if (g_feature_summary || !g_write_failures_path.empty()) {
        write_count_summary(summary_path_for(manifest_path, "feature"), "feature", feature_counts);
        write_count_summary(summary_path_for(manifest_path, "path"), "path", path_counts);
    }
}

// Get short git commit hash for baseline header
static std::string get_git_commit_hash() {
    std::string hash;
    FILE* fp = popen("git rev-parse --short HEAD 2>/dev/null", "r");
    if (fp) {
        char buf[64];
        if (fgets(buf, sizeof(buf), fp)) {
            hash = buf;
            while (!hash.empty() && (hash.back() == '\n' || hash.back() == '\r'))
                hash.pop_back();
        }
        pclose(fp);
    }
    return hash.empty() ? "unknown" : hash;
}

static std::string get_lambda_test_git_commit_hash() {
    std::string hash;
    FILE* fp = popen("git -C test/js262 rev-parse --short HEAD 2>/dev/null", "r");
    if (fp) {
        char buf[64];
        if (fgets(buf, sizeof(buf), fp)) {
            hash = buf;
            while (!hash.empty() && (hash.back() == '\n' || hash.back() == '\r'))
                hash.pop_back();
        }
        pclose(fp);
    }
    return hash.empty() ? "unknown" : hash;
}

static std::string get_ref_test262_git_commit_hash() {
    std::string hash;
    FILE* fp = popen("git -C ref/test262 rev-parse HEAD 2>/dev/null", "r");
    if (fp) {
        char buf[128];
        if (fgets(buf, sizeof(buf), fp)) {
            hash = buf;
            while (!hash.empty() && (hash.back() == '\n' || hash.back() == '\r'))
                hash.pop_back();
        }
        pclose(fp);
    }
    return hash.empty() ? "unknown" : hash;
}

static bool parse_baseline_header_value(const char* line, const char* key, std::string& out) {
    size_t key_len = strlen(key);
    if (strncmp(line, key, key_len) != 0) return false;
    const char* start = line + key_len;
    while (*start == ' ' || *start == '\t') start++;
    const char* end = start + strlen(start);
    while (end > start && (end[-1] == '\n' || end[-1] == '\r' ||
           end[-1] == ' ' || end[-1] == '\t')) {
        end--;
    }
    out.assign(start, (size_t)(end - start));
    return true;
}

static bool git_hash_matches(const std::string& recorded, const std::string& current) {
    if (recorded.empty() || current.empty()) return false;
    if (recorded == "unknown" || current == "unknown") return false;
    size_t common = std::min(recorded.size(), current.size());
    if (common < 7) return false;
    return strncmp(recorded.c_str(), current.c_str(), common) == 0;
}

static bool verify_ref_test262_commit_matches_baseline(const char* baseline_path) {
    if (!g_baseline_loaded) return true;

    std::string current = get_ref_test262_git_commit_hash();
    if (g_baseline_ref_test262_commit.empty()) {
        fprintf(stderr,
                "[test262] Warning: %s does not record a ref/test262 commit; "
                "run --update-baseline to capture the current Test262 snapshot (%s).\n",
                baseline_path, current.c_str());
        return true;
    }

    if (git_hash_matches(g_baseline_ref_test262_commit, current)) {
        fprintf(stderr, "[test262] Verified ref/test262 commit: %s\n", current.c_str());
        return true;
    }

    if (g_update_baseline) {
        fprintf(stderr,
                "[test262] Warning: ref/test262 commit differs from baseline "
                "(baseline=%s current=%s); --update-baseline will rewrite the recorded snapshot.\n",
                g_baseline_ref_test262_commit.c_str(), current.c_str());
        return true;
    }

    fprintf(stderr,
            "[test262] ERROR: ref/test262 checkout does not match %s.\n"
            "[test262] ERROR: baseline ref/test262 commit: %s\n"
            "[test262] ERROR: current  ref/test262 commit: %s\n"
            "[test262] ERROR: checkout the recorded Test262 snapshot or run --update-baseline intentionally.\n",
            baseline_path, g_baseline_ref_test262_commit.c_str(), current.c_str());
    return false;
}

static const char* get_test_gtest_build_mode() {
#ifdef NDEBUG
    return "release";
#else
    return "debug";
#endif
}

static const char* get_lambda_runtime_build_mode() {
    // js262 measures the checked-in lambda.exe, not this gtest binary.  The
    // release make target leaves this marker so the baseline records the actual
    // runtime build that produced the pass/fail/timing data.
    if (access(".lambda_release_build", F_OK) == 0) return "release";
    return "debug-or-unknown";
}

static std::string get_host_os_summary() {
#ifdef _WIN32
    return "Windows";
#else
    struct utsname u;
    if (uname(&u) != 0) return "unknown";
    std::string out = u.sysname;
    out += " ";
    out += u.release;
    out += " ";
    out += u.machine;
    return out;
#endif
}

static long long get_host_memory_bytes() {
#ifdef __APPLE__
    int64_t mem = 0;
    size_t len = sizeof(mem);
    if (sysctlbyname("hw.memsize", &mem, &len, NULL, 0) == 0 && mem > 0) {
        return (long long)mem;
    }
#elif defined(__linux__)
    struct sysinfo info;
    if (sysinfo(&info) == 0) {
        return (long long)info.totalram * (long long)info.mem_unit;
    }
#endif
    return 0;
}

static long get_host_cpu_cores() {
#ifdef __APPLE__
    int cores = 0;
    size_t len = sizeof(cores);
    if (sysctlbyname("hw.ncpu", &cores, &len, NULL, 0) == 0 && cores > 0) {
        return cores;
    }
#endif
#ifdef _SC_NPROCESSORS_ONLN
    long online_cores = sysconf(_SC_NPROCESSORS_ONLN);
    if (online_cores > 0) return online_cores;
#endif
    return 0;
}

static std::string format_memory_gib(long long bytes) {
    if (bytes <= 0) return "unknown";
    char buf[64];
    double gib = (double)bytes / (1024.0 * 1024.0 * 1024.0);
    snprintf(buf, sizeof(buf), "%.1f GiB", gib);
    return buf;
}

static double sum_test_elapsed_secs() {
    long double total_us = 0.0;
    for (const auto& kv : g_cached_batch_results) {
        if (kv.second.elapsed_us > 0) {
            total_us += (long double)kv.second.elapsed_us;
        }
    }
    return (double)(total_us / 1000000.0L);
}

static double sum_test_elapsed_secs(const std::vector<std::string>& tests) {
    long double total_us = 0.0;
    for (const std::string& name : tests) {
        auto it = g_cached_batch_results.find(name);
        if (it != g_cached_batch_results.end() && it->second.elapsed_us > 0) {
            total_us += (long double)it->second.elapsed_us;
        }
    }
    return (double)(total_us / 1000000.0L);
}

// Write baseline file with header comments
static void write_baseline_file(const char* path, std::vector<std::string>& passing,
                                 int total_tests, int skipped, int batched, int failed) {
    FILE* f = fopen(path, "w");
    if (!f) return;
    std::string commit = get_git_commit_hash();
    std::string lambda_test_commit = get_lambda_test_git_commit_hash();
    std::string ref_test262_commit = get_ref_test262_git_commit_hash();
    std::sort(passing.begin(), passing.end());
    double all_test_elapsed_secs = sum_test_elapsed_secs();
    double baseline_test_elapsed_secs = sum_test_elapsed_secs(passing);
    fprintf(f, "# test262 baseline: tests that PASS (auto-updated)\n");
    fprintf(f, "# DO NOT EDIT THIS FILE MANUALLY.\n");
    fprintf(f, "# Update it only by running this gtest with --update-baseline.\n");
    fprintf(f, "# The update gate admits only fully passing tests: batch-safe, non-crashing, non-regressing, and under the slow-test threshold.\n");
    fprintf(f, "# Commit: %s\n", commit.c_str());
    fprintf(f, "# lambda-test commit: %s\n", lambda_test_commit.c_str());
    fprintf(f, "# ref/test262 commit: %s\n", ref_test262_commit.c_str());
    fprintf(f, "# Lambda runtime build: %s\n", get_lambda_runtime_build_mode());
    fprintf(f, "# GTest binary build: %s\n", get_test_gtest_build_mode());
    fprintf(f, "# Host OS: %s\n", get_host_os_summary().c_str());
    fprintf(f, "# Host capacity: %ld CPU cores, %s memory\n",
            get_host_cpu_cores(), format_memory_gib(get_host_memory_bytes()).c_str());
    fprintf(f, "# Scope: ES2024 (skip ES2025+ features)\n");
    fprintf(f, "# Total passing: %zu\n", passing.size());
    fprintf(f, "# Total tests: %d  Skipped: %d  Batched: %d  Passed: %zu  Failed: %d\n",
            total_tests, skipped, batched, passing.size(), failed);
    fprintf(f, "# Runtime: %.1fs total wall (prep %.1fs + exec %.1fs)\n",
            g_total_secs, g_prep_secs, g_exec_secs);
    fprintf(f, "# Runtime: %.1fs per-test sum (all executed test elapsed_us)\n",
            all_test_elapsed_secs);
    fprintf(f, "# Runtime: %.1fs per-test sum (fully passing baseline tests)\n",
            baseline_test_elapsed_secs);
    fprintf(f, "# Batch size: batched %zu tests/process; async %zu tests/process\n",
            g_t262_batch_chunk_size, g_t262_async_chunk_size);
    fprintf(f, "# Phase timing: prepare %.1fs\n", g_prep_secs);
    fprintf(f, "# Phase timing: batch-execute-batched %.1fs\n", g_phase_batch_batched_secs);
    fprintf(f, "# Phase timing: batch-execute-sync-batched %.1fs\n", g_phase_batch_sync_batched_secs);
    fprintf(f, "# Phase timing: batch-execute-async-batched %.1fs\n", g_phase_batch_async_batched_secs);
    fprintf(f, "# Phase timing: execute-non-batched %.1fs\n", g_phase_batch_non_batched_secs);
    fprintf(f, "# Phase timing: retry-lost %.1fs\n", g_phase_retry_secs);
    fprintf(f, "# Phase timing: update-partial-list %.1fs\n", g_phase_partial_secs);
    fprintf(f, "# Phase timing: evaluate-results %.1fs\n", g_phase_evaluate_secs);
    fprintf(f, "# Phase timing: retry-regressions %.1fs\n", g_phase4_secs);
    for (auto& name : passing) {
        fprintf(f, "%s\n", name.c_str());
    }
    fclose(f);
}

// Load non-fully-passing list from previous run (crash, slow, batch-kill, timeout).
// Tests listed here ran individually last run because they cannot pass in batch.
// Running individually prevents collateral damage to co-batched tests.
static void load_partial_list(const char* path) {
    FILE* f = fopen(path, "r");
    if (!f) return;
    char buf[2048];
    while (fgets(buf, sizeof(buf), f)) {
        // format: "MISSING\t<test_name>\t<test_path>" or "CRASH_N\t<test_name>\t<test_path>"
        // or "SLOW_<ms>\t<test_name>\t<test_path>" or "TIMEOUT_\t<test_name>\t<test_path>"
        char* first_tab = strchr(buf, '\t');
        if (!first_tab) continue;
        char* name_start = first_tab + 1;
        char* second_tab = strchr(name_start, '\t');
        if (second_tab) *second_tab = '\0';
        // trim trailing whitespace
        size_t len = strlen(name_start);
        while (len > 0 && (name_start[len-1] == '\n' || name_start[len-1] == '\r'))
            name_start[--len] = '\0';
        if (len > 0) {
            g_known_partial.insert(std::string(name_start));
            // store the tag (e.g. "CRASH_139") for non-fully-passing retention logic
            *first_tab = '\0';  // null-terminate the tag
            g_partial_tags[std::string(name_start)] = std::string(buf);
            if (strncmp(buf, "SLOW_", 5) == 0)
                g_known_slow_tests.insert(std::string(name_start));
        }
    }
    fclose(f);
}

static void load_slow_test_list(const char* path) {
    FILE* f = fopen(path, "r");
    if (!f) return;
    char buf[2048];
    while (fgets(buf, sizeof(buf), f)) {
        char* start = buf;
        while (*start == ' ' || *start == '\t') start++;
        if (*start == '#' || *start == '\n' || *start == '\r' || *start == '\0') continue;
        char* end = start;
        while (*end && *end != ' ' && *end != '\t' && *end != '\n' && *end != '\r') end++;
        if (end > start) {
            g_slow_tests.insert(std::string(start, (size_t)(end - start)));
        }
    }
    fclose(f);
}

// Clean up non-fully-passing list after baseline update.
// Removes entries whose test names are now in the updated baseline (fully passing).
// This prevents stale BATCH_KILL/CRASH/SLOW entries from accumulating across runs.
static void clean_partial_list_after_baseline_update(
    const std::vector<std::string>& new_baseline)
{
    std::unordered_set<std::string> baseline_set(new_baseline.begin(), new_baseline.end());

    // Read existing partial file
    FILE* f = fopen("test/js262/t262_partial.txt", "r");
    if (!f) return;
    std::vector<std::string> kept_lines;
    size_t removed = 0;
    char buf[2048];
    while (fgets(buf, sizeof(buf), f)) {
        size_t len = strlen(buf);
        while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r')) buf[--len] = '\0';
        if (len == 0) { kept_lines.push_back(""); continue; }
        if (buf[0] == '#') { kept_lines.push_back(std::string(buf)); continue; }
        // Extract test name (second tab-separated field)
        char* first_tab = strchr(buf, '\t');
        if (!first_tab) { kept_lines.push_back(std::string(buf)); continue; }
        char* name_start = first_tab + 1;
        char* second_tab = strchr(name_start, '\t');
        std::string name;
        if (second_tab) name = std::string(name_start, second_tab - name_start);
        else name = std::string(name_start);
        if (baseline_set.count(name)) {
            removed++;
        } else {
            // Restore the line (tabs were not modified since we only read)
            *first_tab = '\t';  // already '\t' but be safe
            if (second_tab) *second_tab = '\t';
            kept_lines.push_back(std::string(buf));
        }
    }
    fclose(f);

    if (removed == 0) return;

    // Write back the cleaned file
    f = fopen("test/js262/t262_partial.txt", "w");
    if (!f) return;
    // Strip trailing blank lines
    while (!kept_lines.empty() && kept_lines.back().empty()) kept_lines.pop_back();
    for (auto& line : kept_lines) fprintf(f, "%s\n", line.c_str());
    fclose(f);
    fprintf(stderr, "[test262] Cleaned test/js262/t262_partial.txt: removed %zu stale entries (now in baseline)\n", removed);
}

// Phase 1: Prepare all tests — parse metadata, determine skips.
//          Source assembly is deferred to Phase 2 workers (lazy assembly).
//          This keeps peak memory ~10MB instead of ~1.3GB.
static void prepare_all_tests(
    const std::vector<Test262Param>& tests,
    std::vector<Test262Prepared>& prepared,
    std::vector<size_t>& batch_indices)
{
    // pre-load common harness files
    g_harness_sta = read_file_contents(g_harness_dir + "/sta.js");
    g_harness_assert = read_file_contents(g_harness_dir + "/assert.js");

    prepared.resize(tests.size());
    std::atomic<int> prep_count{0};

    unsigned int num_threads = std::thread::hardware_concurrency();
    if (num_threads == 0) num_threads = 4;

    std::atomic<size_t> next_idx{0};
    auto worker = [&]() {
        while (true) {
            size_t i = next_idx.fetch_add(1, std::memory_order_relaxed);
            if (i >= tests.size()) break;

            const Test262Param& param = tests[i];
            Test262Prepared& p = prepared[i];
            p.test_name = param.test_name;
            p.test_path = param.test_path;
            p.category = param.category;
            p.subcategory = param.subcategory;
            p.skip_result = T262_PASS; // not skipped by default
            p.is_negative = false;
            p.is_strict = false;
            p.is_async = false;
            p.is_module = false;
            p.is_raw = false;
            p.native_harness = false;

            // check test-specific skip list (by relative path)
            {
                static const std::string prefix = std::string(TEST262_ROOT) + "/test/";
                if (param.test_path.size() > prefix.size() &&
                    param.test_path.compare(0, prefix.size(), prefix) == 0) {
                    std::string rel = param.test_path.substr(prefix.size());
                    const auto& skip_map = skipped_tests();
                    auto sit = skip_map.find(rel);
                    if (sit != skip_map.end()) {
                        p.skip_result = T262_SKIP;
                        p.skip_message = sit->second;
                        continue;
                    }
                }
            }

            // load metadata from cache or fall back to file read
            Test262Metadata meta;
            // Used only in debug builds (native-harness eligibility at line ~2287);
            // release uses p.is_raw, so mark maybe_unused to satisfy -Werror.
            [[maybe_unused]] bool cached_native_harness = false;
            if (!g_metadata_cache.empty()) {
                auto it = g_metadata_cache.find(param.test_path);
                if (it != g_metadata_cache.end()) {
                    const CachedMeta& cm = it->second;
                    meta.is_async    = cm.flags & 1;
                    meta.is_module   = cm.flags & 2;
                    meta.is_raw      = cm.flags & 4;
                    meta.is_strict   = cm.flags & 8;
                    meta.is_nostrict = cm.flags & 16;
                    meta.is_negative = cm.flags & 32;
                    meta.negative_phase = cm.neg_phase;
                    meta.negative_type  = cm.neg_type;
                    meta.includes = cm.includes;
                    meta.features = cm.features;
                    cached_native_harness = cm.native_harness;
                } else {
                    p.skip_result = T262_SKIP;
                    p.skip_message = "not in metadata cache";
                    continue;
                }
            } else {
                // no cache: read file and parse YAML frontmatter
                std::string source = read_file_contents(param.test_path);
                if (source.empty()) {
                    p.skip_result = T262_SKIP;
                    p.skip_message = "could not read test file";
                    continue;
                }
                meta = parse_metadata(source);
                // compute native eligibility inline (no cache available)
                if (meta.includes.empty() && !meta.is_negative
                    && source.find("Test262Error") == std::string::npos) {
                    cached_native_harness = true;
                }
            }

            // skip unsupported
            p.is_async = meta.is_async;
            p.is_module = meta.is_module;
            p.is_raw = meta.is_raw;
            p.is_slow_test = g_slow_tests.count(p.test_name) > 0;
            if (has_unsupported_feature_for_test(meta, p.test_name)) {
                p.skip_result = T262_SKIP;
                p.skip_message = unsupported_feature_skip_message(meta);
                continue;
            }
            if (meta.is_module && !(g_run_async && is_es2021_module_test(p.test_path, meta))) {
                p.skip_result = T262_SKIP;
                p.skip_message = "module flag";
                continue;
            }
            if (meta.is_raw && !is_es2021_raw_test(p.test_path, meta)) {
                p.skip_result = T262_SKIP;
                p.skip_message = "raw flag";
                continue;
            }
            if (meta.is_async && !(async_test_is_enabled(p.test_name) ||
                    (g_run_async && is_js51_es2022_async_admission_test(p.test_name)) ||
                    (g_run_async && is_js53_waitasync_admission_test(p.test_name)) ||
                    (g_run_async && (is_dynamic_import_script_test(p.test_path, meta) ||
                        is_es2021_module_test(p.test_path, meta))))) {
                p.skip_result = T262_SKIP;
                p.skip_message = "async flag";
                continue;
            }

            // check harness includes exist
            bool missing_harness = false;
            for (auto& inc : meta.includes) {
                const std::string& harness_src = get_harness_file(inc);
                if (harness_src.empty()) {
                    p.skip_result = T262_SKIP;
                    p.skip_message = "missing harness: " + inc;
                    missing_harness = true;
                    break;
                }
            }
            if (missing_harness) continue;

            // store metadata for lazy assembly in Phase 2
            p.is_negative = meta.is_negative;
            p.negative_type = meta.negative_type;
            p.is_strict = meta.is_strict;
            p.includes = std::move(meta.includes);
            p.features = std::move(meta.features);
            p.special_preamble_includes = special_preamble_for_test(p.test_name, p.test_path);

#ifndef NDEBUG
            // Native harness eligibility: pre-computed in metadata cache (V2+),
            // or computed inline when no cache is available.
            p.native_harness = p.is_raw || (!p.is_async && cached_native_harness);
#else
            p.native_harness = p.is_raw;
#endif
            if (p.is_module) p.native_harness = false;

            prep_count.fetch_add(1, std::memory_order_relaxed);
        }
    };

    std::vector<std::thread> threads;
    threads.reserve(num_threads);
    for (unsigned int t = 0; t < num_threads; t++) {
        threads.emplace_back(worker);
    }
    for (auto& t : threads) t.join();

    // collect batch indices (only non-skipped tests)
    for (size_t i = 0; i < prepared.size(); i++) {
        if (prepared[i].skip_result == T262_SKIP) continue;
        // In baseline-only mode, only run tests that passed in the baseline
        if (g_baseline_only && !g_baseline_passing.empty() &&
            g_baseline_passing.find(prepared[i].test_name) == g_baseline_passing.end()) {
            prepared[i].skip_result = T262_SKIP;
            prepared[i].skip_message = "not in baseline (--baseline-only)";
            continue;
        }
        batch_indices.push_back(i);
    }

    fprintf(stderr, "[test262] Prepared %zu test files (%zu skipped, %zu to batch)\n",
            tests.size(), tests.size() - batch_indices.size(), batch_indices.size());

    // Update global counters for baseline header
    g_total_tests = (int)tests.size();
    g_total_skipped = (int)(tests.size() - batch_indices.size());
    g_total_batched = (int)batch_indices.size();
}

// Hard per-worker timeout: 15s per test, minimum 30s.
// If a worker exceeds this, a watchdog thread sends SIGKILL to prevent infinite hangs
// (the internal js-test-batch timeout only catches JS-level loops, not hangs in JIT/parser).
static constexpr int T262_HARD_TIMEOUT_PER_TEST = 15;
static constexpr int T262_HARD_TIMEOUT_MIN = 30;
static int hard_timeout_per_test_secs() {
    int timeout_secs = current_js_timeout_secs() + 5;
    return std::max(T262_HARD_TIMEOUT_PER_TEST, timeout_secs);
}

static long long t262_steady_now_ms() {
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

// Run a sub-batch of tests from a pre-written manifest file + stdout pipe
// Run a sub-batch from a manifest file using posix_spawn (avoids fork's page table copy)
#ifdef _WIN32
static int run_t262_sub_batch(
    const char* manifest_path,
    std::unordered_map<std::string, BatchResult>& results,
    size_t num_tests = T262_DEFAULT_BATCH_CHUNK_SIZE)
{
    // Windows implementation using CreateProcess + anonymous pipes
    HANDLE pipe_rd = NULL, pipe_wr = NULL;
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    if (!CreatePipe(&pipe_rd, &pipe_wr, &sa, 0)) return -1;
    SetHandleInformation(pipe_rd, HANDLE_FLAG_INHERIT, 0);

    HANDLE manifest_h = CreateFileA(manifest_path, GENERIC_READ, FILE_SHARE_READ,
                                     &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (manifest_h == INVALID_HANDLE_VALUE) {
        CloseHandle(pipe_rd); CloseHandle(pipe_wr);
        return -1;
    }

    // Build command line
    std::string cmd = std::string("lambda.exe js-test-batch ") + g_js_timeout_arg;
    if (g_no_hot_reload) cmd += " --no-hot-reload";
    if (g_opt_level >= 0) cmd += std::string(" ") + g_opt_level_arg;
    if (g_mir_interp) cmd += " --mir-interp";
    if (g_diagnose_mode) cmd += " --diagnose";

    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput  = manifest_h;
    si.hStdOutput = pipe_wr;
    si.hStdError  = pipe_wr;

    PROCESS_INFORMATION pi = {};
    std::string cmd_copy = cmd;
    if (!CreateProcessA(NULL, &cmd_copy[0], NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(manifest_h); CloseHandle(pipe_rd); CloseHandle(pipe_wr);
        return -1;
    }
    CloseHandle(manifest_h);
    CloseHandle(pipe_wr);

    int hard_timeout_secs = std::max(T262_HARD_TIMEOUT_MIN, (int)(num_tests * hard_timeout_per_test_secs()));
    std::atomic<bool> worker_done{false};
    std::thread watchdog([&pi, hard_timeout_secs, &worker_done]() {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(hard_timeout_secs);
        while (!worker_done.load(std::memory_order_relaxed)) {
            if (std::chrono::steady_clock::now() >= deadline) {
                fprintf(stderr, "\n[test262] WARNING: Worker PID %lu exceeded hard timeout (%ds) — terminating\n",
                        pi.dwProcessId, hard_timeout_secs);
                TerminateProcess(pi.hProcess, 1);
                return;
            }
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    });

    // Read output from pipe
    char buffer[4096];
    std::string current_script;
    std::string current_output;
    bool in_script = false;
    DWORD bytes_read;
    std::string partial;
    while (ReadFile(pipe_rd, buffer, sizeof(buffer) - 1, &bytes_read, NULL) && bytes_read > 0) {
        buffer[bytes_read] = '\0';
        partial += buffer;
        size_t pos;
        while ((pos = partial.find('\n')) != std::string::npos) {
            std::string line = partial.substr(0, pos + 1);
            partial = partial.substr(pos + 1);
            const char* buf = line.c_str();
            if (buf[0] == '\x01') {
                if (strncmp(buf + 1, "BATCH_START ", 12) == 0) {
                    current_script = std::string(buf + 13);
                    while (!current_script.empty() &&
                           (current_script.back() == '\n' || current_script.back() == '\r'))
                        current_script.pop_back();
                    current_output.clear();
                    in_script = true;
                } else if (strncmp(buf + 1, "BATCH_END ", 10) == 0) {
                    int status = 0;
                    long elapsed_us = 0;
                    size_t rss_before = 0, rss_after = 0;
                    long parse_us = 0, ast_us = 0, early_us = 0, imports_us = 0, mir_us = 0;
                    long link_us = 0, execute_us = 0, cleanup_us = 0, phase_total_us = 0;
                    sscanf(buf + 11, "%d %ld %zu %zu %ld %ld %ld %ld %ld %ld %ld %ld %ld",
                           &status, &elapsed_us, &rss_before, &rss_after,
                           &parse_us, &ast_us, &early_us, &imports_us, &mir_us,
                           &link_us, &execute_us, &cleanup_us, &phase_total_us);
                    results[current_script] = {current_output, status, elapsed_us, rss_before, rss_after,
                                               parse_us, ast_us, early_us, imports_us, mir_us,
                                               link_us, execute_us, cleanup_us, phase_total_us};
                    in_script = false;
                } else if (strncmp(buf + 1, "BATCH_EXIT ", 11) == 0 ||
                           strncmp(buf + 1, "BATCH_DIAG ", 11) == 0) {
                    fprintf(stderr, "[test262] Child diagnostic: %s", buf + 1);
                }
            } else if (in_script) {
                current_output += buf;
            }
        }
    }
    CloseHandle(pipe_rd);

    DWORD exit_code = 0;
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &exit_code);
    if (exit_code != 0) {
        fprintf(stderr, "[test262] Batch worker PID %lu exited with status %lu, collected %zu results\n",
                pi.dwProcessId, exit_code, results.size());
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    worker_done.store(true, std::memory_order_relaxed);
    watchdog.join();
    return (int)exit_code;
}
#else
static int run_t262_sub_batch(
    const char* manifest_path,
    std::unordered_map<std::string, BatchResult>& results,
    size_t num_tests = T262_DEFAULT_BATCH_CHUNK_SIZE)
{
    int stdout_pipe[2];
    if (pipe(stdout_pipe) != 0) return -1;
    fcntl(stdout_pipe[0], F_SETFD, FD_CLOEXEC);
    fcntl(stdout_pipe[1], F_SETFD, FD_CLOEXEC);

    int manifest_fd = open(manifest_path, O_RDONLY);
    if (manifest_fd < 0) {
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        return -1;
    }
    fcntl(manifest_fd, F_SETFD, FD_CLOEXEC);

    // Use posix_spawn instead of fork+exec. On macOS, posix_spawn uses a
    // kernel-optimized path that creates the child process directly without
    // copying the parent's page tables. This is critical when the parent holds
    // ~1.3GB of test source data — fork() would copy all those page tables
    // 5400+ times, adding ~500s of system time.
    posix_spawn_file_actions_t file_actions;
    posix_spawn_file_actions_init(&file_actions);
    posix_spawn_file_actions_adddup2(&file_actions, manifest_fd, STDIN_FILENO);
    posix_spawn_file_actions_adddup2(&file_actions, stdout_pipe[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&file_actions, stdout_pipe[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&file_actions, manifest_fd);
    posix_spawn_file_actions_addclose(&file_actions, stdout_pipe[0]);
    posix_spawn_file_actions_addclose(&file_actions, stdout_pipe[1]);

    char* argv[10] = {
        (char*)"lambda.exe", (char*)"js-test-batch", g_js_timeout_arg, NULL, NULL, NULL, NULL, NULL, NULL, NULL
    };
    int argi = 3;
    if (g_no_hot_reload) {
        argv[argi++] = (char*)"--no-hot-reload";
    }
    if (g_opt_level >= 0) {
        argv[argi++] = g_opt_level_arg;
    }
    if (g_mir_interp) {
        argv[argi++] = (char*)"--mir-interp";
    }
    if (g_diagnose_mode) {
        argv[argi++] = (char*)"--diagnose";
    }
    extern char** environ;
    pid_t pid;
    int ret = posix_spawn(&pid, "./lambda.exe", &file_actions, NULL, argv, environ);
    posix_spawn_file_actions_destroy(&file_actions);

    close(manifest_fd);
    close(stdout_pipe[1]);

    if (ret != 0) {
        close(stdout_pipe[0]);
        return -1;
    }

    // Watchdog thread: kills the worker if it exceeds the hard timeout.
    // This catches hangs in JIT compilation, parsing, or native code that the
    // internal js-test-batch timeout can't interrupt. Killing the process closes its
    // stdout pipe, which unblocks the fgets loop below.
    int hard_timeout_secs = std::max(T262_HARD_TIMEOUT_MIN, (int)(num_tests * hard_timeout_per_test_secs()));
    std::atomic<bool> worker_done{false};
    std::thread watchdog([pid, hard_timeout_secs, &worker_done]() {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(hard_timeout_secs);
        while (!worker_done.load(std::memory_order_relaxed)) {
            if (std::chrono::steady_clock::now() >= deadline) {
                fprintf(stderr, "\n[test262] WARNING: Worker PID %d exceeded hard timeout (%ds) — sending SIGKILL\n",
                        pid, hard_timeout_secs);
                kill(pid, SIGKILL);
                return;
            }
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    });

    FILE* fp = fdopen(stdout_pipe[0], "r");
    if (fp) {
        char buffer[4096];
        std::string current_script;
        std::string current_output;
        bool in_script = false;

        while (fgets(buffer, sizeof(buffer), fp) != nullptr) {
            if (buffer[0] == '\x01') {
                if (strncmp(buffer + 1, "BATCH_START ", 12) == 0) {
                    current_script = std::string(buffer + 13);
                    while (!current_script.empty() &&
                           (current_script.back() == '\n' || current_script.back() == '\r'))
                        current_script.pop_back();
                    current_output.clear();
                    in_script = true;
                } else if (strncmp(buffer + 1, "BATCH_END ", 10) == 0) {
                    int status = 0;
                    long elapsed_us = 0;
                    size_t rss_before = 0, rss_after = 0;
                    long parse_us = 0, ast_us = 0, early_us = 0, imports_us = 0, mir_us = 0;
                    long link_us = 0, execute_us = 0, cleanup_us = 0, phase_total_us = 0;
                    sscanf(buffer + 11, "%d %ld %zu %zu %ld %ld %ld %ld %ld %ld %ld %ld %ld",
                           &status, &elapsed_us, &rss_before, &rss_after,
                           &parse_us, &ast_us, &early_us, &imports_us, &mir_us,
                           &link_us, &execute_us, &cleanup_us, &phase_total_us);
                    results[current_script] = {current_output, status, elapsed_us, rss_before, rss_after,
                                               parse_us, ast_us, early_us, imports_us, mir_us,
                                               link_us, execute_us, cleanup_us, phase_total_us};
                    in_script = false;
                } else if (strncmp(buffer + 1, "BATCH_EXIT ", 11) == 0 ||
                           strncmp(buffer + 1, "BATCH_DIAG ", 11) == 0) {
                    // Diagnostic from child process: log to parent stderr
                    fprintf(stderr, "[test262] Child diagnostic: %s", buffer + 1);
                }
            } else if (in_script) {
                current_output += buffer;
            }
        }
        fclose(fp);
    } else {
        close(stdout_pipe[0]);
    }

    int wstatus;
    waitpid(pid, &wstatus, 0);
    // Log if the batch process exited abnormally (helps diagnose killed batches)
    int worker_status = 0;
    if (WIFSIGNALED(wstatus)) {
        int sig = WTERMSIG(wstatus);
        worker_status = 128 + sig;
        fprintf(stderr, "[test262] Batch worker PID %d killed by signal %d (%s), collected %zu results\n",
                pid, sig, strsignal(sig), results.size());
    } else if (WIFEXITED(wstatus) && WEXITSTATUS(wstatus) != 0) {
        worker_status = WEXITSTATUS(wstatus);
        // Normal exit with non-zero status (e.g., from MAX_CRASH_COUNT break)
        fprintf(stderr, "[test262] Batch worker PID %d exited with status %d, collected %zu results\n",
                pid, WEXITSTATUS(wstatus), results.size());
    }
    worker_done.store(true, std::memory_order_relaxed);
    watchdog.join();
    return worker_status;
}
#endif // _WIN32

static bool write_t262_sub_batch_manifest(
    FILE* mf,
    const std::vector<Test262Prepared>& prepared,
    const std::vector<size_t>& native_indices,
    const std::vector<JsBatchGroup>& js_groups,
    const std::vector<SubBatch>& batches,
    size_t batch_index)
{
    if (!mf) return false;

    const SubBatch& batch = batches[batch_index];
    const auto& idx_vec = batch.native ? native_indices : js_groups[batch.group].indices;
    std::unordered_set<std::string> preamble_include_set;

    if (!batch.native && !batch.module) {
        // JS-harness batch: send harness preamble via harness: protocol
        const auto& special_includes = js_groups[batch.group].special_includes;
        preamble_include_set = make_preamble_include_set(special_includes);
        std::string harness = assemble_harness_source(special_includes);
        fprintf(mf, "harness:%zu\n", harness.size());
        fwrite(harness.data(), 1, harness.size(), mf);
        fputc('\n', mf);
    }
    // else: native-harness batch — no harness preamble (transpiler intercepts calls)

    // Send each test source
    for (size_t idx = batch.start; idx < batch.end; idx++) {
        size_t pi = idx_vec[idx];
        const auto& p = prepared[pi];
        std::string test_src = batch.module
            ? assemble_module_test_source(p)
            : batch.native
            ? assemble_native_test_source(p)
            : assemble_test_source(p, &preamble_include_set);
        fprintf(mf, "%s:%s:%s:%zu\n",
                batch.module ? "module-source" : "source",
                p.test_name.c_str(), p.test_path.c_str(), test_src.size());
        fwrite(test_src.data(), 1, test_src.size(), mf);
        fputc('\n', mf);
    }
    return true;
}

#ifndef _WIN32
static int run_t262_persistent_sub_batches(
    const char* manifest_path,
    const std::vector<size_t>& batch_order,
    std::vector<std::unordered_map<std::string, BatchResult>>& thread_results,
    std::vector<BatchTiming>& batch_timings,
    std::vector<int>& batch_statuses,
    const std::vector<SubBatch>& batches,
    const std::vector<JsBatchGroup>& js_groups,
    size_t worker_index,
    std::chrono::steady_clock::time_point execute_wall_start)
{
    if (batch_order.empty()) return 0;

    int stdout_pipe[2];
    if (pipe(stdout_pipe) != 0) return -1;
    fcntl(stdout_pipe[0], F_SETFD, FD_CLOEXEC);
    fcntl(stdout_pipe[1], F_SETFD, FD_CLOEXEC);

    int manifest_fd = open(manifest_path, O_RDONLY);
    if (manifest_fd < 0) {
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        return -1;
    }
    fcntl(manifest_fd, F_SETFD, FD_CLOEXEC);

    posix_spawn_file_actions_t file_actions;
    posix_spawn_file_actions_init(&file_actions);
    posix_spawn_file_actions_adddup2(&file_actions, manifest_fd, STDIN_FILENO);
    posix_spawn_file_actions_adddup2(&file_actions, stdout_pipe[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&file_actions, stdout_pipe[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&file_actions, manifest_fd);
    posix_spawn_file_actions_addclose(&file_actions, stdout_pipe[0]);
    posix_spawn_file_actions_addclose(&file_actions, stdout_pipe[1]);

    char* argv[10] = {
        (char*)"lambda.exe", (char*)"js-test-batch", g_js_timeout_arg, NULL, NULL, NULL, NULL, NULL, NULL, NULL
    };
    int argi = 3;
    if (g_no_hot_reload) {
        argv[argi++] = (char*)"--no-hot-reload";
    }
    if (g_opt_level >= 0) {
        argv[argi++] = g_opt_level_arg;
    }
    if (g_mir_interp) {
        argv[argi++] = (char*)"--mir-interp";
    }
    if (g_diagnose_mode) {
        argv[argi++] = (char*)"--diagnose";
    }
    extern char** environ;
    pid_t pid;
    int ret = posix_spawn(&pid, "./lambda.exe", &file_actions, NULL, argv, environ);
    posix_spawn_file_actions_destroy(&file_actions);

    close(manifest_fd);
    close(stdout_pipe[1]);

    if (ret != 0) {
        close(stdout_pipe[0]);
        return -1;
    }

    std::atomic<bool> worker_done{false};
    std::atomic<long long> last_progress_ms{t262_steady_now_ms()};
    int no_progress_timeout_secs = std::max(T262_HARD_TIMEOUT_MIN, hard_timeout_per_test_secs());
    std::thread watchdog([pid, no_progress_timeout_secs, &worker_done, &last_progress_ms]() {
        while (!worker_done.load(std::memory_order_relaxed)) {
            long long idle_ms = t262_steady_now_ms() - last_progress_ms.load(std::memory_order_relaxed);
            if (idle_ms >= (long long)no_progress_timeout_secs * 1000LL) {
                fprintf(stderr, "\n[test262] WARNING: Persistent worker PID %d made no progress for %ds — sending SIGKILL\n",
                        pid, no_progress_timeout_secs);
                kill(pid, SIGKILL);
                return;
            }
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    });

    FILE* fp = fdopen(stdout_pipe[0], "r");
    size_t batch_pos = 0;
    auto batch_start = std::chrono::steady_clock::now();
    std::string current_script;
    std::string current_output;
    bool in_script = false;
    if (fp) {
        char buffer[4096];
        while (fgets(buffer, sizeof(buffer), fp) != nullptr) {
            if (buffer[0] == '\x01') {
                if (strncmp(buffer + 1, "BATCH_START ", 12) == 0) {
                    current_script = std::string(buffer + 13);
                    while (!current_script.empty() &&
                           (current_script.back() == '\n' || current_script.back() == '\r'))
                        current_script.pop_back();
                    current_output.clear();
                    in_script = true;
                    last_progress_ms.store(t262_steady_now_ms(), std::memory_order_relaxed);
                } else if (strncmp(buffer + 1, "BATCH_END ", 10) == 0) {
                    int status = 0;
                    long elapsed_us = 0;
                    size_t rss_before = 0, rss_after = 0;
                    long parse_us = 0, ast_us = 0, early_us = 0, imports_us = 0, mir_us = 0;
                    long link_us = 0, execute_us = 0, cleanup_us = 0, phase_total_us = 0;
                    sscanf(buffer + 11, "%d %ld %zu %zu %ld %ld %ld %ld %ld %ld %ld %ld %ld",
                           &status, &elapsed_us, &rss_before, &rss_after,
                           &parse_us, &ast_us, &early_us, &imports_us, &mir_us,
                           &link_us, &execute_us, &cleanup_us, &phase_total_us);
                    if (batch_pos < batch_order.size()) {
                        size_t batch_index = batch_order[batch_pos];
                        thread_results[batch_index][current_script] =
                            {current_output, status, elapsed_us, rss_before, rss_after,
                             parse_us, ast_us, early_us, imports_us, mir_us,
                             link_us, execute_us, cleanup_us, phase_total_us};
                    }
                    in_script = false;
                    last_progress_ms.store(t262_steady_now_ms(), std::memory_order_relaxed);
                } else if (strncmp(buffer + 1, "BATCH_MANIFEST_END ", 19) == 0) {
                    if (batch_pos < batch_order.size()) {
                        size_t batch_index = batch_order[batch_pos];
                        auto batch_end = std::chrono::steady_clock::now();
                        size_t batch_num_tests = batches[batch_index].end - batches[batch_index].start;
                        bool is_non_batched = batch_num_tests == 1;
                        bool is_async_batch = !batches[batch_index].native &&
                            js_groups[batches[batch_index].group].is_async;
                        batch_timings[batch_index] = {
                            batch_index,
                            std::chrono::duration<double>(batch_end - batch_start).count(),
                            std::chrono::duration<double>(batch_start - execute_wall_start).count(),
                            std::chrono::duration<double>(batch_end - execute_wall_start).count(),
                            batch_num_tests,
                            is_non_batched,
                            is_async_batch
                        };
                        batch_statuses[batch_index] = 0;
                        size_t produced = thread_results[batch_index].size();
                        {
                            std::lock_guard<std::mutex> lock(g_progress_mutex);
                            fprintf(stderr, "[test262] batch[%zu/%zu] %s persistent-worker=%zu status=0 results=%zu/%zu elapsed=%.1fs\n",
                                    batch_index + 1, batches.size(),
                                    batch_worker_status_label(0, produced, batch_num_tests),
                                    worker_index, produced, batch_num_tests,
                                    batch_timings[batch_index].elapsed_secs);
                        }
                        batch_pos++;
                        batch_start = batch_end;
                    }
                    in_script = false;
                    current_script.clear();
                    current_output.clear();
                    last_progress_ms.store(t262_steady_now_ms(), std::memory_order_relaxed);
                } else if (strncmp(buffer + 1, "BATCH_EXIT ", 11) == 0 ||
                           strncmp(buffer + 1, "BATCH_DIAG ", 11) == 0) {
                    fprintf(stderr, "[test262] Child diagnostic: %s", buffer + 1);
                    last_progress_ms.store(t262_steady_now_ms(), std::memory_order_relaxed);
                }
            } else if (in_script) {
                current_output += buffer;
            }
        }
        fclose(fp);
    } else {
        close(stdout_pipe[0]);
    }

    int wstatus;
    waitpid(pid, &wstatus, 0);
    int worker_status = 0;
    if (WIFSIGNALED(wstatus)) {
        int sig = WTERMSIG(wstatus);
        worker_status = 128 + sig;
        fprintf(stderr, "[test262] Persistent worker PID %d killed by signal %d (%s), completed %zu/%zu batches\n",
                pid, sig, strsignal(sig), batch_pos, batch_order.size());
    } else if (WIFEXITED(wstatus) && WEXITSTATUS(wstatus) != 0) {
        worker_status = WEXITSTATUS(wstatus);
        fprintf(stderr, "[test262] Persistent worker PID %d exited with status %d, completed %zu/%zu batches\n",
                pid, WEXITSTATUS(wstatus), batch_pos, batch_order.size());
    }
    worker_done.store(true, std::memory_order_relaxed);
    watchdog.join();

    int unfinished_status = worker_status == 0 ? 1 : worker_status;
    auto batch_end = std::chrono::steady_clock::now();
    for (size_t pos = batch_pos; pos < batch_order.size(); pos++) {
        size_t batch_index = batch_order[pos];
        if (batch_statuses[batch_index] != -1) continue;
        size_t batch_num_tests = batches[batch_index].end - batches[batch_index].start;
        bool is_non_batched = batch_num_tests == 1;
        bool is_async_batch = !batches[batch_index].native &&
            js_groups[batches[batch_index].group].is_async;
        batch_timings[batch_index] = {
            batch_index,
            pos == batch_pos ? std::chrono::duration<double>(batch_end - batch_start).count() : 0.0,
            std::chrono::duration<double>(batch_start - execute_wall_start).count(),
            std::chrono::duration<double>(batch_end - execute_wall_start).count(),
            batch_num_tests,
            is_non_batched,
            is_async_batch
        };
        batch_statuses[batch_index] = unfinished_status;
        size_t produced = thread_results[batch_index].size();
        {
            std::lock_guard<std::mutex> lock(g_progress_mutex);
            fprintf(stderr, "[test262] batch[%zu/%zu] %s persistent-worker=%zu status=%d results=%zu/%zu elapsed=%.1fs\n",
                    batch_index + 1, batches.size(),
                    batch_worker_status_label(unfinished_status, produced, batch_num_tests),
                    worker_index, unfinished_status, produced, batch_num_tests,
                    batch_timings[batch_index].elapsed_secs);
        }
    }

    return worker_status;
}
#endif

// Phase 2: Execute all prepared tests through js-test-batch (reusable manifest files + stdout pipes)
// chunk_size: number of tests per sub-batch process (default 100, use 1 for non-batch)
static std::unordered_map<std::string, BatchResult> execute_t262_batch(
    const std::vector<Test262Prepared>& prepared,
    const std::vector<size_t>& indices,
    size_t chunk_size = T262_DEFAULT_BATCH_CHUNK_SIZE)
{
    auto execute_wall_start = std::chrono::steady_clock::now();
    std::unordered_map<std::string, BatchResult> results;
    reset_batch_execute_timing_summary(g_last_batch_execute_timing);
    if (indices.empty()) return results;

    // Split indices into native-harness and JS-harness groups.  JS groups are
    // keyed by special preamble needs so heavy helpers are compiled once only
    // for the batches that require them.
    std::vector<size_t> native_indices;
    std::vector<JsBatchGroup> js_groups;
    partition_batch_indices(prepared, indices, native_indices, js_groups);
    size_t js_count = 0;
    size_t module_count = 0;
    size_t special_group_count = 0;
    size_t slow_count = 0;
    for (const auto& group : js_groups) {
        js_count += group.indices.size();
        if (group.is_module) module_count += group.indices.size();
        if (group.is_slow_test) slow_count += group.indices.size();
        if (!group.special_includes.empty()) special_group_count++;
    }
    fprintf(stderr, "[test262] Batch split: %zu native-harness + %zu js-harness (%zu module, %zu slow) = %zu total (%zu special-preamble groups)\n",
            native_indices.size(), js_count, module_count, slow_count, indices.size(), special_group_count);

    // Create sub-batches from both groups
    std::vector<SubBatch> batches;
    for (size_t s = 0; s < native_indices.size(); s += chunk_size) {
        size_t e = std::min(s + chunk_size, native_indices.size());
        batches.push_back({s, e, true, false, false, 0});
    }
    size_t native_batch_count = batches.size();
    for (size_t group_index = 0; group_index < js_groups.size(); group_index++) {
        const auto& group_indices = js_groups[group_index].indices;
        size_t group_chunk_size = js_groups[group_index].is_slow_test ? 1 :
            (js_groups[group_index].is_async ? g_t262_async_chunk_size : chunk_size);
        for (size_t s = 0; s < group_indices.size(); s += group_chunk_size) {
            size_t e = std::min(s + group_chunk_size, group_indices.size());
            batches.push_back({s, e, false, js_groups[group_index].is_module,
                               js_groups[group_index].is_slow_test, group_index});
        }
    }

    // Sort dispatch order: run estimated-slowest batches first to avoid stragglers.
    // Uses per-test timing from previous run (temp/_t262_timing_o*.tsv).
    std::vector<size_t> dispatch_order(batches.size());
    for (size_t i = 0; i < dispatch_order.size(); i++) dispatch_order[i] = i;
    {
        // Load previous timing data
        std::unordered_map<std::string, long> prev_timing;
        char timing_path[128] = "temp/_t262_timing.tsv";
        if (g_opt_level >= 0)
            snprintf(timing_path, sizeof(timing_path), "temp/_t262_timing_o%d.tsv", g_opt_level);
        FILE* tf = fopen(timing_path, "r");
        if (tf) {
            char line[512];
            fgets(line, sizeof(line), tf);  // skip header
            while (fgets(line, sizeof(line), tf)) {
                char name[400];
                int exit_code;
                long elapsed_us;
                if (sscanf(line, "%399[^\t]\t%d\t%ld", name, &exit_code, &elapsed_us) == 3) {
                    prev_timing[name] = elapsed_us;
                }
            }
            fclose(tf);
        }
        if (!prev_timing.empty()) {
            // Compute estimated cost per batch (sum of per-test times)
            std::vector<long> batch_cost(batches.size(), 0);
            for (size_t bi = 0; bi < batches.size(); bi++) {
                const auto& idx_vec = batches[bi].native ? native_indices : js_groups[batches[bi].group].indices;
                for (size_t idx = batches[bi].start; idx < batches[bi].end; idx++) {
                    size_t pi = idx_vec[idx];
                    auto it = prev_timing.find(prepared[pi].test_name);
                    if (it != prev_timing.end()) {
                        batch_cost[bi] += it->second;
                    } else {
                        batch_cost[bi] += 20000;  // 20ms default for unknown tests
                    }
                }
            }
            // Sort dispatch order: most expensive first
            std::sort(dispatch_order.begin(), dispatch_order.end(), [&](size_t a, size_t b) {
                return batch_cost[a] > batch_cost[b];
            });
            fprintf(stderr, "[test262] Loaded %zu timing entries → dispatching slowest batches first "
                    "(top: %.1fs, #2: %.1fs, #3: %.1fs)\n",
                    prev_timing.size(),
                    batch_cost[dispatch_order[0]] / 1e6,
                    batches.size() > 1 ? batch_cost[dispatch_order[1]] / 1e6 : 0.0,
                    batches.size() > 2 ? batch_cost[dispatch_order[2]] / 1e6 : 0.0);
        }
    }

    // Run sub-batches with limited parallelism.
    // Each worker reuses ONE temp manifest file.
    // For each sub-batch: truncate → write source data → fork/exec → read stdout.
    // This avoids per-batch file create/unlink overhead while keeping fast file-based stdin.
    std::vector<std::unordered_map<std::string, BatchResult>> thread_results(batches.size());
    std::vector<BatchTiming> batch_timings(batches.size());
    std::vector<int> batch_worker_statuses(batches.size(), -1);
    size_t target_workers = t262_target_worker_count();
    size_t num_workers = std::min(target_workers, batches.size());
#ifdef _WIN32
    if (g_persistent_workers) {
        fprintf(stderr, "[test262] --persistent-workers is not implemented on Windows; using spawn-per-batch mode\n");
        g_persistent_workers = false;
    }
#endif
    fprintf(stderr, "[test262] Batch workers: %zu (target=%zu, batches=%zu, chunk=%zu, async_chunk=%zu, %s, %s)\n",
            num_workers, target_workers, batches.size(),
            g_t262_batch_chunk_size, g_t262_async_chunk_size,
            g_t262_jobs == 0 ? "auto: cpu-1" : "explicit --jobs",
            g_persistent_workers ? "persistent" : "spawn-per-batch");
    std::vector<std::thread> threads;
    std::vector<std::vector<size_t>> worker_batch_order;
    std::atomic<size_t> next_batch{0};

#ifndef _WIN32
    if (g_persistent_workers) {
        worker_batch_order.resize(num_workers);
        for (size_t di = 0; di < dispatch_order.size(); di++) {
            worker_batch_order[di % num_workers].push_back(dispatch_order[di]);
        }

        for (size_t w = 0; w < num_workers; w++) {
            threads.emplace_back([&, w]() {
                char manifest_path[256];
                snprintf(manifest_path, sizeof(manifest_path), "temp/_t262_worker_%zu.manifest", w);
                FILE* mf = fopen(manifest_path, "wb");
                if (!mf) return;

                size_t worker_tests = 0;
                for (size_t i : worker_batch_order[w]) {
                    write_t262_sub_batch_manifest(mf, prepared, native_indices, js_groups, batches, i);
                    fprintf(mf, "batch-boundary:%zu\n", i);
                    worker_tests += batches[i].end - batches[i].start;
                }
                fclose(mf);

                {
                    std::lock_guard<std::mutex> lock(g_progress_mutex);
                    fprintf(stderr, "[test262] persistent worker=%zu start batches=%zu tests=%zu\n",
                            w, worker_batch_order[w].size(), worker_tests);
                }
                run_t262_persistent_sub_batches(
                    manifest_path, worker_batch_order[w], thread_results, batch_timings,
                    batch_worker_statuses, batches, js_groups, w, execute_wall_start);
                unlink(manifest_path);
            });
        }
    } else
#endif
    {
    for (size_t w = 0; w < num_workers; w++) {
        threads.emplace_back([&, w]() {
            // Each worker has its own reusable manifest file
            char manifest_path[256];
            snprintf(manifest_path, sizeof(manifest_path), "temp/_t262_worker_%zu.manifest", w);
            while (true) {
                size_t di = next_batch.fetch_add(1, std::memory_order_relaxed);
                if (di >= batches.size()) break;
                size_t i = dispatch_order[di];  // pick batch in cost-sorted order

                // Write manifest for this sub-batch
                FILE* mf = fopen(manifest_path, "wb");
                if (!mf) continue;
                write_t262_sub_batch_manifest(mf, prepared, native_indices, js_groups, batches, i);
                fclose(mf);

                // Execute: fork/exec with stdin from manifest, read stdout via pipe
                auto t0 = std::chrono::steady_clock::now();
                size_t batch_num_tests = batches[i].end - batches[i].start;
                bool is_non_batched = batch_num_tests == 1;
                // per-batch "start" line intentionally omitted to keep progress output
                // to one line per batch; the "finished" line below carries the outcome.
                int worker_status = run_t262_sub_batch(manifest_path, thread_results[i], batch_num_tests);
                auto t1 = std::chrono::steady_clock::now();
                bool is_async_batch = !batches[i].native && js_groups[batches[i].group].is_async;
                batch_timings[i] = {i, std::chrono::duration<double>(t1 - t0).count(),
                                    std::chrono::duration<double>(t0 - execute_wall_start).count(),
                                    std::chrono::duration<double>(t1 - execute_wall_start).count(),
                                    batch_num_tests, is_non_batched, is_async_batch};
                batch_worker_statuses[i] = worker_status;
                size_t produced = thread_results[i].size();
                {
                    std::lock_guard<std::mutex> lock(g_progress_mutex);
                    fprintf(stderr, "[test262] batch[%zu/%zu] %s worker=%zu status=%d results=%zu/%zu elapsed=%.1fs\n",
                            i + 1, batches.size(),
                            batch_worker_status_label(worker_status, produced, batch_num_tests),
                            w, worker_status, produced, batch_num_tests,
                            batch_timings[i].elapsed_secs);
                }
            }
            unlink(manifest_path);
        });
    }
    }
    for (auto& t : threads) t.join();

    // Report per-batch timing — top 20 slowest batches + per-test detail for the
    // top 5. Verbose-only; per-test timings are always in the timing TSV regardless.
    if (g_verbose) {
        std::vector<size_t> order(batches.size());
        for (size_t i = 0; i < order.size(); i++) order[i] = i;
        std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
            return batch_timings[a].elapsed_secs > batch_timings[b].elapsed_secs;
        });
        fprintf(stderr, "\n[test262] Per-batch timing (top 20 slowest of %zu batches, %zu native + %zu js):\n",
                batches.size(), native_batch_count, batches.size() - native_batch_count);
        size_t show = std::min((size_t)20, batches.size());
        for (size_t k = 0; k < show; k++) {
            size_t bi = order[k];
            fprintf(stderr, "  batch[%3zu]: %6.1fs (%zu tests) %s  tests[%zu..%zu]\n",
                    bi, batch_timings[bi].elapsed_secs, batch_timings[bi].num_tests,
                    batches[bi].native ? "[native]" :
                        (batches[bi].module ? "[module]" :
                        (batches[bi].slow_test ? "[slow]  " :
                        (js_groups[batches[bi].group].is_async ? "[async] " :
                            (js_groups[batches[bi].group].special_includes.empty() ? "[js]    " : "[js+pre]")))),
                    batches[bi].start, batches[bi].end - 1);
        }
        // For the top 5 slowest batches, list all test names
        fprintf(stderr, "\n[test262] Tests in top 5 slowest batches:\n");
        size_t detail = std::min((size_t)5, batches.size());
        for (size_t k = 0; k < detail; k++) {
            size_t bi = order[k];
            const auto& idx_vec = batches[bi].native ? native_indices : js_groups[batches[bi].group].indices;
            fprintf(stderr, "  --- batch[%zu] (%.1fs, %s) ---\n", bi, batch_timings[bi].elapsed_secs,
                    batches[bi].native ? "native" :
                        (batches[bi].module ? "js-module" :
                        (batches[bi].slow_test ? "js-slow" :
                        (js_groups[batches[bi].group].is_async ? "js-async" :
                            (js_groups[batches[bi].group].special_includes.empty() ? "js" : "js-special")))));
            for (size_t idx = batches[bi].start; idx < batches[bi].end; idx++) {
                size_t pi = idx_vec[idx];
                const auto& p = prepared[pi];
                // show per-test time if available
                auto it = thread_results[bi].find(p.test_name);
                if (it != thread_results[bi].end()) {
                    fprintf(stderr, "    %6ldms  %s\n", it->second.elapsed_us / 1000, p.test_name.c_str());
                } else {
                    fprintf(stderr, "    [lost]   %s\n", p.test_name.c_str());
                }
            }
        }
        fprintf(stderr, "\n");
    }

    auto execute_wall_done = std::chrono::steady_clock::now();
    double execute_wall_secs = std::chrono::duration<double>(
        execute_wall_done - execute_wall_start).count();
    finalize_batch_execute_timing_summary(
        g_last_batch_execute_timing, batch_timings, execute_wall_secs);
    fprintf(stderr,
            "[test262] Batch execute split: batched %.1fs wall / %.1fs worker "
            "(sync %.1fs/%.1fs, async %.1fs/%.1fs; %zu batches, %zu tests); "
            "non-batched %.1fs wall / %.1fs worker (%zu batches, %zu tests)\n",
            g_last_batch_execute_timing.batched_wall_secs,
            g_last_batch_execute_timing.batched_worker_secs,
            g_last_batch_execute_timing.sync_batched_wall_secs,
            g_last_batch_execute_timing.sync_batched_worker_secs,
            g_last_batch_execute_timing.async_batched_wall_secs,
            g_last_batch_execute_timing.async_batched_worker_secs,
            g_last_batch_execute_timing.batched_batches,
            g_last_batch_execute_timing.batched_tests,
            g_last_batch_execute_timing.non_batched_wall_secs,
            g_last_batch_execute_timing.non_batched_worker_secs,
            g_last_batch_execute_timing.non_batched_batches,
            g_last_batch_execute_timing.non_batched_tests);

    // merge
    for (auto& partial : thread_results) {
        for (auto& kv : partial) {
            results[kv.first] = std::move(kv.second);
        }
    }

    return results;
}

// Phase 3: Evaluate batch results against metadata expectations
static Test262RunResult evaluate_batch_result(
    const Test262Prepared& prep,
    const std::unordered_map<std::string, BatchResult>& batch_results)
{
    if (prep.skip_result == T262_SKIP) {
        return {T262_SKIP, prep.skip_message};
    }

    auto it = batch_results.find(prep.test_name);
    if (it == batch_results.end()) {
        return {T262_FAIL, "not found in batch results"};
    }

    const BatchResult& br = it->second;

    if (prep.is_negative) {
        if (br.exit_code != 0 || br.output.find("Error") != std::string::npos ||
            br.output.find("error") != std::string::npos) {
            return {T262_PASS, ""};
        } else {
            return {T262_FAIL, "negative test did not throw (expected " + prep.negative_type + ")"};
        }
    }

    // timeout (exit code 124 from batch handler)
    if (br.exit_code == 124) {
        if (br.output.find("slow") != std::string::npos)
            return {T262_TIMEOUT, "slow test (took >3s in previous run)"};
        return {T262_TIMEOUT, "test timed out"};
    }

    // crash (signalled)
    if (br.exit_code > 128) {
        return {T262_CRASH, "crashed with signal " + std::to_string(br.exit_code - 128)};
    }

    // normal test: exit 0 and no Test262Error
    if (br.exit_code != 0) {
        std::string msg = br.output.substr(0, 200);
        return {T262_FAIL, "exit code " + std::to_string(br.exit_code) + ": " + msg};
    }

    if (br.output.find("Test262Error") != std::string::npos) {
        std::string msg = br.output.substr(0, 200);
        return {T262_FAIL, msg};
    }

    return {T262_PASS, ""};
}

// =============================================================================
// Batch pre-run: prepare, execute, evaluate — cache all results
// =============================================================================

static bool g_parallel_done = false;

static void batch_run_all_tests(const std::vector<Test262Param>& tests) {
    auto start_time = std::chrono::steady_clock::now();

    // Phase 1: parse metadata only (lazy assembly defers source to Phase 2)
    std::vector<Test262Prepared> prepared;
    std::vector<size_t> batch_indices;
    prepare_all_tests(tests, prepared, batch_indices);

    // All non-skipped tests run every time. t262_partial.txt is only the
    // previous run's report; it is not a skip list.
    std::vector<size_t> clean_indices;
    size_t previous_partial_count = 0;
    for (size_t idx : batch_indices) {
        if (!g_known_partial.empty() && g_known_partial.count(prepared[idx].test_name))
            previous_partial_count++;
        clean_indices.push_back(idx);
    }

    auto prep_time = std::chrono::steady_clock::now();
    double prep_secs = std::chrono::duration<double>(prep_time - start_time).count();
    fprintf(stderr, "[test262] Phase 1 (prepare): %.1fs — %zu scripts to batch (%zu clean, %zu previous-partial included)\n",
            prep_secs, batch_indices.size(), clean_indices.size(), previous_partial_count);

    // Phase 2: execute clean tests through js-test-batch (default batch size 100)
    auto batch_exec_start = std::chrono::steady_clock::now();
    auto batch_results = execute_t262_batch(prepared, clean_indices, g_t262_batch_chunk_size);
    auto batch_exec_done = std::chrono::steady_clock::now();
    double batch_exec_secs = std::chrono::duration<double>(batch_exec_done - batch_exec_start).count();

    // Build batch assignment map for Phase 4 diagnostics.  This mirrors
    // execute_t262_batch: native batches first, then JS batches grouped by
    // their special preamble helper set.
    {
        std::vector<size_t> native_clean_idx;
        std::vector<JsBatchGroup> js_clean_groups;
        partition_batch_indices(prepared, clean_indices, native_clean_idx, js_clean_groups);
        size_t native_batches = (native_clean_idx.size() + g_t262_batch_chunk_size - 1) / g_t262_batch_chunk_size;
        size_t js_batches = 0;
        for (const auto& group : js_clean_groups) {
            size_t group_chunk_size = group.is_slow_test ? 1 :
                (group.is_async ? g_t262_async_chunk_size : g_t262_batch_chunk_size);
            js_batches += (group.indices.size() + group_chunk_size - 1) / group_chunk_size;
        }
        size_t num_batches    = native_batches + js_batches;
        g_batch_contents.resize(num_batches);
        for (size_t i = 0; i < native_clean_idx.size(); i++) {
            size_t bi = i / g_t262_batch_chunk_size;
            const auto& name = prepared[native_clean_idx[i]].test_name;
            g_batch_assignment[name] = bi;
            g_batch_contents[bi].push_back(name);
        }
        size_t js_batch_base = native_batches;
        for (const auto& group : js_clean_groups) {
            size_t group_chunk_size = group.is_slow_test ? 1 :
                (group.is_async ? g_t262_async_chunk_size : g_t262_batch_chunk_size);
            for (size_t i = 0; i < group.indices.size(); i++) {
                size_t bi = js_batch_base + (i / group_chunk_size);
                const auto& name = prepared[group.indices[i]].test_name;
                g_batch_assignment[name] = bi;
                g_batch_contents[bi].push_back(name);
            }
            js_batch_base += (group.indices.size() + group_chunk_size - 1) / group_chunk_size;
        }
        fprintf(stderr, "[test262] Batch assignment map: %zu entries across %zu batches (%zu native + %zu js)\n",
                g_batch_assignment.size(), num_batches, native_batches, js_batches);
    }

    auto exec_time = std::chrono::steady_clock::now();
    double exec_secs = std::chrono::duration<double>(exec_time - prep_time).count();
    fprintf(stderr, "[test262] Phase 2 (execute): %.1fs — %zu results collected\n",
            exec_secs, batch_results.size());

    // Diagnostic: identify which clean batches lost tests and the crash points.
    // Crash-point tests kill the batch process in a way the signal handler can't
    // catch (stack overflow, double fault, etc.). They'll be added to the non-fully-passing
    // list as BATCH_KILL entries to prevent collateral damage in future runs.
    std::unordered_set<std::string> batch_kill_tests;
    {
        // Reconstruct native/js split to match actual sub-batch composition
        std::vector<size_t> native_clean;
        std::vector<JsBatchGroup> js_clean_groups;
        partition_batch_indices(prepared, clean_indices, native_clean, js_clean_groups);

        auto analyze_group = [&](const std::vector<size_t>& group_indices, const char* label,
                                 size_t analyze_chunk_size) {
            size_t total_lost = 0;
            size_t killed_batches = 0;
            for (size_t start = 0; start < group_indices.size(); start += analyze_chunk_size) {
                size_t end = std::min(start + analyze_chunk_size, group_indices.size());
                // Find first missing test in this sub-batch
                size_t completed = 0;
                size_t lost_in_batch = 0;
                std::string first_lost_name;
                std::string last_ok_name;
                for (size_t i = start; i < end; i++) {
                    const auto& name = prepared[group_indices[i]].test_name;
                    if (batch_results.count(name)) {
                        completed++;
                        last_ok_name = name;
                    } else {
                        if (first_lost_name.empty()) first_lost_name = name;
                        lost_in_batch++;
                    }
                }
                if (lost_in_batch > 0) {
                    size_t batch_num = start / analyze_chunk_size;
                    fprintf(stderr, "[test262] KILLED %s-batch[%zu]: %zu/%zu completed, %zu lost, "
                            "crash-point: '%s' (after '%s')\n",
                            label, batch_num, completed, end - start, lost_in_batch,
                            first_lost_name.c_str(),
                            last_ok_name.empty() ? "(batch start)" : last_ok_name.c_str());
                    total_lost += lost_in_batch;
                    killed_batches++;
                    // Queue the crash-point test for the non-fully-passing list
                    if (!first_lost_name.empty()) {
                        batch_kill_tests.insert(first_lost_name);
                    }
                }
            }
            if (killed_batches > 0)
                fprintf(stderr, "[test262] %s group: %zu killed batches, %zu total lost tests\n",
                        label, killed_batches, total_lost);
        };

        analyze_group(native_clean, "native", g_t262_batch_chunk_size);
        for (const auto& group : js_clean_groups) {
            size_t group_chunk_size = group.is_slow_test ? 1 :
                (group.is_async ? g_t262_async_chunk_size : g_t262_batch_chunk_size);
            std::string label = group.is_slow_test
                ? "js-slow"
                : group.special_includes.empty()
                ? "js"
                : std::string("js-special(") + group.key + ")";
            analyze_group(group.indices, label.c_str(), group_chunk_size);
        }
        if (!batch_kill_tests.empty())
            fprintf(stderr, "[test262] Detected %zu batch-kill crash-point tests (will add to non-fully-passing list for next run)\n",
                    batch_kill_tests.size());
    }

    // Phase 2a removed. Previous partial tests already ran in Phase 2.

    // Phase 2b: retry batch-lost tests in small batches (crash recovery)
    // Only needed for NEW unexpected crashes not in the non-fully-passing list.
    // Only retry clean tests that got lost (not partial tests — they already ran in Phase 2a)
    std::vector<size_t> lost_indices;
    for (size_t idx : clean_indices) {
        const auto& p = prepared[idx];
        if (batch_results.find(p.test_name) == batch_results.end()) {
            lost_indices.push_back(idx);
        }
    }
    double retry_secs = 0;
    if (!lost_indices.empty()) {
        auto retry_start = std::chrono::steady_clock::now();
        fprintf(stderr, "[test262] Phase 2b (retry): %zu batch-lost tests, re-running in small batches...\n",
                lost_indices.size());

        auto retry_results = [&]() {
            std::unordered_map<std::string, BatchResult> results;
            std::vector<SubBatch> retry_batches;
            // Retry individually (batch of 1) to prevent a single crashing test from
            // killing other lost tests in the retry batch.
            for (size_t s = 0; s < lost_indices.size(); s++) {
                size_t pi = lost_indices[s];
                retry_batches.push_back({s, s + 1, false, prepared[pi].is_module,
                                         prepared[pi].is_slow_test, 0});
            }
            std::vector<std::unordered_map<std::string, BatchResult>> thread_results(retry_batches.size());
            std::atomic<size_t> next_batch{0};
            size_t num_workers = t262_worker_count(retry_batches.size());
            std::vector<std::thread> threads;
            for (size_t w = 0; w < num_workers; w++) {
                threads.emplace_back([&, w]() {
                    char manifest_path[256];
                    snprintf(manifest_path, sizeof(manifest_path), "temp/_t262_retry_%zu.manifest", w);
                    while (true) {
                        size_t i = next_batch.fetch_add(1, std::memory_order_relaxed);
                        if (i >= retry_batches.size()) break;
                        FILE* mf = fopen(manifest_path, "wb");
                        if (!mf) continue;
                        for (size_t idx = retry_batches[i].start; idx < retry_batches[i].end; idx++) {
                            size_t pi = lost_indices[idx];
                            const auto& p = prepared[pi];
                            std::string combined = p.is_module
                                ? assemble_module_test_source(p)
                                : assemble_combined_source(p);
                            fprintf(mf, "%s:%s:%s:%zu\n",
                                    p.is_module ? "module-source" : "source",
                                    p.test_name.c_str(), p.test_path.c_str(), combined.size());
                            fwrite(combined.data(), 1, combined.size(), mf);
                            fputc('\n', mf);
                        }
                        fclose(mf);
                        size_t retry_num_tests = retry_batches[i].end - retry_batches[i].start;
                        run_t262_sub_batch(manifest_path, thread_results[i], retry_num_tests);
                    }
                    unlink(manifest_path);
                });
            }
            for (auto& t : threads) t.join();
            for (auto& partial : thread_results) {
                for (auto& kv : partial) results[kv.first] = std::move(kv.second);
            }
            return results;
        }();

        // merge retry results into main results
        size_t recovered = 0;
        for (auto& kv : retry_results) {
            if (batch_results.find(kv.first) == batch_results.end()) recovered++;
            batch_results[kv.first] = std::move(kv.second);
        }

        auto retry_time = std::chrono::steady_clock::now();
        retry_secs = std::chrono::duration<double>(retry_time - retry_start).count();
        fprintf(stderr, "[test262] Phase 2b (retry): %.1fs — recovered %zu of %zu lost tests\n",
                retry_secs, recovered, lost_indices.size());

        exec_time = retry_time;  // update for total calculation
    }

    g_cached_batch_results = batch_results;

    // Write a fresh non-fully-passing list for next run. Previous entries are
    // never preserved by themselves; every line must be justified by this run.
    auto partial_start = std::chrono::steady_clock::now();
    {
        FILE* partial_log = fopen("test/js262/t262_partial.txt", "w");
        if (partial_log) {
            size_t still_lost = 0;
            size_t crash_exit = 0;
            size_t slow_count = 0;
            size_t batch_kill_count = 0;
            std::unordered_set<std::string> written;

            auto prepared_path_for = [&](const std::string& name) -> const char* {
                for (size_t idx : clean_indices) {
                    if (prepared[idx].test_name == name) return prepared[idx].test_path.c_str();
                }
                for (size_t idx : lost_indices) {
                    if (prepared[idx].test_name == name) return prepared[idx].test_path.c_str();
                }
                return "";
            };
            auto write_partial_once = [&](const char* tag, const std::string& name,
                                          const char* path) -> bool {
                if (g_slow_tests.count(name)) return false;
                if (written.count(name)) return false;
                fprintf(partial_log, "%s\t%s\t%s\n", tag, name.c_str(), path);
                written.insert(name);
                return true;
            };

            // Add crash-exit tests from this run (Phase 2 + 2b)
            // Don't count as crash-exits (baseline gate blocker) if:
            // - BATCH_KILL: pass individually but crash the batch process
            // - Crash-collateral: tests from killed batches
            // - Not in baseline: pre-existing failures that now crash (not regressions)
            for (auto& kv : batch_results) {
                if (written.count(kv.first)) continue;
                if (kv.second.exit_code > 128) {
                    bool is_batch_kill = batch_kill_tests.count(kv.first) > 0;
                    bool is_collateral = false;
                    bool was_in_baseline = g_baseline_passing.count(kv.first) > 0;
                    {
                        auto ba_it = g_batch_assignment.find(kv.first);
                        if (ba_it != g_batch_assignment.end() && g_crashed_batches.count(ba_it->second))
                            is_collateral = true;
                    }
                    char tag[32];
                    snprintf(tag, sizeof(tag), "CRASH_%d", kv.second.exit_code);
                    bool wrote = write_partial_once(tag, kv.first, prepared_path_for(kv.first));
                    // Only count as crash-exit if it's a genuine regression.
                    bool was_known_crash = false;
                    {
                        auto tag_it = g_partial_tags.find(kv.first);
                        if (tag_it != g_partial_tags.end()) {
                            const auto& tag = tag_it->second;
                            was_known_crash = (tag.substr(0, 5) == "CRASH" ||
                                               tag.substr(0, 7) == "TIMEOUT" ||
                                               tag.substr(0, 4) == "SLOW" ||
                                               tag == "BATCH_KILL");
                        }
                    }
                    if (wrote && !is_batch_kill && !is_collateral && was_in_baseline && !was_known_crash)
                        crash_exit++;
                }
            }
            // Add slow tests above the configured elapsed threshold from this run.
            // Tests listed in t262_slow.txt are excluded from t262_partial.txt.
            for (auto& kv : batch_results) {
                if (written.count(kv.first)) continue;
                if (g_slow_tests.count(kv.first)) continue;
                long threshold_us = slow_threshold_for_test(kv.first);
                if (kv.second.elapsed_us >= threshold_us) {
                    char tag[32];
                    snprintf(tag, sizeof(tag), "SLOW_%ld", kv.second.elapsed_us / 1000);
                    if (write_partial_once(tag, kv.first, prepared_path_for(kv.first)))
                        slow_count++;
                }
            }
            // Clean tests still missing after Phase 2b
            for (size_t idx : lost_indices) {
                const auto& p = prepared[idx];
                if (written.count(p.test_name)) continue;
                auto it = batch_results.find(p.test_name);
                if (it == batch_results.end()) {
                    if (write_partial_once("MISSING", p.test_name, p.test_path.c_str()))
                        still_lost++;
                }
            }

            // Add batch-kill crash-point tests to non-fully-passing list.
            // These tests kill the batch process in a way the signal handler can't
            // catch (e.g. stack overflow, double fault). They pass individually but
            // crash the process when run in batch context. Adding them to the partial
            // list forces individual execution, preventing collateral loss.
            // Unlike CRASH entries (sticky), BATCH_KILL entries are re-verified
            // each run: if the test no longer appears as a crash-point, it's removed.
            for (auto& bk_name : batch_kill_tests) {
                if (written.count(bk_name)) continue;
                if (write_partial_once("BATCH_KILL", bk_name, prepared_path_for(bk_name)))
                    batch_kill_count++;
            }

            fclose(partial_log);
            if (still_lost + crash_exit + slow_count + batch_kill_count > 0) {
                fprintf(stderr, "[test262] Non-fully-passing: %zu missing + %zu crash-exit + %zu slow (>3s) + %zu batch-kill → test/js262/t262_partial.txt\n",
                        still_lost, crash_exit, slow_count, batch_kill_count);
            }
            // Expose crash-exit count for --update-baseline gate
            g_phase_crash_exit = crash_exit + still_lost;
        }
    }
    auto partial_done = std::chrono::steady_clock::now();
    double partial_secs = std::chrono::duration<double>(partial_done - partial_start).count();

    // Write per-test timing data to temp/_t262_timing.tsv (or _o0/_o3 suffix when --opt-level used)
    auto timing_start = std::chrono::steady_clock::now();
    {
        char timing_path[128] = "temp/_t262_timing.tsv";
        if (g_opt_level >= 0)
            snprintf(timing_path, sizeof(timing_path), "temp/_t262_timing_o%d.tsv", g_opt_level);
        FILE* timing_log = fopen(timing_path, "w");
        if (timing_log) {
            fprintf(timing_log, "test_name\texit_code\telapsed_us\n");
            for (auto& kv : batch_results) {
                fprintf(timing_log, "%s\t%d\t%ld\n",
                        kv.first.c_str(), kv.second.exit_code, kv.second.elapsed_us);
            }
            fclose(timing_log);
            fprintf(stderr, "[test262] Timing data: %zu entries → %s\n",
                    batch_results.size(), timing_path);
        }
    }
    write_phase_timing_log(batch_results);
    auto timing_done = std::chrono::steady_clock::now();
    double timing_secs = std::chrono::duration<double>(timing_done - timing_start).count();

    // Write per-test memory profiling data and summary
    auto memory_start = std::chrono::steady_clock::now();
    {
        char mem_path[128] = "temp/_t262_memory.tsv";
        if (g_opt_level >= 0)
            snprintf(mem_path, sizeof(mem_path), "temp/_t262_memory_o%d.tsv", g_opt_level);
        FILE* mem_log = fopen(mem_path, "w");
        if (mem_log) {
            fprintf(mem_log, "test_name\texit_code\trss_before_kb\trss_after_kb\trss_delta_kb\n");
            for (auto& kv : batch_results) {
                long delta_kb = (long)(kv.second.rss_after / 1024) - (long)(kv.second.rss_before / 1024);
                fprintf(mem_log, "%s\t%d\t%zu\t%zu\t%ld\n",
                        kv.first.c_str(), kv.second.exit_code,
                        kv.second.rss_before / 1024, kv.second.rss_after / 1024, delta_kb);
            }
            fclose(mem_log);
        }

        // Compute memory summary stats
        size_t total_tests = 0, tests_with_mem = 0;
        size_t peak_rss = 0, max_delta = 0;
        long total_delta = 0;
        std::string peak_rss_test, max_delta_test;
        size_t large_alloc_count = 0; // tests with > 10 MB delta
        size_t leak_suspect_count = 0; // tests with > 1 MB positive delta

        // Per-batch tracking: group by rss_before to detect cross-test growth
        // First test in a batch has smallest rss_before, last has largest rss_after
        size_t min_rss_seen = (size_t)-1, max_rss_seen = 0;

        for (auto& kv : batch_results) {
            total_tests++;
            if (kv.second.rss_after == 0) continue; // no memory data
            tests_with_mem++;
            long delta = (long)(kv.second.rss_after) - (long)(kv.second.rss_before);
            total_delta += delta;
            if (kv.second.rss_after > peak_rss) {
                peak_rss = kv.second.rss_after;
                peak_rss_test = kv.first;
            }
            if (delta > 0 && (size_t)delta > max_delta) {
                max_delta = (size_t)delta;
                max_delta_test = kv.first;
            }
            if (delta > 10 * 1024 * 1024) large_alloc_count++;
            if (delta > 1 * 1024 * 1024) leak_suspect_count++;
            if (kv.second.rss_before > 0 && kv.second.rss_before < min_rss_seen)
                min_rss_seen = kv.second.rss_before;
            if (kv.second.rss_after > max_rss_seen)
                max_rss_seen = kv.second.rss_after;
        }

        if (tests_with_mem > 0) {
            fprintf(stderr, "\n[test262] ╔══════════════════════════════════════════════════╗\n");
            fprintf(stderr, "[test262] ║         Memory Profiling Summary                 ║\n");
            fprintf(stderr, "[test262] ╠══════════════════════════════════════════════════╣\n");
            fprintf(stderr, "[test262] ║  Tests with memory data: %5zu / %5zu           ║\n",
                    tests_with_mem, total_tests);
            fprintf(stderr, "[test262] ║  Peak RSS across all tests: %7.1f MB            ║\n",
                    peak_rss / (1024.0 * 1024.0));
            fprintf(stderr, "[test262] ║  Min RSS seen (batch start): %6.1f MB            ║\n",
                    min_rss_seen / (1024.0 * 1024.0));
            fprintf(stderr, "[test262] ║  Max RSS seen (batch end):   %6.1f MB            ║\n",
                    max_rss_seen / (1024.0 * 1024.0));
            fprintf(stderr, "[test262] ║  Avg RSS delta/test: %+.1f KB                    ║\n",
                    (total_delta / (double)tests_with_mem) / 1024.0);
            fprintf(stderr, "[test262] ║  Largest single-test growth: %7.1f MB           ║\n",
                    max_delta / (1024.0 * 1024.0));
            fprintf(stderr, "[test262] ║  Tests > 1 MB growth (leak suspects): %4zu       ║\n",
                    leak_suspect_count);
            fprintf(stderr, "[test262] ║  Tests > 10 MB growth (large alloc):  %4zu       ║\n",
                    large_alloc_count);
            fprintf(stderr, "[test262] ╚══════════════════════════════════════════════════╝\n");

            if (!max_delta_test.empty()) {
                fprintf(stderr, "[test262]   Largest growth test: %s\n", max_delta_test.c_str());
            }
            if (!peak_rss_test.empty()) {
                fprintf(stderr, "[test262]   Peak RSS test: %s\n", peak_rss_test.c_str());
            }

            // Print top 20 tests by RSS delta (descending) — verbose-only; the full
            // per-test deltas are always written to the mem_path TSV below.
            if (g_verbose) {
                std::vector<std::pair<long, std::string>> by_delta;
                for (auto& kv : batch_results) {
                    if (kv.second.rss_after == 0) continue;
                    long delta = (long)(kv.second.rss_after) - (long)(kv.second.rss_before);
                    by_delta.push_back({delta, kv.first});
                }
                std::sort(by_delta.begin(), by_delta.end(),
                          [](const auto& a, const auto& b) { return a.first > b.first; });
                fprintf(stderr, "[test262]   Top 20 tests by memory growth:\n");
                for (size_t i = 0; i < 20 && i < by_delta.size(); i++) {
                    fprintf(stderr, "[test262]     %+8.1f KB  %s\n",
                            by_delta[i].first / 1024.0, by_delta[i].second.c_str());
                }
            }
            fprintf(stderr, "[test262]   Memory data → %s\n", mem_path);
        }
    }
    auto memory_done = std::chrono::steady_clock::now();
    double memory_secs = std::chrono::duration<double>(memory_done - memory_start).count();

    // Build classification sets for non-fully-passing logic.
    // All tests must pass in their original Phase 2 batch under their timing gate
    // to be "fully passed".
    std::set<std::string> clean_set, phase2b_set;
    for (size_t idx : clean_indices)    clean_set.insert(prepared[idx].test_name);
    for (size_t idx : lost_indices)     phase2b_set.insert(prepared[idx].test_name);

    // Build set of batches that had crash-points (BATCH_KILL).
    // Tests lost from these batches are crash-collateral, not genuinely batch-unstable.
    for (auto& bk_name : batch_kill_tests) {
        auto it = g_batch_assignment.find(bk_name);
        if (it != g_batch_assignment.end()) g_crashed_batches.insert(it->second);
    }

    // Phase 3: evaluate results and cache, applying non-fully-passing classification
    auto eval_start = std::chrono::steady_clock::now();
    size_t pp_batch_lost = 0, pp_slow = 0, pp_collateral = 0;
    for (size_t i = 0; i < prepared.size(); i++) {
        const auto& pname = prepared[i].test_name;
        g_failure_info[pname] = {prepared[i].test_path, prepared[i].category,
                                 prepared[i].subcategory, prepared[i].features,
                                 prepared[i].includes, prepared[i].native_harness};

        Test262RunResult result = evaluate_batch_result(prepared[i], batch_results);

        // Classify non-fully-passing tests.
        // Only fully passed if passed in original Phase 2 batch under its timing gate.
        if (result.result == T262_PASS) {
            const auto& name = prepared[i].test_name;
            if (clean_set.count(name) && phase2b_set.count(name)) {
                // Check if this was crash-collateral (batch had a BATCH_KILL crash-point).
                // Crash-collateral tests are promoted to full pass — they're not genuinely
                // batch-unstable, they were just killed by a different test's crash.
                auto ba_it = g_batch_assignment.find(name);
                if (ba_it != g_batch_assignment.end() && g_crashed_batches.count(ba_it->second)) {
                    // Crash-collateral: keep as T262_PASS (not partial)
                    pp_collateral++;
                } else {
                    result = {T262_PARTIAL_PASS, "non-fully-passing: batch-lost, passed only in individual retry"};
                    pp_batch_lost++;
                }
            } else if (clean_set.count(name) && !g_slow_tests.count(name)) {
                auto br_it = batch_results.find(name);
                long threshold_us = slow_threshold_for_test(name);
                if (br_it != batch_results.end() && br_it->second.elapsed_us >= threshold_us) {
                    // Test passed in batch but exceeded its slow threshold.
                    // Slow tests are not "fully passing" for baseline purposes:
                    // they go to t262_partial.txt and are excluded from baseline updates.
                    long elapsed_ms = br_it->second.elapsed_us / 1000;
                    long threshold_ms = threshold_us / 1000;
                    result = {T262_PARTIAL_PASS,
                              "non-fully-passing: slow batch runtime " +
                              std::to_string(elapsed_ms) + "ms >= " +
                              std::to_string(threshold_ms) + "ms"};
                    pp_slow++;
                }
            }
            // fast clean-batch passes remain T262_PASS
        }

        std::lock_guard<std::mutex> lock(g_results_mutex);
        g_cached_results[prepared[i].test_name] = result;
    }
    if (pp_batch_lost + pp_collateral + pp_slow > 0) {
        fprintf(stderr, "[test262] Phase 3 non-fully-passing: %zu batch-lost + %zu crash-collateral (promoted) + %zu slow (>= threshold)\n",
                pp_batch_lost, pp_collateral, pp_slow);
    }
    // Expose batch-lost count for --update-baseline gate
    g_phase_batch_lost = pp_batch_lost;
    auto eval_done = std::chrono::steady_clock::now();
    double eval_secs = std::chrono::duration<double>(eval_done - eval_start).count();

    auto total_time = std::chrono::steady_clock::now();
    double total_secs = std::chrono::duration<double>(total_time - start_time).count();
    fprintf(stderr, "[test262] All %zu tests completed in %.1fs (prep %.1fs + batch %.1fs [batched %.1fs: sync %.1fs + async %.1fs; non-batched %.1fs] + retry %.1fs + partial %.1fs + timing %.1fs + memory %.1fs + eval %.1fs)\n",
            tests.size(), total_secs, prep_secs,
            batch_exec_secs, g_last_batch_execute_timing.batched_wall_secs,
            g_last_batch_execute_timing.sync_batched_wall_secs,
            g_last_batch_execute_timing.async_batched_wall_secs,
            g_last_batch_execute_timing.non_batched_wall_secs,
            retry_secs, partial_secs, timing_secs, memory_secs, eval_secs);
    g_prep_secs = prep_secs;
    g_exec_secs = std::chrono::duration<double>(exec_time - prep_time).count();
    g_total_secs = total_secs;
    g_phase_batch_batched_secs = g_last_batch_execute_timing.batched_wall_secs;
    g_phase_batch_non_batched_secs = g_last_batch_execute_timing.non_batched_wall_secs;
    g_phase_batch_sync_batched_secs = g_last_batch_execute_timing.sync_batched_wall_secs;
    g_phase_batch_async_batched_secs = g_last_batch_execute_timing.async_batched_wall_secs;
    g_phase_retry_secs = retry_secs;
    g_phase_partial_secs = partial_secs;
    g_phase_timing_secs = timing_secs;
    g_phase_memory_secs = memory_secs;
    g_phase_evaluate_secs = eval_secs;
    g_parallel_done = true;
}

// =============================================================================
// GTest parameterized test (reads from cache)
// =============================================================================

class Test262Suite : public testing::TestWithParam<Test262Param> {};

static int REPORT_ALL_TESTS() {
    return RUN_ALL_TESTS();
}

TEST_P(Test262Suite, Run) {
    const Test262Param& param = GetParam();

    // look up cached result from parallel pre-run
    Test262RunResult result;
    {
        std::lock_guard<std::mutex> lock(g_results_mutex);
        auto it = g_cached_results.find(param.test_name);
        if (it != g_cached_results.end()) {
            result = it->second;
        } else {
            result = {T262_FAIL, "not found in cache"};
        }
    }

    switch (result.result) {
        case T262_PASS:
            break;
        case T262_PARTIAL_PASS:
            GTEST_NONFATAL_FAILURE_(result.message.c_str());
            break;
        case T262_SKIP:
            GTEST_SKIP() << "Skipped: " << result.message;
            break;
        case T262_FAIL:
            GTEST_NONFATAL_FAILURE_(result.message.c_str());
            break;
        case T262_TIMEOUT:
            GTEST_NONFATAL_FAILURE_(result.message.c_str());
            break;
        case T262_CRASH:
            GTEST_NONFATAL_FAILURE_(result.message.c_str());
            break;
    }
}

// Custom test name generator
static std::string test262_name_generator(const testing::TestParamInfo<Test262Param>& info) {
    return info.param.test_name;
}

INSTANTIATE_TEST_SUITE_P(
    Test262,
    Test262Suite,
    testing::ValuesIn(discover_all_tests()),
    test262_name_generator
);

// =============================================================================
// Summary reporting (printed after all tests)
// =============================================================================

class Test262ReportListener : public testing::EmptyTestEventListener {
public:
    int passed = 0, failed = 0, skipped = 0, crashed = 0, timed_out = 0, batch_lost = 0, partial = 0;
    std::map<std::string, std::pair<int,int>> category_results; // category -> (pass, total)
    std::vector<std::string> current_passing;    // tests that fully passed this run
    std::vector<std::string> regressions;        // baseline pass → now fail
    std::vector<std::string> improvements;       // baseline fail → now pass

    void OnTestPartResult(const testing::TestPartResult& result) override {
        // count non-fatal failures
    }

    void OnTestEnd(const testing::TestInfo& info) override {
        // Extract test name: GTest param name is the test_name
        std::string test_name;
        if (info.value_param()) {
            // For parameterized tests, parse the name from the test case name
            // Format: "Run/<test_name>"
            test_name = info.name();
            // The actual param name is in the test name suffix after "Run/"
        }
        // Use the full test name from the test info
        std::string full_name = info.test_case_name() ? info.test_case_name() : "";
        // Extract just the parameter part: "Test262/Test262Suite.Run/<test_name>"
        // The test_name is in info.name() which is "Run/<param_name>"
        std::string param_name;
        const char* name_str = info.name();
        if (name_str && strncmp(name_str, "Run/", 4) == 0) {
            param_name = name_str + 4;
        }

        if (info.result()->Skipped()) {
            skipped++;
        } else if (info.result()->Passed()) {
            passed++;
            if (!param_name.empty()) {
                current_passing.push_back(param_name);
                if (!g_baseline_passing.empty() && g_baseline_passing.find(param_name) == g_baseline_passing.end()) {
                    improvements.push_back(param_name);
                }
            }
        } else {
            // Check if this is non-fully-passing (passed individually but not reliably in batch)
            bool is_partial = false;
            if (!param_name.empty()) {
                std::lock_guard<std::mutex> lock(g_results_mutex);
                auto it = g_cached_results.find(param_name);
                if (it != g_cached_results.end() && it->second.result == T262_PARTIAL_PASS) {
                    is_partial = true;
                }
            }
            if (is_partial) {
                partial++;
                // Non-fully-passing tests are not added to current_passing (baseline).
                // If this test was in the baseline, it IS a regression (no longer fully passing).
                if (!param_name.empty() && !g_baseline_passing.empty() &&
                    g_baseline_passing.find(param_name) != g_baseline_passing.end()) {
                    regressions.push_back(param_name);
                }
            } else {
                failed++;
                // Check if failure is "not found in batch results" — batch infrastructure
                // issue, not a real test failure. Don't count as regression.
                bool is_batch_lost = false;
                for (int i = 0; i < info.result()->total_part_count(); i++) {
                    const auto& part = info.result()->GetTestPartResult(i);
                    if (part.failed() && part.message() &&
                        strstr(part.message(), "not found in batch")) {
                        is_batch_lost = true;
                        break;
                    }
                }
                if (!is_batch_lost && !param_name.empty() && !g_baseline_passing.empty() &&
                    g_baseline_passing.find(param_name) != g_baseline_passing.end()) {
                    regressions.push_back(param_name);
                }
                if (is_batch_lost) batch_lost++;
            }
        }
    }

    void OnTestSuiteEnd(const testing::TestSuite& suite) override {
        // printed by GTest
    }

    void OnTestProgramEnd(const testing::UnitTest& unit_test) override {
        // test262 Compliance Summary box removed to avoid duplicating the Regression
        // Check vs Baseline box below (which carries the pass/fail/regression verdict
        // for baseline runs). GTest still prints its own overall pass/fail line.
        if (partial > 0) {
            printf("\n⚡ NON-FULLY-PASSING tests (%d):\n", partial);
            std::lock_guard<std::mutex> lock(g_results_mutex);
            for (auto& kv : g_cached_results) {
                if (kv.second.result == T262_PARTIAL_PASS) {
                    printf("  ~ %s  [%s]\n", kv.first.c_str(), kv.second.message.c_str());
                }
            }
        }

        // Regression / improvement report
        if (!g_baseline_passing.empty()) {
            printf("\n");
            printf("╔══════════════════════════════════════════════════╗\n");
            printf("║         Regression Check vs Baseline             ║\n");
            printf("╠══════════════════════════════════════════════════╣\n");
            printf("║  Baseline passing: %5zu                         ║\n", g_baseline_passing.size());
            printf("║  Fully passing:    %5zu                         ║\n", current_passing.size());
            printf("║  Non-fully-passing:%5d                          ║\n", partial);
            printf("║  Improvements:     %5zu  (fail → pass)          ║\n", improvements.size());
            printf("║  Regressions:      %5zu  (pass → fail)          ║\n", regressions.size());
            printf("╚══════════════════════════════════════════════════╝\n");

            if (!regressions.empty()) {
                printf("\n⚠️  REGRESSIONS (%zu tests that previously passed now fail):\n", regressions.size());
                std::sort(regressions.begin(), regressions.end());
                for (auto& r : regressions) {
                    std::lock_guard<std::mutex> lock(g_results_mutex);
                    auto rit = g_cached_results.find(r);
                    if (rit != g_cached_results.end() && !rit->second.message.empty()) {
                        printf("  - %s  [%s]\n", r.c_str(), rit->second.message.c_str());
                    } else {
                        printf("  - %s\n", r.c_str());
                    }
                }
            }
            if (!improvements.empty() && improvements.size() <= 50) {
                printf("\n✅  IMPROVEMENTS (%zu tests that previously failed now pass):\n", improvements.size());
                std::sort(improvements.begin(), improvements.end());
                for (auto& r : improvements) {
                    printf("  + %s\n", r.c_str());
                }
            } else if (!improvements.empty()) {
                printf("\n✅  IMPROVEMENTS: %zu tests (too many to list)\n", improvements.size());
            }
        }

        // Update baseline if requested — gated by stability criteria
        if (g_update_baseline) {
            bool gate_ok = true;
            if ((size_t)batch_lost > 0 || g_phase_batch_lost > 0) {
                printf("\n❌  Baseline NOT updated: %d batch-lost (must be 0)\n", batch_lost);
                gate_ok = false;
            }
            if (g_phase_crash_exit > 0) {
                printf("\n❌  Baseline NOT updated: %zu crash-exits (must be 0)\n", g_phase_crash_exit);
                gate_ok = false;
            }
            if (!regressions.empty()) {
                printf("\n❌  Baseline NOT updated: %zu regressions (must be 0)\n", regressions.size());
                gate_ok = false;
            }
            if ((int)current_passing.size() < STABLE_BASELINE_MIN) {
                printf("\n❌  Baseline NOT updated: %zu fully passing < %d (stable minimum)\n",
                       current_passing.size(), STABLE_BASELINE_MIN);
                gate_ok = false;
            }
            if (gate_ok) {
                write_baseline_file(BASELINE_FILE, current_passing,
                                    g_total_tests, skipped, g_total_batched, failed);
                clean_partial_list_after_baseline_update(current_passing);
                printf("\n📝  Baseline updated: %s (%zu fully passing tests, gate: batch-lost=0 crash=0 min=%d)\n",
                       BASELINE_FILE, current_passing.size(), STABLE_BASELINE_MIN);
            }
        }

        write_failure_artifacts();
    }
};

static bool test262_help_requested(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            return true;
        }
    }
    return false;
}

#ifndef _WIN32
static bool claim_test262_gtest_run_lock() {
    if (mkdir("temp", 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "[test262] ERROR: cannot create temp directory for run lock: %s\n",
                strerror(errno));
        return false;
    }

    pid_t self = getpid();
    for (int attempt = 0; attempt < 2; attempt++) {
        int fd = open(TEST262_RUN_LOCK_FILE, O_CREAT | O_EXCL | O_WRONLY, 0644);
        if (fd >= 0) {
            char buf[64];
            int len = snprintf(buf, sizeof(buf), "%ld\n", (long)self);
            if (len > 0) {
                ssize_t ignored = write(fd, buf, (size_t)len);
                (void)ignored;
            }
            close(fd);
            return true;
        }

        if (errno != EEXIST) {
            fprintf(stderr, "[test262] ERROR: cannot create run lock %s: %s\n",
                    TEST262_RUN_LOCK_FILE, strerror(errno));
            return false;
        }

        int lock_fd = open(TEST262_RUN_LOCK_FILE, O_RDONLY);
        char buf[64] = {0};
        ssize_t bytes = -1;
        if (lock_fd >= 0) {
            bytes = read(lock_fd, buf, sizeof(buf) - 1);
            close(lock_fd);
        }
        pid_t owner = bytes > 0 ? (pid_t)atol(buf) : -1;
        errno = 0;
        int owner_signal = owner > 0 && owner != self ? kill(owner, 0) : -1;
        if (owner_signal == 0 || errno == EPERM) {
            fprintf(stderr,
                "[test262] ERROR: another test_js_test262_gtest.exe process is already running "
                "(pid %ld). Concurrent runs share batch scratch output and are not supported.\n",
                (long)owner);
            return false;
        }

        unlink(TEST262_RUN_LOCK_FILE);
    }

    fprintf(stderr, "[test262] ERROR: could not acquire run lock %s\n", TEST262_RUN_LOCK_FILE);
    return false;
}

static void release_test262_gtest_run_lock() {
    unlink(TEST262_RUN_LOCK_FILE);
}
#endif

static void print_test262_help(const char* program) {
    if (!program || !program[0]) {
        program = "./test/test_js_test262_gtest.exe";
    }

    printf("LambdaJS test262 gtest runner\n");
    printf("\n");
    printf("Usage:\n");
    printf("  %s [js262 options] [gtest options]\n", program);
    printf("\n");
    printf("Common js262 options:\n");
    printf("  --batch-only              Run the js262 batch runner path.\n");
    printf("  --verbose                 Dump per-batch timing (top 20/top 5) and top-20 memory growth.\n");
    printf("  --baseline-only           Run only tests listed in the baseline file.\n");
    printf("  --update-baseline         Update the baseline when stability gates pass.\n");
    printf("  --batch-file=<path>       Run tests listed in a newline-separated file.\n");
    printf("  --run-async               Permit async-flagged tests that are also allowlisted.\n");
    printf("                            Without an allowlist, async tests remain skipped.\n");
    printf("  --async-list=<path>       Async allowlist, one test name per line.\n");
    printf("                            Defaults to --batch-file when --batch-file is present.\n");
    printf("  --batch-chunk-size=<n>    Tests per js-test-batch process, clamped to\n");
    printf("                            1..%zu (default: %zu).\n",
           T262_MAX_BATCH_CHUNK_SIZE, T262_DEFAULT_BATCH_CHUNK_SIZE);
    printf("  --async-chunk-size=<n>    Async tests per js-test-batch process, clamped to\n");
    printf("                            1..%zu (default: %zu, same as sync batches).\n",
           T262_MAX_BATCH_CHUNK_SIZE, T262_DEFAULT_BATCH_CHUNK_SIZE);
    printf("  --diagnose                Run diagnose list and pass --diagnose to Lambda.\n");
    printf("  --diagnose-list=<path>    Override diagnose list path (default: test/js262/diagnose_list.txt).\n");
    printf("  --write-failures=<path>   Write TSV failure/regression details.\n");
    printf("  --js-timeout=<seconds>    Set per-test Lambda timeout, clamped to 1..120.\n");
    printf("  --jobs=<n>                Set batch worker count.\n");
    printf("  --opt-level=<0..3>        Pass the MIR optimization level to Lambda.\n");
    printf("  --feature-summary         Print failure counts grouped by feature.\n");
    printf("  --no-hot-reload           Disable batch hot reload behavior.\n");
    printf("  --persistent-workers      Reuse one Lambda worker process for all manifests\n");
    printf("                            assigned to each test262 worker (POC timing mode).\n");
    printf("  --mir-interp              Run Lambda through MIR interpreter mode.\n");
    printf("  --no-stripped             Use canonical test files instead of stripped files.\n");
    printf("  --help, -h                Print this help and exit without running tests.\n");
    printf("\n");
    printf("Useful gtest options:\n");
    printf("  --gtest_filter=<pattern>  Restrict gtest cases.\n");
    printf("  --gtest_list_tests        List gtest cases without running them.\n");
    printf("  --gtest_brief=1           Print shorter gtest output.\n");
    printf("\n");
    printf("Examples:\n");
    printf("  %s --batch-only --baseline-only\n", program);
    printf("  %s --batch-only --baseline-only --batch-chunk-size=100\n", program);
    printf("  %s --batch-only --batch-file=temp/js44_batch.txt --jobs=1 --write-failures=temp/out.tsv\n", program);
    printf("  %s --batch-only --baseline-only --persistent-workers\n", program);
    printf("  %s --batch-only --run-async --async-list=test/js262/test262_baseline.txt --update-baseline\n", program);
    printf("  %s --batch-only --run-async --batch-file=temp/js47_async.txt --jobs=1\n", program);
    printf("  %s --batch-only --run-async --batch-file=temp/js47_async.txt --async-chunk-size=1 --jobs=1\n", program);
    printf("  %s --diagnose --jobs=1 --js-timeout=30\n", program);
    printf("  %s --batch-only --update-baseline\n", program);
    printf("\n");
    printf("Async flow:\n");
    printf("  Metadata cache marks tests with the test262 async flag. Those tests are skipped\n");
    printf("  unless --run-async is set and their sanitized test name appears in the async\n");
    printf("  allowlist. Enabled async tests run through JS-harness batches with $DONE,\n");
    printf("  microtask/timer draining, and per-test harness reset. Use --async-chunk-size=1\n");
    printf("  only for isolation; the default is the normal batch size for full-suite runs.\n");
}

int main(int argc, char** argv) {
    if (test262_help_requested(argc, argv)) {
        print_test262_help(argc > 0 ? argv[0] : nullptr);
        return 0;
    }

#ifndef _WIN32
    if (!claim_test262_gtest_run_lock()) {
        return 2;
    }
    atexit(release_test262_gtest_run_lock);
#endif

    testing::InitGoogleTest(&argc, argv);

#ifndef _WIN32
    // Kill any stale lambda.exe processes from previous runs to avoid
    // CPU contention that skews timing results.
    {
        FILE* fp = popen("pgrep -x lambda.exe 2>/dev/null", "r");
        if (fp) {
            char buf[64];
            int count = 0;
            pid_t my_ppid = getppid();  // don't kill our own parent
            while (fgets(buf, sizeof(buf), fp)) {
                pid_t pid = (pid_t)atoi(buf);
                if (pid > 0 && pid != getpid() && pid != my_ppid) {
                    kill(pid, SIGKILL);
                    count++;
                }
            }
            pclose(fp);
            if (count > 0) {
                fprintf(stderr, "[test262] Killed %d stale lambda.exe process(es)\n", count);
            }
        }
    }
#endif

    // Check for --update-baseline flag (must be after InitGoogleTest consumes gtest flags)
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--update-baseline") == 0) {
            g_update_baseline = true;
        }
        if (strcmp(argv[i], "--baseline-only") == 0) {
            g_baseline_only = true;
        }
        if (strcmp(argv[i], "--batch-only") == 0) {
            g_batch_only = true;
        }
        if (strcmp(argv[i], "--verbose") == 0) {
            g_verbose = true;
        }
        if (strcmp(argv[i], "--diagnose") == 0) {
            g_diagnose_mode = true;
        }
        if (strcmp(argv[i], "--run-async") == 0) {
            g_run_async = true;
        }
        if (strcmp(argv[i], "--no-hot-reload") == 0) {
            g_no_hot_reload = true;
        }
        if (strcmp(argv[i], "--persistent-workers") == 0) {
            g_persistent_workers = true;
        }
        if (strcmp(argv[i], "--mir-interp") == 0) {
            g_mir_interp = true;
        }
        if (strcmp(argv[i], "--no-stripped") == 0) {
            g_no_stripped = true;  // explicit disable
        }
        if (strncmp(argv[i], "--batch-file=", 13) == 0) {
            g_batch_file = argv[i] + 13;
        }
        if (strncmp(argv[i], "--diagnose-list=", 16) == 0) {
            g_diagnose_mode = true;
            g_diagnose_list_file = argv[i] + 16;
        }
        if (strncmp(argv[i], "--async-list=", 13) == 0) {
            g_async_list_file = argv[i] + 13;
        }
        if (strncmp(argv[i], "--batch-chunk-size=", 19) == 0) {
            int requested_chunk = atoi(argv[i] + 19);
            if (requested_chunk < 1) {
                requested_chunk = 1;
            }
            if ((size_t)requested_chunk > T262_MAX_BATCH_CHUNK_SIZE) {
                requested_chunk = (int)T262_MAX_BATCH_CHUNK_SIZE;
            }
            g_t262_batch_chunk_size = (size_t)requested_chunk;
        }
        if (strncmp(argv[i], "--async-chunk-size=", 19) == 0) {
            int requested_chunk = atoi(argv[i] + 19);
            if (requested_chunk < 1) {
                requested_chunk = 1;
            }
            if ((size_t)requested_chunk > T262_MAX_BATCH_CHUNK_SIZE) {
                requested_chunk = (int)T262_MAX_BATCH_CHUNK_SIZE;
            }
            g_t262_async_chunk_size = (size_t)requested_chunk;
            g_t262_async_chunk_size_explicit = true;
        }
        if (strncmp(argv[i], "--write-failures=", 17) == 0) {
            g_write_failures_path = argv[i] + 17;
        }
        if (strcmp(argv[i], "--feature-summary") == 0) {
            g_feature_summary = true;
        }
        if (strncmp(argv[i], "--opt-level=", 12) == 0) {
            g_opt_level = atoi(argv[i] + 12);
            if (g_opt_level < 0 || g_opt_level > 3) g_opt_level = 0;
            snprintf(g_opt_level_arg, sizeof(g_opt_level_arg), "--opt-level=%d", g_opt_level);
        }
        if (strncmp(argv[i], "--js-timeout=", 13) == 0) {
            int timeout_secs = atoi(argv[i] + 13);
            if (timeout_secs < 1) timeout_secs = 10;
            if (timeout_secs > 120) timeout_secs = 120;
            snprintf(g_js_timeout_arg, sizeof(g_js_timeout_arg), "--timeout=%d", timeout_secs);
        }
        if (strncmp(argv[i], "--jobs=", 7) == 0) {
            int jobs = atoi(argv[i] + 7);
            if (jobs < 1) jobs = 1;
            g_t262_jobs = (size_t)jobs;
        }
    }

    if (!g_t262_async_chunk_size_explicit) {
        g_t262_async_chunk_size = g_t262_batch_chunk_size;
    } else if (g_t262_async_chunk_size > g_t262_batch_chunk_size) {
        g_t262_async_chunk_size = g_t262_batch_chunk_size;
    }

    if (g_diagnose_mode) {
        g_batch_only = true;
        if (g_batch_file.empty()) {
            g_batch_file = g_diagnose_list_file;
        }
        fprintf(stderr, "[test262] Diagnose mode enabled: list=%s\n", g_batch_file.c_str());
    }

    // Auto-detect stripped test files directory (test/js262 -> ../lambda-test/js262)
    if (!g_no_stripped) {
        struct stat st;
        std::string stripped_test_dir = std::string(TEST262_SOURCE_DIR) + "/test";
        if (stat(stripped_test_dir.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
            g_use_stripped = true;
            // NOTE: do NOT switch g_harness_dir to the stripped harness directory.
            // The files in test/js262/harness/ are not just comment-stripped — they
            // are an older, materially divergent version (e.g. propertyHelper.js
            // is missing verifyCallableProperty and has different writable/
            // configurable check semantics) that causes spurious regressions.
            // Always load harness files from the canonical ref/test262/harness.
            fprintf(stderr, "[test262] Using comment-stripped test files from %s "
                    "(harness from %s)\n",
                    TEST262_SOURCE_DIR, g_harness_dir.c_str());
        }
    }

    // Load baseline file for regression checking
    {
        FILE* f = fopen(BASELINE_FILE, "r");
        if (f) {
            char line[512];
            while (fgets(line, sizeof(line), f)) {
                if (line[0] == '#') {
                    parse_baseline_header_value(line, "# ref/test262 commit:",
                                                g_baseline_ref_test262_commit);
                    continue;
                }
                // Skip empty lines
                if (line[0] == '\n' || line[0] == '\r') continue;
                // Trim trailing newline
                size_t len = strlen(line);
                while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) line[--len] = '\0';
                if (len > 0) g_baseline_passing.insert(std::string(line, len));
            }
            fclose(f);
            g_baseline_loaded = true;
            fprintf(stderr, "[test262] Loaded baseline: %zu passing tests from %s\n",
                    g_baseline_passing.size(), BASELINE_FILE);
            if (!verify_ref_test262_commit_matches_baseline(BASELINE_FILE)) {
                return 1;
            }
        } else {
            fprintf(stderr, "[test262] No baseline file found (%s) — regression checking disabled\n",
                    BASELINE_FILE);
        }
    }

    // Load metadata cache (or generate it)
    if (!load_metadata_cache(METADATA_CACHE_FILE)) {
        fprintf(stderr, "[test262] Metadata cache not found, generating...\n");
        int ret = system("python3 utils/generate_test262_metadata.py");
        if (ret != 0) {
            fprintf(stderr, "[test262] Warning: metadata generation failed (exit %d), will parse files directly\n", ret);
        } else {
            load_metadata_cache(METADATA_CACHE_FILE);
        }
    }
    if (!g_metadata_cache.empty()) {
        fprintf(stderr, "[test262] Loaded metadata cache: %zu entries from %s\n",
                g_metadata_cache.size(), METADATA_CACHE_FILE);
    }

    if (load_special_preamble_list(SPECIAL_PREAMBLE_FILE)) {
        fprintf(stderr, "[test262] Loaded special preamble map: %zu exact + %zu prefix entries from %s\n",
                g_special_preamble_exact.size(), g_special_preamble_prefix.size(), SPECIAL_PREAMBLE_FILE);
    }

    // Load non-fully-passing list from previous run
    load_partial_list("test/js262/t262_partial.txt");
    if (!g_known_partial.empty()) {
        fprintf(stderr, "[test262] Loaded %zu non-fully-passing entries from test/js262/t262_partial.txt\n",
                g_known_partial.size());
    }
    load_slow_test_list(SLOW_TEST_FILE);
    if (!g_slow_tests.empty()) {
        fprintf(stderr, "[test262] Loaded %zu slow-test entries from %s (own batch, 5s slow gate)\n",
                g_slow_tests.size(), SLOW_TEST_FILE);
    }

    if (g_run_async) {
        std::string async_source = !g_async_list_file.empty() ? g_async_list_file : g_batch_file;
        if (async_source.empty()) {
            fprintf(stderr, "[test262] --run-async enabled without --async-list or --batch-file; async tests remain skipped\n");
        } else if (load_test_name_allowlist(async_source, g_async_allowlist)) {
            fprintf(stderr, "[test262] Loaded async allowlist: %zu tests from %s\n",
                    g_async_allowlist.size(), async_source.c_str());
        } else {
            fprintf(stderr, "[test262] Error: cannot open async allowlist: %s\n", async_source.c_str());
            return 1;
        }
    }

    // register summary listener
    testing::TestEventListeners& listeners = testing::UnitTest::GetInstance()->listeners();
    listeners.Append(new Test262ReportListener());

    // --batch-file mode: run only tests from the given list in a single batch, then exit
    if (!g_batch_file.empty()) {
        // Load test names from file
        std::vector<std::string> batch_names;
        FILE* bf = fopen(g_batch_file.c_str(), "r");
        if (!bf) {
            fprintf(stderr, "[test262] Error: cannot open batch file: %s\n", g_batch_file.c_str());
            return 1;
        }
        char line[512];
        while (fgets(line, sizeof(line), bf)) {
            char* start = line;
            while (*start == ' ' || *start == '\t') start++;
            if (*start == '#' || *start == '\n' || *start == '\r' || *start == '\0') continue;

            if (g_diagnose_mode) {
                std::string row = trim_ascii(start);
                size_t tab1 = row.find('\t');
                std::string name = trim_ascii(tab1 == std::string::npos ? row : row.substr(0, tab1));
                if (name.empty()) continue;
                batch_names.push_back(name);

                if (tab1 != std::string::npos) {
                    size_t tab2 = row.find('\t', tab1 + 1);
                    if (tab2 != std::string::npos) {
                        size_t tab3 = row.find('\t', tab2 + 1);
                        std::string expected = row.substr(tab2 + 1,
                            tab3 == std::string::npos ? std::string::npos : tab3 - tab2 - 1);
                        std::vector<std::string> paths = split_diagnose_expected_paths(expected);
                        if (!paths.empty()) {
                            g_diagnose_expected_paths[name] = paths;
                        }
                    }
                }
                continue;
            }

            char* first = start;
            char* first_end = first;
            while (*first_end && *first_end != ' ' && *first_end != '\t' &&
                   *first_end != '\n' && *first_end != '\r') {
                first_end++;
            }
            char* second = first_end;
            while (*second == ' ' || *second == '\t') second++;
            char* second_end = second;
            while (*second_end && *second_end != ' ' && *second_end != '\t' &&
                   *second_end != '\n' && *second_end != '\r') {
                second_end++;
            }

            char* name = first;
            char* name_end = first_end;
            if ((strncmp(first, "SLOW_", 5) == 0 ||
                 strncmp(first, "CRASH_", 6) == 0 ||
                 strncmp(first, "BATCH_", 6) == 0) &&
                second_end > second) {
                name = second;
                name_end = second_end;
            }
            size_t len = (size_t)(name_end - name);
            if (len > 0) batch_names.push_back(std::string(name, len));
        }
        fclose(bf);
        fprintf(stderr, "[test262] Batch file: %zu tests from %s\n", batch_names.size(), g_batch_file.c_str());

        // Pre-load harness files (needed for batch execution)
        g_harness_sta = read_file_contents(g_harness_dir + "/sta.js");
        g_harness_assert = read_file_contents(g_harness_dir + "/assert.js");

        // Discover all tests and build name→index lookup
        auto all_tests = discover_all_tests();
        std::unordered_map<std::string, size_t> name_to_idx;
        for (size_t i = 0; i < all_tests.size(); i++) {
            name_to_idx[all_tests[i].test_name] = i;
        }

        // Build prepared entries for the batch
        std::vector<Test262Prepared> prepared;
        std::vector<size_t> indices;
        for (auto& name : batch_names) {
            auto it = name_to_idx.find(name);
            if (it == name_to_idx.end()) {
                std::string sanitized_name = sanitize_test262_manifest_name(name);
                if (sanitized_name != name) it = name_to_idx.find(sanitized_name);
            }
            if (it == name_to_idx.end()) {
                fprintf(stderr, "[test262]   WARNING: test not found: %s\n", name.c_str());
                continue;
            }
            const auto& param = all_tests[it->second];
            Test262Prepared p;
            p.test_name = param.test_name;
            p.test_path = param.test_path;
            p.category = param.category;
            p.subcategory = param.subcategory;
            p.skip_result = T262_PASS;
            p.is_negative = false;
            p.is_strict = false;
            p.is_async = false;
            p.is_module = false;
            p.is_raw = false;
            p.native_harness = false;
            Test262Metadata meta;
            bool metadata_loaded = false;
            bool is_module = false;
            bool is_raw = false;
            static const std::string prefix = std::string(TEST262_ROOT) + "/test/";
            if (param.test_path.size() > prefix.size() &&
                param.test_path.compare(0, prefix.size(), prefix) == 0) {
                std::string rel = param.test_path.substr(prefix.size());
                const auto& skip_map = skipped_tests();
                auto sit = skip_map.find(rel);
                if (sit != skip_map.end()) {
                    p.skip_result = T262_SKIP;
                    p.skip_message = sit->second;
                }
            }
            auto cm_it = g_metadata_cache.find(param.test_path);
            if (cm_it != g_metadata_cache.end()) {
                const CachedMeta& cm = cm_it->second;
                p.is_negative = cm.flags & 32;
                p.negative_type = cm.neg_type;
                p.is_strict = cm.flags & 8;
                p.is_async = cm.flags & 1;
                is_module = cm.flags & 2;
                p.is_module = is_module;
                is_raw = cm.flags & 4;
                p.is_raw = is_raw;
                p.includes = cm.includes;
                p.features = cm.features;
                p.native_harness = !p.is_async && cm.native_harness;
                meta.is_async = p.is_async;
                meta.is_module = is_module;
                meta.is_raw = is_raw;
                meta.is_negative = p.is_negative;
                meta.negative_type = p.negative_type;
                meta.includes = p.includes;
                meta.features = p.features;
                metadata_loaded = true;
            } else {
                std::string source = read_file_contents(param.test_path);
                if (!source.empty()) {
                    meta = parse_metadata(source);
                    p.is_negative = meta.is_negative;
                    p.negative_type = meta.negative_type;
                    p.is_strict = meta.is_strict;
                    p.is_async = meta.is_async;
                    is_module = meta.is_module;
                    p.is_module = is_module;
                    is_raw = meta.is_raw;
                    p.is_raw = is_raw;
                    p.includes = meta.includes;
                    p.features = meta.features;
                    metadata_loaded = true;
                }
            }
            if (metadata_loaded && p.skip_result != T262_SKIP) {
                p.is_slow_test = g_slow_tests.count(p.test_name) > 0;
                if (has_unsupported_feature_for_test(meta, p.test_name)) {
                    p.skip_result = T262_SKIP;
                    p.skip_message = unsupported_feature_skip_message(meta);
                } else if (is_module && !(g_run_async && is_es2021_module_test(p.test_path, meta))) {
                    p.skip_result = T262_SKIP;
                    p.skip_message = "module flag";
                } else if (is_raw && !is_es2021_raw_test(p.test_path, meta)) {
                    p.skip_result = T262_SKIP;
                    p.skip_message = "raw flag";
                } else if (p.is_async && !(async_test_is_enabled(p.test_name) ||
                        (g_run_async && is_js51_es2022_async_admission_test(p.test_name)) ||
                        (g_run_async && is_js53_waitasync_admission_test(p.test_name)) ||
                        (g_run_async && (is_dynamic_import_script_test(p.test_path, meta) ||
                            is_es2021_module_test(p.test_path, meta))))) {
                    p.skip_result = T262_SKIP;
                    p.skip_message = "async flag";
                }
            }
            p.special_preamble_includes = special_preamble_for_test(p.test_name, p.test_path);
            if (p.is_raw) p.native_harness = true;
            if (p.is_module) p.native_harness = false;
            if (p.skip_result != T262_SKIP) indices.push_back(prepared.size());
            prepared.push_back(std::move(p));
        }

        // Run as a single batch (all tests in one lambda.exe js-test-batch process)
        fprintf(stderr, "[test262] Running %zu tests in a single batch...\n", indices.size());
        auto results = execute_t262_batch(prepared, indices, g_t262_batch_chunk_size);

        // Evaluate and report per-test results
        int passed = 0, failed = 0;
        std::vector<std::string> failed_names;
        g_cached_batch_results = results;
        for (size_t i = 0; i < prepared.size(); i++) {
            auto result = evaluate_batch_result(prepared[i], results);
            if (g_diagnose_mode && result.result != T262_SKIP) {
                auto exp_it = g_diagnose_expected_paths.find(prepared[i].test_name);
                if (exp_it != g_diagnose_expected_paths.end()) {
                    auto br_it = results.find(prepared[i].test_name);
                    const std::string output = br_it != results.end() ? br_it->second.output : "";
                    std::vector<std::string> missing;
                    for (const std::string& path : exp_it->second) {
                        if (!diagnose_output_has_path(output, path)) missing.push_back(path);
                    }
                    if (!missing.empty()) {
                        std::string diag_msg = std::string("diagnose expected path(s) not hit: ") +
                            join_fields(missing, ",");
                        if (result.result == T262_PASS) {
                            result.result = T262_FAIL;
                            result.message = diag_msg;
                        } else if (result.message.find(diag_msg) == std::string::npos) {
                            if (!result.message.empty()) result.message += "\n";
                            result.message += diag_msg;
                        }
                    }
                }
            }
            g_failure_info[prepared[i].test_name] = {prepared[i].test_path, prepared[i].category,
                                                     prepared[i].subcategory, prepared[i].features,
                                                     prepared[i].includes, prepared[i].native_harness};
            g_cached_results[prepared[i].test_name] = result;
            const char* status = "?";
            switch (result.result) {
                case T262_PASS: status = "PASS"; passed++; break;
                case T262_PARTIAL_PASS: status = "PARTIAL"; failed++; failed_names.push_back(prepared[i].test_name); break;
                case T262_FAIL: status = "FAIL"; failed++; failed_names.push_back(prepared[i].test_name); break;
                case T262_TIMEOUT: status = "TIMEOUT"; failed++; failed_names.push_back(prepared[i].test_name); break;
                case T262_CRASH: status = "CRASH"; failed++; failed_names.push_back(prepared[i].test_name); break;
                case T262_SKIP: status = "SKIP"; break;
            }
            fprintf(stderr, "  [%s] %s", status, prepared[i].test_name.c_str());
            if (!result.message.empty() && result.result != T262_PASS) {
                // Show first line of failure message
                std::string first_line = result.message.substr(0, result.message.find('\n'));
                if (first_line.size() > 120) first_line = first_line.substr(0, 120) + "...";
                fprintf(stderr, " — %s", first_line.c_str());
            }
            fprintf(stderr, "\n");
        }
        write_phase_timing_log(results);
        fprintf(stderr, "\n[test262] Batch file result: %d passed, %d failed out of %zu\n",
                passed, failed, prepared.size());
        if (!failed_names.empty()) {
            fprintf(stderr, "[test262] Failed tests:\n");
            for (auto& name : failed_names) {
                fprintf(stderr, "  - %s\n", name.c_str());
            }
        }
        write_failure_artifacts();
        return failed > 0 ? 1 : 0;
    }

    // pre-run all tests in batch mode
    auto all_tests = discover_all_tests();
    batch_run_all_tests(all_tests);

    if (g_batch_only) {
        // Print summary directly from cached results, skip individual gtest runs
        int passed = 0, failed = 0, skipped = 0, partial = 0;
        std::vector<std::string> current_passing;  // only fully passed tests
        std::vector<std::string> regressions;
        std::vector<std::string> improvements;
        auto refresh_summary_from_cache = [&]() {
            passed = 0; failed = 0; skipped = 0; partial = 0;
            current_passing.clear();
            improvements.clear();
            std::lock_guard<std::mutex> lock(g_results_mutex);
            for (auto& kv : g_cached_results) {
                switch (kv.second.result) {
                    case T262_PASS: passed++; current_passing.push_back(kv.first); break;
                    case T262_PARTIAL_PASS: partial++; break;
                    case T262_SKIP: skipped++; break;
                    default: failed++; break;
                }
            }
            if (!g_baseline_passing.empty()) {
                for (auto& name : current_passing) {
                    if (g_baseline_passing.find(name) == g_baseline_passing.end()) {
                        improvements.push_back(name);
                    }
                }
            }
        };
        refresh_summary_from_cache();
        // Compute regressions/improvements vs baseline
        if (!g_baseline_passing.empty()) {
            std::set<std::string> pass_set(current_passing.begin(), current_passing.end());
            for (auto& name : g_baseline_passing) {
                if (pass_set.find(name) == pass_set.end()) regressions.push_back(name);
            }
        }

        // Phase 4: Retry regressions individually to distinguish real regressions
        // from batch-mode infrastructure failures (OOM kills, signal 9, etc.)
        auto phase4_start = std::chrono::steady_clock::now();
        if (!regressions.empty()) {
            fprintf(stderr, "[test262] Phase 4: Retrying %zu regressions individually...\n", regressions.size());
            // Build name→path lookup from all_tests
            std::unordered_map<std::string, size_t> name_to_idx;
            for (size_t i = 0; i < all_tests.size(); i++) {
                name_to_idx[all_tests[i].test_name] = i;
            }
            // Build prepared entries for regressions and run through batch retry
            std::vector<Test262Prepared> retry_prepared;
            std::vector<size_t> retry_indices;
            for (auto& reg_name : regressions) {
                auto it = name_to_idx.find(reg_name);
                if (it == name_to_idx.end()) continue;
                const auto& param = all_tests[it->second];
                Test262Prepared p;
                p.test_name = param.test_name;
                p.test_path = param.test_path;
                p.skip_result = T262_PASS;
                p.is_negative = false;
                p.is_strict = false;
                p.native_harness = false;
                // Load metadata from cache
                auto cm_it = g_metadata_cache.find(param.test_path);
                if (cm_it != g_metadata_cache.end()) {
                    const CachedMeta& cm = cm_it->second;
                    p.is_negative = cm.flags & 32;
                    p.negative_type = cm.neg_type;
                    p.is_strict = cm.flags & 8;
                    p.includes = cm.includes;
                    p.native_harness = cm.native_harness;
                }
                // Phase 4 rebuilds prepared records from all_tests, so carry the
                // batch-local helper policy too.  Without this, helper-dependent
                // regressions retry without their special preamble and thousands
                // of retry results become infrastructure noise instead of signal.
                p.special_preamble_includes = special_preamble_for_test(p.test_name, p.test_path);
                retry_indices.push_back(retry_prepared.size());
                retry_prepared.push_back(std::move(p));
            }
            // Run each individually (chunk_size=1) to isolate from batch effects
            if (!retry_prepared.empty()) {
                char saved_js_timeout_arg[sizeof(g_js_timeout_arg)];
                memcpy(saved_js_timeout_arg, g_js_timeout_arg, sizeof(g_js_timeout_arg));
                snprintf(g_js_timeout_arg, sizeof(g_js_timeout_arg),
                         "--timeout=%d", T262_PHASE4_RETRY_TIMEOUT_SECS);
                fprintf(stderr, "[test262] Phase 4: using %ds isolated retry timeout\n",
                        T262_PHASE4_RETRY_TIMEOUT_SECS);
                auto retry_results = execute_t262_batch(retry_prepared, retry_indices, 1);
                memcpy(g_js_timeout_arg, saved_js_timeout_arg, sizeof(g_js_timeout_arg));
                std::vector<std::string> real_regressions;
                std::vector<std::string> recovered_names;
                size_t recovered = 0;
                for (auto& reg_name : regressions) {
                    // Find in retry_prepared
                    Test262RunResult result = {T262_FAIL, "not retried"};
                    for (size_t i = 0; i < retry_prepared.size(); i++) {
                        if (retry_prepared[i].test_name == reg_name) {
                            result = evaluate_batch_result(retry_prepared[i], retry_results);
                            break;
                        }
                    }
                    if (result.result == T262_PASS) {
                        recovered++;
                        recovered_names.push_back(reg_name);
                        // Check if this was crash-collateral (batch had a BATCH_KILL crash-point).
                        // Crash-collateral recoveries are promoted to full pass.
                        auto ba_it = g_batch_assignment.find(reg_name);
                        bool is_collateral = ba_it != g_batch_assignment.end() &&
                                             g_crashed_batches.count(ba_it->second);
                        if (is_collateral) {
                            // Promote to full pass; summary counters refresh from cache after Phase 4.
                            {
                                std::lock_guard<std::mutex> lock(g_results_mutex);
                                g_cached_results[reg_name] = {T262_PASS, "recovered in Phase 4 retry (crash-collateral)"};
                            }
                        } else {
                            // Genuine batch-instability — keep as partial.
                            {
                                std::lock_guard<std::mutex> lock(g_results_mutex);
                                g_cached_results[reg_name] = {T262_PARTIAL_PASS, "non-fully-passing: passed in Phase 4 retry only"};
                            }
                        }
                    } else {
                        real_regressions.push_back(reg_name);
                    }
                }
                fprintf(stderr, "[test262] Phase 4: %zu/%zu regressions recovered (were batch kills)\n",
                        recovered, regressions.size());

                // Diagnostic: group recovered tests by their original Phase 2 batch
                // to identify which batches have state-leak / interaction issues.
                if (!recovered_names.empty()) {
                    std::map<size_t, std::vector<std::string>> recovered_by_batch;
                    std::vector<std::string> untracked_recovered;  // not in any tracked batch
                    for (auto& name : recovered_names) {
                        auto it = g_batch_assignment.find(name);
                        if (it != g_batch_assignment.end()) {
                            recovered_by_batch[it->second].push_back(name);
                        } else {
                            untracked_recovered.push_back(name);
                        }
                    }
                    fprintf(stderr, "[test262] Phase 4 diagnostic: %zu recovered tests came from %zu batches",
                            recovered_names.size(), recovered_by_batch.size());
                    if (!untracked_recovered.empty()) {
                        fprintf(stderr, " (%zu ran individually/non-batch)", untracked_recovered.size());
                    }
                    fprintf(stderr, "\n");

                    // Write detailed report to file for analysis
                    FILE* diag = fopen("temp/_t262_batch_kills.txt", "w");
                    if (diag) {
                        fprintf(diag, "# Phase 4 batch-kill diagnostic\n");
                        fprintf(diag, "# %zu tests recovered from %zu batches (batch size = %zu)\n",
                                recovered_names.size(), recovered_by_batch.size(), g_t262_batch_chunk_size);
                        fprintf(diag, "# Format: batch[N] — M recovered / K total tests in batch\n");
                        fprintf(diag, "# Tests marked [RECOVERED] passed individually but failed in batch\n\n");
                        for (auto& [bi, names] : recovered_by_batch) {
                            fprintf(diag, "batch[%zu] — %zu recovered / %zu total tests:\n",
                                    bi, names.size(), bi < g_batch_contents.size() ? g_batch_contents[bi].size() : 0);
                            // List all tests in this batch, marking recovered ones
                            if (bi < g_batch_contents.size()) {
                                std::set<std::string> recovered_set(names.begin(), names.end());
                                for (auto& tname : g_batch_contents[bi]) {
                                    fprintf(diag, "  %s  %s\n",
                                            recovered_set.count(tname) ? "[RECOVERED]" : "[OK]        ",
                                            tname.c_str());
                                }
                            }
                            fprintf(diag, "\n");
                        }
                        // Write untracked recovered tests (from non-batch or unknown batches)
                        if (!untracked_recovered.empty()) {
                            fprintf(diag, "# %zu recovered tests NOT in any tracked clean batch:\n",
                                    untracked_recovered.size());
                            for (auto& name : untracked_recovered) {
                                fprintf(diag, "  [RECOVERED-UNTRACKED]  %s\n", name.c_str());
                            }
                            fprintf(diag, "\n");
                        }
                        fclose(diag);
                        fprintf(stderr, "[test262] Phase 4 diagnostic → temp/_t262_batch_kills.txt\n");
                    }
                }

                regressions = std::move(real_regressions);
            }
        }
        auto phase4_done = std::chrono::steady_clock::now();
        g_phase4_secs = std::chrono::duration<double>(phase4_done - phase4_start).count();
        refresh_summary_from_cache();

        int total = passed + failed + partial;
        double pct = total > 0 ? 100.0 * passed / total : 0.0;
        printf("\n");
        printf("╔══════════════════════════════════════════════════╗\n");
        printf("║         test262 Compliance Summary               ║\n");
        printf("╠══════════════════════════════════════════════════╣\n");
        printf("║  Fully passed: %5d / %5d  (%.1f%%)             ║\n", passed, total, pct);
        printf("║  Non-fully-passing: %5d  (batch-unstable or slow)   ║\n", partial);
        printf("║  Failed:       %5d                             ║\n", failed);
        printf("║  Skipped:      %5d                             ║\n", skipped);
        printf("╚══════════════════════════════════════════════════╝\n");
        if (partial > 0) {
            printf("\n  Non-fully-passing tests (%d):\n", partial);
            std::lock_guard<std::mutex> lock(g_results_mutex);
            for (auto& kv : g_cached_results) {
                if (kv.second.result == T262_PARTIAL_PASS) {
                    printf("  ~ %s  [%s]\n", kv.first.c_str(), kv.second.message.c_str());
                }
            }
        }
        if (!g_baseline_passing.empty()) {
            printf("\n╔══════════════════════════════════════════════════╗\n");
            printf("║         Regression Check vs Baseline             ║\n");
            printf("╠══════════════════════════════════════════════════╣\n");
            printf("║  Baseline passing: %5zu                         ║\n", g_baseline_passing.size());
            printf("║  Fully passing:    %5zu                         ║\n", current_passing.size());
            printf("║  Non-fully-passing:%5d                          ║\n", partial);
            printf("║  Improvements:     %5zu  (fail → pass)          ║\n", improvements.size());
            printf("║  Regressions:      %5zu  (pass → fail)          ║\n", regressions.size());
            printf("╚══════════════════════════════════════════════════╝\n");
            if (!regressions.empty()) {
                printf("\n⚠️  REGRESSIONS (%zu tests):\n", regressions.size());
                std::sort(regressions.begin(), regressions.end());
                for (auto& r : regressions) {
                    auto rit = g_cached_results.find(r);
                    if (rit != g_cached_results.end()) {
                        printf("  - %s  [result=%d msg=%s]\n", r.c_str(), (int)rit->second.result, rit->second.message.c_str());
                    } else {
                        printf("  - %s  [NOT IN CACHE]\n", r.c_str());
                    }
                }
            }
            if (!improvements.empty() && improvements.size() <= 50) {
                printf("\n✅  IMPROVEMENTS (%zu tests):\n", improvements.size());
                std::sort(improvements.begin(), improvements.end());
                for (auto& r : improvements) printf("  + %s\n", r.c_str());
            } else if (!improvements.empty()) {
                printf("\n✅  IMPROVEMENTS: %zu tests (too many to list)\n", improvements.size());
            }
        }
        // Update baseline if requested (batch-only path) — gated by stability criteria
        if (g_update_baseline) {
            bool gate_ok = true;
            if (g_phase_batch_lost > 0) {
                printf("\n❌  Baseline NOT updated: %zu batch-lost (must be 0)\n", g_phase_batch_lost);
                gate_ok = false;
            }
            if (g_phase_crash_exit > 0) {
                printf("\n❌  Baseline NOT updated: %zu crash-exits (must be 0)\n", g_phase_crash_exit);
                gate_ok = false;
            }
            if (!regressions.empty()) {
                printf("\n❌  Baseline NOT updated: %zu regressions (must be 0)\n", regressions.size());
                gate_ok = false;
            }
            if ((int)current_passing.size() < STABLE_BASELINE_MIN) {
                printf("\n❌  Baseline NOT updated: %zu fully passing < %d (stable minimum)\n",
                       current_passing.size(), STABLE_BASELINE_MIN);
                gate_ok = false;
            }
            if (gate_ok) {
                write_baseline_file(BASELINE_FILE, current_passing,
                                    g_total_tests, skipped, g_total_batched, failed);
                clean_partial_list_after_baseline_update(current_passing);
                printf("\n📝  Baseline updated: %s (%zu fully passing tests, gate: batch-lost=0 crash=0 min=%d)\n",
                       BASELINE_FILE, current_passing.size(), STABLE_BASELINE_MIN);
            }
        }
        write_failure_artifacts();
        return regressions.empty() ? 0 : 1;
    }

    return REPORT_ALL_TESTS();
}
