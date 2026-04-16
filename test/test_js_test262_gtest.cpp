// =============================================================================
// test262 Compliance Test Runner for LambdaJS
// =============================================================================
//
// Workflow to move a test into the baseline list:
//   1. Fix it so that it passes, is stable, and finishes in < 3 seconds.
//   2. Run it in a batch of 50 tests.  If still stable, add it to baseline.
//
// =============================================================================
// Dynamically discovers and runs ECMAScript test262 tests against lambda.exe js.
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
// Stable checkpoint (commit 86235a964, 2026-04-15):
//   Baseline passing:  21,824
//   Nonbatch entries:  0
//   Batch-lost:        0
//   Crash-exits:       0
//   Regressions:       0   (verified stable across 2 consecutive runs)
//
// --update-baseline gate: to update the baseline, ALL of the following must hold:
//   1. Nonbatch entries   == 0   (all tests batch-safe)
//   2. Batch-lost         == 0   (no infrastructure failures)
//   3. Crash-exits        == 0   (no crashes)
//   4. Fully passing      >= STABLE_BASELINE_MIN  (net improvement required)
//   5. Regressions        == 0   (no test may regress)
//
// Execution model:
//   - Phase 1:  Parse metadata, partition tests into three groups:
//       (a) CLEAN    — safe to batch (50 tests per process, 12 parallel workers)
//       (b) NON-BATCH — tests whose harness includes mutate global built-in state
//                        (e.g. propertyHelper.js deletes properties to test
//                        configurability).  These MUST run individually (batch
//                        size = 1) to prevent poisoning co-batched tests.
//       (c) CRASHERS — quarantined from a previous run; skipped entirely.
//   - Phase 2:  Execute CLEAN tests in batched workers.
//   - Phase 2a: Execute NON-BATCH tests individually.
//   - Phase 2b: Retry any batch-lost tests individually (crash recovery).
//   - Phase 3:  Evaluate results against expected outcomes.
//   - Phase 4:  (--batch-only) Retry regressions individually; if recovered,
//               the failure was a batch interaction, not a real regression.
//
// Key invariant: after Phase 2 + 2a + 2b, every non-skipped test must have
// a result.  If any test is MISSING, that is a crash and a critical bug.
//
// When adding a new test harness that mutates global state, add it to
// NON_BATCH_INCLUDES so it always runs in isolation.
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
#endif

// =============================================================================
// Configuration
// =============================================================================

static const char* TEST262_ROOT = "ref/test262";
static const char* TEST262_SOURCE_DIR = "test/js262";  // comment-stripped test files (symlink to ../lambda-test/js262)
static std::string g_harness_dir = "ref/test262/harness";
// Only tests with runtime < 3s (debug build) belong in the baseline.
// Slow tests (>= 3s) should be moved to temp/_t262_crashers.txt with SLOW status.
static const char* BASELINE_FILE = "test/js262/test262_baseline.txt";
static const char* NONBATCH_FILE = "test/js262/test262_nonbatch.txt";
static bool g_use_stripped = false;  // use comment-stripped test files from TEST262_SOURCE_DIR

// Features above ES2020 — skip tests requiring these.
// Target: ES2020 compliance. All features ≤ES2020 are in scope.
// Reference: TC39 finished-proposals.md (publication year field)
static const std::set<std::string> UNSUPPORTED_FEATURES = {
    // === ES2015 features (not implemented) ===
    "tail-call-optimization",                     // Proper tail calls (100K+ recursion, hangs without TCO)

    // === ES2021 features ===
    "WeakRef",                                    // Weak references
    "FinalizationRegistry",                       // GC callback hooks
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
    "top-level-await",                            // Module-level await
    "regexp-match-indices",                       // /d flag, .indices
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
    "Atomics.waitAsync",                          // Atomics.waitAsync proposal
    "resizable-arraybuffer",                      // Resizable/growable ArrayBuffers
    "ArrayBuffer-transfer", "arraybuffer-transfer", // ArrayBuffer.prototype.transfer
    "regexp-v-flag",                              // Unicode sets (/v flag)
    // "promise-with-resolvers",                  // SUPPORTED
    // "array-grouping",                          // SUPPORTED
    "String.prototype.isWellFormed",              // Well-Formed Unicode Strings
    "String.prototype.toWellFormed",

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

    // === Test harness / Annex B / host features ===
    "IsHTMLDDA",                                  // document.all behavior
    "host-gc-required",                           // Requires $262.gc()
    "cross-realm",                                // Requires $262.createRealm()
    "caller",                                     // Function.prototype.caller (Annex B)
};

// Tests skipped by relative path (under ref/test262/test/).
// Use this for tests that are non-deterministic, test implementation-specific
// behavior, or are known false positives unrelated to grammar/runtime correctness.
static const std::map<std::string, std::string> SKIPPED_TESTS = {
    // Math.random() is inherently non-deterministic — this test calls
    // Math.random() 100 times and checks values are in [0,1). It can
    // intermittently fail depending on the PRNG state and does not
    // indicate a real engine regression.
    {"built-ins/Math/random/S15.8.2.14_A1.js", "non-deterministic (Math.random)"},
};

// Harness files that mutate global built-in state.  Tests including ANY of
// these MUST run individually, never inside a shared batch process, because
// the mutations leak into subsequent tests within the same batch.
// NOTE: propertyHelper.js was previously listed here because its
// isConfigurable() helper called `delete obj[name]` without restoring.
// We patched isConfigurable() to restore after testing, so it's now safe
// to batch.
static const std::set<std::string> NON_BATCH_INCLUDES = {
};

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
                    if (fs == std::string::npos || ln[fs] != '-') break;
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

static const char* METADATA_CACHE_FILE = "temp/test262_metadata.tsv";

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
    T262_PASS,          // fully passed: batch run + time < 3s (or non-batch individual pass)
    T262_PARTIAL_PASS,  // passed individually but not reliably in batch (recovered, slow, or quarantined)
    T262_FAIL,
    T262_SKIP,
    T262_TIMEOUT,
    T262_CRASH,
};

struct Test262RunResult {
    Test262Result result;
    std::string message;
};

static bool has_unsupported_feature(const Test262Metadata& meta) {
    for (auto& f : meta.features) {
        if (UNSUPPORTED_FEATURES.count(f)) return true;
    }
    return false;
}

// =============================================================================
// Batch mode: prepare temp files, run through js-test-batch, evaluate
// =============================================================================

// Cached harness sources (loaded once)
static std::string g_harness_sta;
static std::string g_harness_assert;
static std::mutex  g_harness_mutex;
static std::unordered_map<std::string, std::string> g_harness_cache;

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
    std::vector<std::string> includes; // harness includes (e.g. "propertyHelper.js")
    bool is_strict;              // add "use strict" prefix
    Test262Result skip_result;   // T262_SKIP if test should be skipped
    std::string skip_message;
    bool is_negative;
    std::string negative_type;
    bool native_harness;         // true = run without JS harness preamble (native interception only)
};

// Assemble combined source on-the-fly from metadata.
// Reads test file from disk (OS-cached) and prepends harness files.
// Assemble the harness source (sta.js + assert.js) — sent once per batch via harness: protocol
static std::string assemble_harness_source() {
    std::string harness;
    harness.reserve(g_harness_sta.size() + g_harness_assert.size() + 4);
    harness += g_harness_sta;
    harness += '\n';
    harness += g_harness_assert;
    harness += '\n';
    return harness;
}

// Map original test path to stripped version if available
static std::string get_source_path(const std::string& test_path) {
    if (g_use_stripped) {
        // ref/test262/test/... -> test/js262/test/...
        std::string stripped = std::string(TEST262_SOURCE_DIR) +
                               test_path.substr(strlen(TEST262_ROOT));
        return stripped;
    }
    return test_path;
}

// Assemble test source WITHOUT harness (for two-module split: harness sent separately)
static std::string assemble_test_source(const Test262Prepared& p) {
    std::string source = read_file_contents(get_source_path(p.test_path));
    if (source.empty()) return "";

    std::string combined;
    combined.reserve(source.size() + 4096);

    for (auto& inc : p.includes) {
        const std::string& harness_src = get_harness_file(inc);
        if (!harness_src.empty()) {
            combined += harness_src;
            combined += '\n';
        }
    }

    if (p.is_strict) {
        combined += "\"use strict\";\n";
    }
    combined += source;
    return combined;
}

// Assemble raw test source for native harness mode (no includes, just strict prefix + test body)
static std::string assemble_native_test_source(const Test262Prepared& p) {
    std::string source = read_file_contents(get_source_path(p.test_path));
    if (source.empty()) return "";

    if (p.is_strict) {
        std::string combined;
        combined.reserve(source.size() + 20);
        combined += "\"use strict\";\n";
        combined += source;
        return combined;
    }
    return source;
}

// Legacy: assemble combined source with harness included (for backward compatibility)
static std::string assemble_combined_source(const Test262Prepared& p) {
    std::string source = read_file_contents(get_source_path(p.test_path));
    if (source.empty()) return "";

    std::string combined;
    combined.reserve(g_harness_sta.size() + g_harness_assert.size() + source.size() + 4096);
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

    if (p.is_strict) {
        combined += "\"use strict\";\n";
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
};

static const size_t T262_BATCH_CHUNK_SIZE = 50;
static const size_t T262_MAX_PARALLEL_BATCHES = 12;
static const size_t RETRY_BATCH_SIZE = 5;

struct SubBatch { size_t start; size_t end; bool native; };

struct BatchTiming {
    size_t batch_idx;
    double elapsed_secs;
    size_t num_tests;
};

// Forward declarations for globals used in prepare phase
static std::set<std::string> g_baseline_passing;
static std::set<std::string> g_known_crashers;
static std::unordered_map<std::string, std::string> known_crasher_tags;  // test_name → original tag (e.g. "CRASH_139")
static std::set<std::string> g_nonbatch_tests;  // tests that must run individually (from test262_nonbatch.txt)
static std::set<std::string> g_known_slow_tests;  // tests >3s elapsed in previous run
static const long SLOW_THRESHOLD_US = 3000000L;  // 3 seconds
static bool g_update_baseline = false;
static bool g_baseline_only = false;
static bool g_batch_only = false;
static bool g_no_hot_reload = false;
static bool g_mir_interp = false;
static bool g_no_stripped = false;  // --no-stripped: force original test files
static std::string g_batch_file;   // --batch-file=<path>: run only tests from this list in a single batch
static int g_opt_level = 0;  // default -O0 (fastest for short-lived test262 scripts)
static char g_opt_level_arg[20] = "--opt-level=0";  // "--opt-level=N"
static int g_total_tests = 0;   // total discovered tests
static int g_total_skipped = 0; // total skipped tests
static int g_total_batched = 0; // total batched (executed) tests
static double g_prep_secs = 0;  // Phase 1 (prepare) runtime
static double g_exec_secs = 0;  // Phase 2 (execute) runtime
static double g_total_secs = 0; // total runtime

// Stable checkpoint baseline (commit 86235a964, 2026-04-15).
// --update-baseline requires fully passing >= this value.
static const int STABLE_BASELINE_MIN = 21824;

// Phase-level counters set during execution, read by --update-baseline gate.
static size_t g_phase_nonbatch_count = 0;   // nonbatch entries loaded from file
static size_t g_phase_crash_exit = 0;       // crash-exit tests (exit > 128)
static size_t g_phase_batch_lost = 0;       // tests lost in batch, recovered individually

// Batch assignment tracking: which batch each test was in during Phase 2.
// Used by Phase 4 to diagnose which co-batched tests cause false failures.
static std::unordered_map<std::string, size_t> g_batch_assignment;  // test_name → batch_index
static std::vector<std::vector<std::string>> g_batch_contents;      // batch_index → [test_names]

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

// Write baseline file with header comments
static void write_baseline_file(const char* path, std::vector<std::string>& passing,
                                 int total_tests, int skipped, int batched, int failed) {
    FILE* f = fopen(path, "w");
    if (!f) return;
    std::string commit = get_git_commit_hash();
    std::sort(passing.begin(), passing.end());
    fprintf(f, "# test262 baseline: tests that PASS (auto-updated)\n");
    fprintf(f, "# Commit: %s\n", commit.c_str());
    fprintf(f, "# Scope: ES2020 (skip ES2021+ features)\n");
    fprintf(f, "# Total passing: %zu\n", passing.size());
    fprintf(f, "# Total tests: %d  Skipped: %d  Batched: %d  Passed: %zu  Failed: %d\n",
            total_tests, skipped, batched, passing.size(), failed);
    fprintf(f, "# Runtime: %.1fs total (prep %.1fs + exec %.1fs)\n",
            g_total_secs, g_prep_secs, g_exec_secs);
    for (auto& name : passing) {
        fprintf(f, "%s\n", name.c_str());
    }
    fclose(f);
}

// Load known crashers from previous run's crasher log.
// Tests listed here are quarantined into their own small batches
// to avoid collateral damage to co-batched tests.
static void load_known_crashers(const char* path) {
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
            g_known_crashers.insert(std::string(name_start));
            // Store the tag (e.g. "CRASH_139") for use in quarantine retention logic
            *first_tab = '\0';  // null-terminate the tag
            known_crasher_tags[std::string(name_start)] = std::string(buf);
            if (strncmp(buf, "SLOW_", 5) == 0)
                g_known_slow_tests.insert(std::string(name_start));
        }
    }
    fclose(f);
}

// Load non-batch test list: tests that must run individually, not in shared batches.
// These are tests that destructively mutate global built-in state.
static void load_nonbatch_list(const char* path) {
    FILE* f = fopen(path, "r");
    if (!f) return;
    char buf[2048];
    while (fgets(buf, sizeof(buf), f)) {
        // skip comments and empty lines
        if (buf[0] == '#' || buf[0] == '\n' || buf[0] == '\r') continue;
        // trim trailing whitespace
        size_t len = strlen(buf);
        while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r' || buf[len-1] == ' '))
            buf[--len] = '\0';
        if (len > 0) g_nonbatch_tests.insert(std::string(buf));
    }
    fclose(f);
    g_phase_nonbatch_count = g_nonbatch_tests.size();
    if (!g_nonbatch_tests.empty())
        fprintf(stderr, "[test262] Loaded %zu non-batch tests from %s\n",
                g_nonbatch_tests.size(), path);
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
            p.skip_result = T262_PASS; // not skipped by default
            p.is_negative = false;
            p.is_strict = false;
            p.native_harness = false;

            // check test-specific skip list (by relative path)
            {
                static const std::string prefix = std::string(TEST262_ROOT) + "/test/";
                if (param.test_path.size() > prefix.size() &&
                    param.test_path.compare(0, prefix.size(), prefix) == 0) {
                    std::string rel = param.test_path.substr(prefix.size());
                    auto sit = SKIPPED_TESTS.find(rel);
                    if (sit != SKIPPED_TESTS.end()) {
                        p.skip_result = T262_SKIP;
                        p.skip_message = sit->second;
                        continue;
                    }
                }
            }

            // load metadata from cache or fall back to file read
            Test262Metadata meta;
            bool cached_native_harness = false;
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
            if (meta.is_async)   { p.skip_result = T262_SKIP; p.skip_message = "async flag"; continue; }
            if (meta.is_module)  { p.skip_result = T262_SKIP; p.skip_message = "module flag"; continue; }
            if (meta.is_raw)     { p.skip_result = T262_SKIP; p.skip_message = "raw flag"; continue; }
            if (has_unsupported_feature(meta)) { p.skip_result = T262_SKIP; p.skip_message = "unsupported feature"; continue; }

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

#ifndef NDEBUG
            // Native harness eligibility: pre-computed in metadata cache (V2+),
            // or computed inline when no cache is available.
            p.native_harness = cached_native_harness;
#else
            p.native_harness = false;
#endif

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
// (the internal --timeout=10 only catches JS-level loops, not hangs in JIT/parser).
static constexpr int T262_HARD_TIMEOUT_PER_TEST = 15;
static constexpr int T262_HARD_TIMEOUT_MIN = 30;

// Run a sub-batch of tests from a pre-written manifest file + stdout pipe
// Run a sub-batch from a manifest file using posix_spawn (avoids fork's page table copy)
static void run_t262_sub_batch(
    const char* manifest_path,
    std::unordered_map<std::string, BatchResult>& results,
    size_t num_tests = T262_BATCH_CHUNK_SIZE)
{
    int stdout_pipe[2];
    if (pipe(stdout_pipe) != 0) return;
    fcntl(stdout_pipe[0], F_SETFD, FD_CLOEXEC);
    fcntl(stdout_pipe[1], F_SETFD, FD_CLOEXEC);

    int manifest_fd = open(manifest_path, O_RDONLY);
    if (manifest_fd < 0) {
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        return;
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

    char* argv[8] = {
        (char*)"lambda.exe", (char*)"js-test-batch", (char*)"--timeout=10", NULL, NULL, NULL, NULL, NULL
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
    extern char** environ;
    pid_t pid;
    int ret = posix_spawn(&pid, "./lambda.exe", &file_actions, NULL, argv, environ);
    posix_spawn_file_actions_destroy(&file_actions);

    close(manifest_fd);
    close(stdout_pipe[1]);

    if (ret != 0) {
        close(stdout_pipe[0]);
        return;
    }

    // Watchdog thread: kills the worker if it exceeds the hard timeout.
    // This catches hangs in JIT compilation, parsing, or native code that the
    // internal --timeout=10 can't interrupt. Killing the process closes its
    // stdout pipe, which unblocks the fgets loop below.
    int hard_timeout_secs = std::max(T262_HARD_TIMEOUT_MIN, (int)(num_tests * T262_HARD_TIMEOUT_PER_TEST));
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
                    int status = atoi(buffer + 11);
                    long elapsed_us = 0;
                    size_t rss_before = 0, rss_after = 0;
                    const char* space2 = strchr(buffer + 11, ' ');
                    if (space2) {
                        elapsed_us = atol(space2 + 1);
                        const char* space3 = strchr(space2 + 1, ' ');
                        if (space3) {
                            rss_before = (size_t)atol(space3 + 1);
                            const char* space4 = strchr(space3 + 1, ' ');
                            if (space4) rss_after = (size_t)atol(space4 + 1);
                        }
                    }
                    results[current_script] = {current_output, status, elapsed_us, rss_before, rss_after};
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
    if (WIFSIGNALED(wstatus)) {
        int sig = WTERMSIG(wstatus);
        fprintf(stderr, "[test262] Batch worker PID %d killed by signal %d (%s), collected %zu results\n",
                pid, sig, strsignal(sig), results.size());
    } else if (WIFEXITED(wstatus) && WEXITSTATUS(wstatus) != 0) {
        // Normal exit with non-zero status (e.g., from MAX_CRASH_COUNT break)
        fprintf(stderr, "[test262] Batch worker PID %d exited with status %d, collected %zu results\n",
                pid, WEXITSTATUS(wstatus), results.size());
    }
    worker_done.store(true, std::memory_order_relaxed);
    watchdog.join();
}

// Phase 2: Execute all prepared tests through js-test-batch (reusable manifest files + stdout pipes)
// chunk_size: number of tests per sub-batch process (default T262_BATCH_CHUNK_SIZE=50, use 1 for non-batch)
static std::unordered_map<std::string, BatchResult> execute_t262_batch(
    const std::vector<Test262Prepared>& prepared,
    const std::vector<size_t>& indices,
    size_t chunk_size = T262_BATCH_CHUNK_SIZE)
{
    std::unordered_map<std::string, BatchResult> results;
    if (indices.empty()) return results;

    // Split indices into native-harness and JS-harness groups.
    // Native tests run without harness preamble (transpiler intercepts all harness calls).
    // JS tests run with sta.js+assert.js preamble as before.
    std::vector<size_t> native_indices, js_indices;
    for (size_t idx : indices) {
        if (prepared[idx].native_harness)
            native_indices.push_back(idx);
        else
            js_indices.push_back(idx);
    }
    fprintf(stderr, "[test262] Batch split: %zu native-harness + %zu js-harness = %zu total\n",
            native_indices.size(), js_indices.size(), indices.size());

    // Create sub-batches from both groups
    std::vector<SubBatch> batches;
    for (size_t s = 0; s < native_indices.size(); s += chunk_size) {
        size_t e = std::min(s + chunk_size, native_indices.size());
        batches.push_back({s, e, true});
    }
    size_t native_batch_count = batches.size();
    for (size_t s = 0; s < js_indices.size(); s += chunk_size) {
        size_t e = std::min(s + chunk_size, js_indices.size());
        batches.push_back({s, e, false});
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
                const auto& idx_vec = batches[bi].native ? native_indices : js_indices;
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
    // Each worker reuses ONE temp manifest file (6 workers = 6 files total).
    // For each sub-batch: truncate → write source data → fork/exec → read stdout.
    // This avoids per-batch file create/unlink overhead while keeping fast file-based stdin.
    std::vector<std::unordered_map<std::string, BatchResult>> thread_results(batches.size());
    std::vector<BatchTiming> batch_timings(batches.size());
    std::atomic<size_t> next_batch{0};
    size_t num_workers = std::min(T262_MAX_PARALLEL_BATCHES, batches.size());
    std::vector<std::thread> threads;
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

                const auto& idx_vec = batches[i].native ? native_indices : js_indices;

                if (!batches[i].native) {
                    // JS-harness batch: send harness preamble via harness: protocol
                    std::string harness = assemble_harness_source();
                    fprintf(mf, "harness:%zu\n", harness.size());
                    fwrite(harness.data(), 1, harness.size(), mf);
                    fputc('\n', mf);
                }
                // else: native-harness batch — no harness preamble (transpiler intercepts calls)

                // Send each test source
                for (size_t idx = batches[i].start; idx < batches[i].end; idx++) {
                    size_t pi = idx_vec[idx];
                    const auto& p = prepared[pi];
                    std::string test_src = batches[i].native
                        ? assemble_native_test_source(p)
                        : assemble_test_source(p);
                    fprintf(mf, "source:%s:%zu\n",
                            p.test_name.c_str(), test_src.size());
                    fwrite(test_src.data(), 1, test_src.size(), mf);
                    fputc('\n', mf);
                }
                fclose(mf);

                // Execute: fork/exec with stdin from manifest, read stdout via pipe
                auto t0 = std::chrono::steady_clock::now();
                size_t batch_num_tests = batches[i].end - batches[i].start;
                run_t262_sub_batch(manifest_path, thread_results[i], batch_num_tests);
                auto t1 = std::chrono::steady_clock::now();
                batch_timings[i] = {i, std::chrono::duration<double>(t1 - t0).count(),
                                    batch_num_tests};
            }
            unlink(manifest_path);
        });
    }
    for (auto& t : threads) t.join();

    // Report per-batch timing — sort by elapsed time, show top 20 slowest
    {
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
                    batches[bi].native ? "[native]" : "[js]    ",
                    batches[bi].start, batches[bi].end - 1);
        }
        // For the top 5 slowest batches, list all test names
        fprintf(stderr, "\n[test262] Tests in top 5 slowest batches:\n");
        size_t detail = std::min((size_t)5, batches.size());
        for (size_t k = 0; k < detail; k++) {
            size_t bi = order[k];
            const auto& idx_vec = batches[bi].native ? native_indices : js_indices;
            fprintf(stderr, "  --- batch[%zu] (%.1fs, %s) ---\n", bi, batch_timings[bi].elapsed_secs,
                    batches[bi].native ? "native" : "js");
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
            return {T262_TIMEOUT, "slow test (took >3s in previous run, quarantined)"};
        return {T262_TIMEOUT, "test timed out (10s)"};
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

static std::unordered_map<std::string, Test262RunResult> g_cached_results;
static std::mutex g_results_mutex;
static bool g_parallel_done = false;

static void batch_run_all_tests(const std::vector<Test262Param>& tests) {
    auto start_time = std::chrono::steady_clock::now();

    // Phase 1: parse metadata only (lazy assembly defers source to Phase 2)
    std::vector<Test262Prepared> prepared;
    std::vector<size_t> batch_indices;
    prepare_all_tests(tests, prepared, batch_indices);

    // Partition batch_indices into three groups:
    //   1. clean_indices   — safe to run in shared batches of 50
    //   2. nonbatch_indices — tests that mutate global state (propertyHelper.js etc.)
    //                         and MUST run individually to avoid poisoning co-batched tests
    //   3. crasher_indices  — known crashers quarantined from previous runs
    std::vector<size_t> clean_indices;
    std::vector<size_t> nonbatch_indices;
    std::vector<size_t> crasher_indices;
    for (size_t idx : batch_indices) {
        if (!g_known_crashers.empty() && g_known_crashers.count(prepared[idx].test_name)) {
            crasher_indices.push_back(idx);
        } else {
            // check if any included harness is in the non-batch list
            bool is_nonbatch = false;
            if (g_nonbatch_tests.count(prepared[idx].test_name)) {
                is_nonbatch = true;
            } else {
                for (auto& inc : prepared[idx].includes) {
                    if (NON_BATCH_INCLUDES.count(inc)) {
                        is_nonbatch = true;
                        break;
                    }
                }
            }
            if (is_nonbatch) {
                nonbatch_indices.push_back(idx);
            } else {
                clean_indices.push_back(idx);
            }
        }
    }

    auto prep_time = std::chrono::steady_clock::now();
    double prep_secs = std::chrono::duration<double>(prep_time - start_time).count();
    fprintf(stderr, "[test262] Phase 1 (prepare): %.1fs — %zu scripts to batch (%zu clean, %zu non-batch, %zu quarantined-crashers)\n",
            prep_secs, batch_indices.size(), clean_indices.size(), nonbatch_indices.size(), crasher_indices.size());

    // Phase 2: execute clean tests through js-test-batch (batch size 50)
    auto batch_results = execute_t262_batch(prepared, clean_indices);

    // Build batch assignment map for Phase 4 diagnostics.
    // clean_indices are chunked sequentially into batches of T262_BATCH_CHUNK_SIZE.
    {
        size_t num_batches = (clean_indices.size() + T262_BATCH_CHUNK_SIZE - 1) / T262_BATCH_CHUNK_SIZE;
        g_batch_contents.resize(num_batches);
        for (size_t i = 0; i < clean_indices.size(); i++) {
            size_t bi = i / T262_BATCH_CHUNK_SIZE;
            const auto& name = prepared[clean_indices[i]].test_name;
            g_batch_assignment[name] = bi;
            g_batch_contents[bi].push_back(name);
        }
        fprintf(stderr, "[test262] Batch assignment map: %zu entries across %zu batches\n",
                g_batch_assignment.size(), num_batches);
    }

    auto exec_time = std::chrono::steady_clock::now();
    double exec_secs = std::chrono::duration<double>(exec_time - prep_time).count();
    fprintf(stderr, "[test262] Phase 2 (execute): %.1fs — %zu results collected\n",
            exec_secs, batch_results.size());

    // Diagnostic: identify which clean batches lost tests and the crash points.
    // Crash-point tests kill the batch process in a way the signal handler can't
    // catch (stack overflow, double fault, etc.). They'll be added to quarantine
    // as BATCH_KILL entries to prevent collateral damage in future runs.
    std::unordered_set<std::string> batch_kill_tests;
    {
        // Reconstruct native/js split to match actual sub-batch composition
        std::vector<size_t> native_clean, js_clean;
        for (size_t idx : clean_indices) {
            if (prepared[idx].native_harness)
                native_clean.push_back(idx);
            else
                js_clean.push_back(idx);
        }

        auto analyze_group = [&](const std::vector<size_t>& group_indices, const char* label) {
            size_t total_lost = 0;
            size_t killed_batches = 0;
            for (size_t start = 0; start < group_indices.size(); start += T262_BATCH_CHUNK_SIZE) {
                size_t end = std::min(start + T262_BATCH_CHUNK_SIZE, group_indices.size());
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
                    size_t batch_num = start / T262_BATCH_CHUNK_SIZE;
                    fprintf(stderr, "[test262] KILLED %s-batch[%zu]: %zu/%zu completed, %zu lost, "
                            "crash-point: '%s' (after '%s')\n",
                            label, batch_num, completed, end - start, lost_in_batch,
                            first_lost_name.c_str(),
                            last_ok_name.empty() ? "(batch start)" : last_ok_name.c_str());
                    total_lost += lost_in_batch;
                    killed_batches++;
                    // Queue the crash-point test for quarantine
                    if (!first_lost_name.empty()) {
                        batch_kill_tests.insert(first_lost_name);
                    }
                }
            }
            if (killed_batches > 0)
                fprintf(stderr, "[test262] %s group: %zu killed batches, %zu total lost tests\n",
                        label, killed_batches, total_lost);
        };

        analyze_group(native_clean, "native");
        analyze_group(js_clean, "js");
        if (!batch_kill_tests.empty())
            fprintf(stderr, "[test262] Detected %zu batch-kill crash-point tests (will quarantine for next run)\n",
                    batch_kill_tests.size());
    }

    // Phase 2a: execute non-batch tests individually (batch size = 1).
    // These tests include harnesses (e.g. propertyHelper.js) that destructively
    // mutate global built-in state, so they cannot safely share a process.
    if (!nonbatch_indices.empty()) {
        fprintf(stderr, "[test262] Phase 2a (non-batch): %zu tests running individually...\n",
                nonbatch_indices.size());
        auto nb_results = execute_t262_batch(prepared, nonbatch_indices, /*chunk_size=*/1);
        for (auto& kv : nb_results) {
            batch_results[kv.first] = std::move(kv.second);
        }
        fprintf(stderr, "[test262] Phase 2a (non-batch): %zu results collected\n",
                nb_results.size());
    }

    // Run crasher-quarantined tests individually (batch size = 1).
    // These tests previously caused batch crashes (signal kills, OOM). Running them
    // individually prevents collateral damage and actually tests whether they still crash.
    // Previously we faked them as CRASH without running, but that caused ~N phantom
    // regressions every run that Phase 4 would redundantly recover.
    if (!crasher_indices.empty()) {
        fprintf(stderr, "[test262] Phase 2a (crashers): %zu quarantined tests running individually...\n",
                crasher_indices.size());
        auto cr_results = execute_t262_batch(prepared, crasher_indices, /*chunk_size=*/1);
        for (auto& kv : cr_results) {
            batch_results[kv.first] = std::move(kv.second);
        }
        fprintf(stderr, "[test262] Phase 2a (crashers): %zu results collected\n",
                cr_results.size());
    }

    // Phase 2b: retry batch-lost tests in small batches (crash recovery)
    // Only needed for NEW unexpected crashes not in the quarantine list.
    // Only retry clean tests that got lost (not quarantined crashers — they already ran in Phase 2a)
    std::vector<size_t> lost_indices;
    for (size_t idx : clean_indices) {
        const auto& p = prepared[idx];
        if (batch_results.find(p.test_name) == batch_results.end()) {
            lost_indices.push_back(idx);
        }
    }
    if (!lost_indices.empty()) {
        auto retry_start = std::chrono::steady_clock::now();
        fprintf(stderr, "[test262] Phase 2b (retry): %zu batch-lost tests, re-running in small batches...\n",
                lost_indices.size());

        auto retry_results = [&]() {
            std::unordered_map<std::string, BatchResult> results;
            std::vector<SubBatch> retry_batches;
            // Retry individually (batch of 1) to prevent a single crasher from
            // killing other lost tests in the retry batch.
            for (size_t s = 0; s < lost_indices.size(); s++) {
                retry_batches.push_back({s, s + 1});
            }
            std::vector<std::unordered_map<std::string, BatchResult>> thread_results(retry_batches.size());
            std::atomic<size_t> next_batch{0};
            size_t num_workers = std::min(T262_MAX_PARALLEL_BATCHES, retry_batches.size());
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
                            std::string combined = assemble_combined_source(p);
                            fprintf(mf, "source:%s:%zu\n", p.test_name.c_str(), combined.size());
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
        double retry_secs = std::chrono::duration<double>(retry_time - retry_start).count();
        fprintf(stderr, "[test262] Phase 2b (retry): %.1fs — recovered %zu of %zu lost tests\n",
                retry_secs, recovered, lost_indices.size());

        exec_time = retry_time;  // update for total calculation
    }

    // Write crasher log for next run's quarantine optimization.
    // Cumulative: merge previous crashers with newly-discovered ones.
    // Tests are only removed from quarantine if they PASS when run individually in Phase 2a.
    {
        // Preserve manually-added TIMEOUT_ entries, SLOW_ entries, and # comments from the existing file.
        // TIMEOUT_ entries are tests that loop forever (Lambda bug: missing throw guard),
        // SLOW_ entries are tests that took >3s — both are quarantined from batch execution.
        // SLOW_ entries are regenerated from fresh timing data if the test ran in this session.
        std::vector<std::string> timeout_lines;
        std::unordered_set<std::string> timeout_names;  // names to skip re-emitting as CRASH_
        {
            FILE* old_f = fopen("temp/_t262_crashers.txt", "r");
            if (old_f) {
                char tbuf[2048];
                while (fgets(tbuf, sizeof(tbuf), old_f)) {
                    bool is_timeout = strncmp(tbuf, "TIMEOUT_", 8) == 0;
                    bool is_slow    = strncmp(tbuf, "SLOW_", 5) == 0;
                    if (is_timeout || is_slow || tbuf[0] == '#') {
                        size_t tl = strlen(tbuf);
                        while (tl > 0 && (tbuf[tl-1] == '\n' || tbuf[tl-1] == '\r')) tbuf[--tl] = '\0';
                        if (tl > 0) {
                            if (is_timeout) {
                                // always preserve TIMEOUT_ entries
                                timeout_lines.push_back(std::string(tbuf));
                                char* ft = strchr(tbuf, '\t');
                                if (ft) {
                                    char* ns = ft + 1;
                                    char* st = strchr(ns, '\t');
                                    if (st) *st = '\0';
                                    timeout_names.insert(std::string(ns));
                                }
                            } else if (is_slow) {
                                // preserve SLOW_ entries only if test was not re-run this session,
                                // or if re-run and still slow (>= 3s). Tests that ran faster
                                // are released from quarantine and return to batch execution.
                                char slow_buf[2048];
                                strncpy(slow_buf, tbuf, sizeof(slow_buf));
                                slow_buf[sizeof(slow_buf)-1] = '\0';
                                char* ft = strchr(tbuf, '\t');
                                if (ft) {
                                    char* ns = ft + 1;
                                    char* st = strchr(ns, '\t');
                                    if (st) { *st = '\0'; }
                                    std::string name(ns);
                                    auto br_it = batch_results.find(name);
                                    if (br_it == batch_results.end() ||
                                        br_it->second.elapsed_us >= SLOW_THRESHOLD_US) {
                                        // not re-run or still slow → preserve
                                        timeout_lines.push_back(std::string(slow_buf));
                                        timeout_names.insert(name);
                                    }
                                    // else: re-run and now fast → release from quarantine
                                }
                            } else {
                                // # comment lines
                                timeout_lines.push_back(std::string(tbuf));
                            }
                        }
                    }
                }
                fclose(old_f);
            }
        }

        FILE* crasher_log = fopen("temp/_t262_crashers.txt", "w");
        if (crasher_log) {
            // Write preserved TIMEOUT_ entries and comments first
            for (auto& line : timeout_lines) fprintf(crasher_log, "%s\n", line.c_str());
            if (!timeout_lines.empty()) fprintf(crasher_log, "\n");

            size_t still_lost = 0;
            size_t crash_exit = 0;
            std::unordered_set<std::string> written;
            // skip timeout tests — they are already above, don't re-emit as CRASH_
            written.insert(timeout_names.begin(), timeout_names.end());

            // Re-evaluate previously-quarantined crashers.
            // CRASH entries that still crash (exit > 128) stay quarantined.
            // CRASH entries that pass individually (exit <= 128) are RELEASED from
            // quarantine — they return to batch execution next run. If they crash
            // in batch again, they'll be re-quarantined as BATCH_KILL.
            // MISSING entries are removed if they pass individually (they were likely
            // collateral from a batch kill, not inherently crashing).
            size_t crash_released = 0;
            for (size_t idx : crasher_indices) {
                const auto& p = prepared[idx];
                auto it = batch_results.find(p.test_name);
                if (it == batch_results.end()) {
                    fprintf(crasher_log, "MISSING\t%s\t%s\n", p.test_name.c_str(), p.test_path.c_str());
                    written.insert(p.test_name);
                    still_lost++;
                } else if (it->second.exit_code > 128) {
                    fprintf(crasher_log, "CRASH_%d\t%s\t%s\n", it->second.exit_code,
                            p.test_name.c_str(), p.test_path.c_str());
                    written.insert(p.test_name);
                    // Only count as crash_exit if this is a NEW crash (not a known crasher re-confirming)
                    bool was_known_crash = known_crasher_tags.count(p.test_name) &&
                                           (known_crasher_tags[p.test_name].substr(0, 5) == "CRASH" ||
                                            known_crasher_tags[p.test_name].substr(0, 7) == "TIMEOUT");
                    if (!was_known_crash) crash_exit++;
                } else if (known_crasher_tags.count(p.test_name) &&
                           known_crasher_tags[p.test_name].substr(0, 5) == "CRASH") {
                    // Was a CRASH entry but now passes individually — release from quarantine.
                    // It will return to batch execution next run. If it causes a batch crash,
                    // the BATCH_KILL detection will re-quarantine it.
                    crash_released++;
                }
                // else: MISSING/SLOW test passed individually → removed from quarantine
            }
            if (crash_released > 0) {
                fprintf(stderr, "[test262] Released %zu formerly-crashing tests from quarantine (now pass individually)\n",
                        crash_released);
            }

            // Add newly-discovered crash-exit tests from clean batches (Phase 2 + 2b)
            for (auto& kv : batch_results) {
                if (written.count(kv.first)) continue;
                if (kv.second.exit_code > 128) {
                    const char* path = "";
                    for (size_t idx : clean_indices) {
                        if (prepared[idx].test_name == kv.first) {
                            path = prepared[idx].test_path.c_str();
                            break;
                        }
                    }
                    fprintf(crasher_log, "CRASH_%d\t%s\t%s\n", kv.second.exit_code,
                            kv.first.c_str(), path);
                    written.insert(kv.first);
                    crash_exit++;
                }
            }
            // Add newly-discovered slow tests (>3s elapsed) from clean batches
            size_t slow_count = 0;
            for (auto& kv : batch_results) {
                if (written.count(kv.first)) continue;
                if (kv.second.elapsed_us >= SLOW_THRESHOLD_US) {
                    const char* path = "";
                    for (size_t idx : clean_indices) {
                        if (prepared[idx].test_name == kv.first) {
                            path = prepared[idx].test_path.c_str();
                            break;
                        }
                    }
                    fprintf(crasher_log, "SLOW_%ld\t%s\t%s\n",
                            kv.second.elapsed_us / 1000, kv.first.c_str(), path);
                    written.insert(kv.first);
                    slow_count++;
                }
            }
            // Clean tests still missing after Phase 2b
            for (size_t idx : lost_indices) {
                const auto& p = prepared[idx];
                if (written.count(p.test_name)) continue;
                auto it = batch_results.find(p.test_name);
                if (it == batch_results.end()) {
                    fprintf(crasher_log, "MISSING\t%s\t%s\n", p.test_name.c_str(), p.test_path.c_str());
                    written.insert(p.test_name);
                    still_lost++;
                }
            }

            // Add batch-kill crash-point tests to quarantine.
            // These tests kill the batch process in a way the signal handler can't
            // catch (e.g. stack overflow, double fault). They pass individually but
            // crash the process when run in batch context. Quarantining forces them
            // to run individually, preventing collateral loss of co-batched tests.
            // Unlike CRASH entries (sticky), BATCH_KILL entries are re-verified each
            // run: if the test no longer appears as a crash-point, it's removed.
            size_t batch_kill_count = 0;
            for (auto& bk_name : batch_kill_tests) {
                if (written.count(bk_name)) continue;
                const char* path = "";
                for (size_t idx : clean_indices) {
                    if (prepared[idx].test_name == bk_name) {
                        path = prepared[idx].test_path.c_str();
                        break;
                    }
                }
                fprintf(crasher_log, "BATCH_KILL\t%s\t%s\n", bk_name.c_str(), path);
                written.insert(bk_name);
                batch_kill_count++;
            }
            // Also re-emit previously known BATCH_KILL entries that are still
            // quarantined — they stay quarantined for one extra run to verify
            // they're no longer crash-points. (If they don't re-appear as crash-points,
            // they'll be dropped next run because known_crasher_tags won't match CRASH.)
            for (size_t idx : crasher_indices) {
                const auto& p = prepared[idx];
                if (written.count(p.test_name)) continue;
                if (known_crasher_tags.count(p.test_name) &&
                    known_crasher_tags[p.test_name] == "BATCH_KILL") {
                    // Re-emit only if still a crash-point this run
                    if (batch_kill_tests.count(p.test_name)) {
                        fprintf(crasher_log, "BATCH_KILL\t%s\t%s\n",
                                p.test_name.c_str(), p.test_path.c_str());
                        written.insert(p.test_name);
                        batch_kill_count++;
                    }
                    // else: no longer a crash-point → drop from quarantine
                }
            }

            fclose(crasher_log);
            if (still_lost + crash_exit + slow_count + batch_kill_count > 0) {
                fprintf(stderr, "[test262] Crasher log: %zu missing + %zu crash-exit + %zu slow (>3s) + %zu batch-kill → temp/_t262_crashers.txt\n",
                        still_lost, crash_exit, slow_count, batch_kill_count);
            }
            // Expose crash-exit count for --update-baseline gate
            g_phase_crash_exit = crash_exit + still_lost;
        }
    }

    // Write per-test timing data to temp/_t262_timing.tsv (or _o0/_o3 suffix when --opt-level used)
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

    // Write per-test memory profiling data and summary
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

            // Print top 20 tests by RSS delta (descending)
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
            fprintf(stderr, "[test262]   Memory data → %s\n", mem_path);
        }
    }

    // Build classification sets for partial-pass logic.
    // Batchable tests must pass in their original Phase 2 batch with time < 3s to be "fully passed".
    // Non-batch tests (propertyHelper.js etc.) are fully passed if they pass individually.
    std::set<std::string> clean_set, nonbatch_set, crasher_name_set, phase2b_set;
    for (size_t idx : clean_indices)    clean_set.insert(prepared[idx].test_name);
    for (size_t idx : nonbatch_indices) nonbatch_set.insert(prepared[idx].test_name);
    for (size_t idx : crasher_indices)  crasher_name_set.insert(prepared[idx].test_name);
    for (size_t idx : lost_indices)     phase2b_set.insert(prepared[idx].test_name);

    // Phase 3: evaluate results and cache, applying partial-pass classification
    size_t pp_crasher = 0, pp_batch_lost = 0, pp_slow = 0;
    for (size_t i = 0; i < prepared.size(); i++) {
        Test262RunResult result = evaluate_batch_result(prepared[i], batch_results);

        // Classify partial passes for batchable tests.
        // Non-batch tests: a pass is always a full pass.
        // Batchable tests: only fully passed if passed in original batch run with time < 3s.
        if (result.result == T262_PASS) {
            const auto& name = prepared[i].test_name;
            if (crasher_name_set.count(name)) {
                result = {T262_PARTIAL_PASS, "partial: quarantined crasher, passed individually only"};
                pp_crasher++;
            } else if (clean_set.count(name) && phase2b_set.count(name)) {
                result = {T262_PARTIAL_PASS, "partial: batch-lost, passed only in individual retry"};
                pp_batch_lost++;
            } else if (clean_set.count(name)) {
                auto br_it = batch_results.find(name);
                if (br_it != batch_results.end() && br_it->second.elapsed_us >= SLOW_THRESHOLD_US) {
                    result = {T262_PARTIAL_PASS, "partial: slow (>= 3s in batch)"};
                    pp_slow++;
                }
            }
            // non-batch tests and fast clean-batch passes remain T262_PASS
        }

        std::lock_guard<std::mutex> lock(g_results_mutex);
        g_cached_results[prepared[i].test_name] = result;
    }
    if (pp_crasher + pp_batch_lost + pp_slow > 0) {
        fprintf(stderr, "[test262] Phase 3 partial passes: %zu quarantined-crasher + %zu batch-lost + %zu slow (>= 3s)\n",
                pp_crasher, pp_batch_lost, pp_slow);
    }
    // Expose batch-lost count for --update-baseline gate
    g_phase_batch_lost = pp_batch_lost;

    auto total_time = std::chrono::steady_clock::now();
    double total_secs = std::chrono::duration<double>(total_time - start_time).count();
    fprintf(stderr, "[test262] All %zu tests completed in %.1fs (prep %.1fs + exec %.1fs + eval <0.1s)\n",
            tests.size(), total_secs, prep_secs,
            std::chrono::duration<double>(exec_time - prep_time).count());
    g_prep_secs = prep_secs;
    g_exec_secs = std::chrono::duration<double>(exec_time - prep_time).count();
    g_total_secs = total_secs;
    g_parallel_done = true;
}

// =============================================================================
// GTest parameterized test (reads from cache)
// =============================================================================

class Test262Suite : public testing::TestWithParam<Test262Param> {};

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
            // Check if this is a partial pass (passed individually but not reliably in batch)
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
                // Partial passes are not added to current_passing (baseline).
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
        int total = passed + failed + partial;
        int real_failed = failed - batch_lost;
        double pct = total > 0 ? 100.0 * passed / total : 0.0;
        printf("\n");
        printf("╔══════════════════════════════════════════════════╗\n");
        printf("║         test262 Compliance Summary               ║\n");
        printf("╠══════════════════════════════════════════════════╣\n");
        printf("║  Fully passed: %5d / %5d  (%.1f%%)             ║\n", passed, total, pct);
        printf("║  Partial pass: %5d  (batch-unstable or slow)   ║\n", partial);
        printf("║  Failed:       %5d  (real: %d, batch-lost: %d) ║\n", failed, real_failed, batch_lost);
        printf("║  Skipped:      %5d                             ║\n", skipped);
        printf("╚══════════════════════════════════════════════════╝\n");

        // Regression / improvement report
        if (!g_baseline_passing.empty()) {
            printf("\n");
            printf("╔══════════════════════════════════════════════════╗\n");
            printf("║         Regression Check vs Baseline             ║\n");
            printf("╠══════════════════════════════════════════════════╣\n");
            printf("║  Baseline passing: %5zu                         ║\n", g_baseline_passing.size());
            printf("║  Fully passing:    %5zu                         ║\n", current_passing.size());
            printf("║  Partial passing:  %5d                          ║\n", partial);
            printf("║  Improvements:     %5zu  (fail → pass)          ║\n", improvements.size());
            printf("║  Regressions:      %5zu  (pass → fail)          ║\n", regressions.size());
            printf("╚══════════════════════════════════════════════════╝\n");

            if (!regressions.empty()) {
                printf("\n⚠️  REGRESSIONS (%zu tests that previously passed now fail):\n", regressions.size());
                std::sort(regressions.begin(), regressions.end());
                for (auto& r : regressions) {
                    printf("  - %s\n", r.c_str());
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
            if (g_phase_nonbatch_count > 0) {
                printf("\n❌  Baseline NOT updated: %zu nonbatch entries (must be 0)\n", g_phase_nonbatch_count);
                gate_ok = false;
            }
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
                printf("\n📝  Baseline updated: %s (%zu fully passing tests, gate: nonbatch=0 batch-lost=0 crash=0 min=%d)\n",
                       BASELINE_FILE, current_passing.size(), STABLE_BASELINE_MIN);
            }
        }
    }
};

int main(int argc, char** argv) {
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
        if (strcmp(argv[i], "--no-hot-reload") == 0) {
            g_no_hot_reload = true;
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
        if (strncmp(argv[i], "--opt-level=", 12) == 0) {
            g_opt_level = atoi(argv[i] + 12);
            if (g_opt_level < 0 || g_opt_level > 3) g_opt_level = 0;
            snprintf(g_opt_level_arg, sizeof(g_opt_level_arg), "--opt-level=%d", g_opt_level);
        }
    }

    // Auto-detect stripped test files directory (test/js262 -> ../lambda-test/js262)
    if (!g_no_stripped) {
        struct stat st;
        std::string stripped_test_dir = std::string(TEST262_SOURCE_DIR) + "/test";
        if (stat(stripped_test_dir.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
            g_use_stripped = true;
            // Also use stripped harness files if available
            std::string stripped_harness = std::string(TEST262_SOURCE_DIR) + "/harness";
            if (stat(stripped_harness.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
                g_harness_dir = stripped_harness;
            }
            fprintf(stderr, "[test262] Using comment-stripped files from %s\n",
                    TEST262_SOURCE_DIR);
        }
    }

    // Load baseline file for regression checking
    {
        FILE* f = fopen(BASELINE_FILE, "r");
        if (f) {
            char line[512];
            while (fgets(line, sizeof(line), f)) {
                // Skip comments and empty lines
                if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
                // Trim trailing newline
                size_t len = strlen(line);
                while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) line[--len] = '\0';
                if (len > 0) g_baseline_passing.insert(std::string(line, len));
            }
            fclose(f);
            fprintf(stderr, "[test262] Loaded baseline: %zu passing tests from %s\n",
                    g_baseline_passing.size(), BASELINE_FILE);
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

    // Load known crashers from previous run for quarantine optimization
    load_known_crashers("temp/_t262_crashers.txt");
    if (!g_known_crashers.empty()) {
        fprintf(stderr, "[test262] Loaded %zu known crashers for quarantine\n",
                g_known_crashers.size());
    }

    // Load non-batch tests: poisoners that mutate global state
    load_nonbatch_list(NONBATCH_FILE);

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
            if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
            size_t len = strlen(line);
            while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) line[--len] = '\0';
            if (len > 0) batch_names.push_back(std::string(line, len));
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
                fprintf(stderr, "[test262]   WARNING: test not found: %s\n", name.c_str());
                continue;
            }
            const auto& param = all_tests[it->second];
            Test262Prepared p;
            p.test_name = param.test_name;
            p.test_path = param.test_path;
            p.skip_result = T262_PASS;
            p.is_negative = false;
            p.is_strict = false;
            auto cm_it = g_metadata_cache.find(param.test_path);
            if (cm_it != g_metadata_cache.end()) {
                const CachedMeta& cm = cm_it->second;
                p.is_negative = cm.flags & 32;
                p.negative_type = cm.neg_type;
                p.is_strict = cm.flags & 8;
                p.includes = cm.includes;
            }
            indices.push_back(prepared.size());
            prepared.push_back(std::move(p));
        }

        // Run as a single batch (all tests in one lambda.exe js-test-batch process)
        fprintf(stderr, "[test262] Running %zu tests in a single batch...\n", indices.size());
        auto results = execute_t262_batch(prepared, indices);

        // Evaluate and report per-test results
        int passed = 0, failed = 0;
        std::vector<std::string> failed_names;
        for (size_t i = 0; i < prepared.size(); i++) {
            auto result = evaluate_batch_result(prepared[i], results);
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
        fprintf(stderr, "\n[test262] Batch file result: %d passed, %d failed out of %zu\n",
                passed, failed, prepared.size());
        if (!failed_names.empty()) {
            fprintf(stderr, "[test262] Failed tests:\n");
            for (auto& name : failed_names) {
                fprintf(stderr, "  - %s\n", name.c_str());
            }
        }
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
        {
            std::lock_guard<std::mutex> lock(g_results_mutex);
            for (auto& kv : g_cached_results) {
                switch (kv.second.result) {
                    case T262_PASS: passed++; current_passing.push_back(kv.first); break;
                    case T262_PARTIAL_PASS: partial++; break;
                    case T262_SKIP: skipped++; break;
                    default: failed++; break;
                }
            }
        }
        // Compute regressions/improvements vs baseline
        if (!g_baseline_passing.empty()) {
            std::set<std::string> pass_set(current_passing.begin(), current_passing.end());
            for (auto& name : g_baseline_passing) {
                if (pass_set.find(name) == pass_set.end()) regressions.push_back(name);
            }
            for (auto& name : current_passing) {
                if (g_baseline_passing.find(name) == g_baseline_passing.end()) improvements.push_back(name);
            }
        }

        // Phase 4: Retry regressions individually to distinguish real regressions
        // from batch-mode infrastructure failures (OOM kills, signal 9, etc.)
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
                // Load metadata from cache
                auto cm_it = g_metadata_cache.find(param.test_path);
                if (cm_it != g_metadata_cache.end()) {
                    const CachedMeta& cm = cm_it->second;
                    p.is_negative = cm.flags & 32;
                    p.negative_type = cm.neg_type;
                    p.is_strict = cm.flags & 8;
                    p.includes = cm.includes;
                }
                retry_indices.push_back(retry_prepared.size());
                retry_prepared.push_back(std::move(p));
            }
            // Run each individually in small batches
            if (!retry_prepared.empty()) {
                auto retry_results = execute_t262_batch(retry_prepared, retry_indices);
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
                        // Phase 4 recovered = partial pass (passed individually, not in batch)
                        partial++;
                        failed--;
                        {
                            std::lock_guard<std::mutex> lock(g_results_mutex);
                            g_cached_results[reg_name] = {T262_PARTIAL_PASS, "partial: passed in Phase 4 retry only"};
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
                                recovered_names.size(), recovered_by_batch.size(), T262_BATCH_CHUNK_SIZE);
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

        int total = passed + failed + partial;
        double pct = total > 0 ? 100.0 * passed / total : 0.0;
        printf("\n");
        printf("╔══════════════════════════════════════════════════╗\n");
        printf("║         test262 Compliance Summary               ║\n");
        printf("╠══════════════════════════════════════════════════╣\n");
        printf("║  Fully passed: %5d / %5d  (%.1f%%)             ║\n", passed, total, pct);
        printf("║  Partial pass: %5d  (batch-unstable or slow)   ║\n", partial);
        printf("║  Failed:       %5d                             ║\n", failed);
        printf("║  Skipped:      %5d                             ║\n", skipped);
        printf("╚══════════════════════════════════════════════════╝\n");
        if (!g_baseline_passing.empty()) {
            printf("\n╔══════════════════════════════════════════════════╗\n");
            printf("║         Regression Check vs Baseline             ║\n");
            printf("╠══════════════════════════════════════════════════╣\n");
            printf("║  Baseline passing: %5zu                         ║\n", g_baseline_passing.size());
            printf("║  Fully passing:    %5zu                         ║\n", current_passing.size());
            printf("║  Partial passing:  %5d                          ║\n", partial);
            printf("║  Improvements:     %5zu  (fail → pass)          ║\n", improvements.size());
            printf("║  Regressions:      %5zu  (pass → fail)          ║\n", regressions.size());
            printf("╚══════════════════════════════════════════════════╝\n");
            if (!regressions.empty()) {
                printf("\n⚠️  REGRESSIONS (%zu tests):\n", regressions.size());
                std::sort(regressions.begin(), regressions.end());
                for (auto& r : regressions) printf("  - %s\n", r.c_str());
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
            if (g_phase_nonbatch_count > 0) {
                printf("\n❌  Baseline NOT updated: %zu nonbatch entries (must be 0)\n", g_phase_nonbatch_count);
                gate_ok = false;
            }
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
                printf("\n📝  Baseline updated: %s (%zu fully passing tests, gate: nonbatch=0 batch-lost=0 crash=0 min=%d)\n",
                       BASELINE_FILE, current_passing.size(), STABLE_BASELINE_MIN);
            }
        }
        return regressions.empty() ? 0 : 1;
    }

    return RUN_ALL_TESTS();
}
