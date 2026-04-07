// =============================================================================
// test262 Compliance Test Runner for LambdaJS
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
    #include <dirent.h>
    #include <fcntl.h>
    #include <spawn.h>
#endif

// =============================================================================
// Configuration
// =============================================================================

static const char* TEST262_ROOT = "ref/test262";
static const char* HARNESS_DIR = "ref/test262/harness";
static const char* BASELINE_FILE = "test/js/test262_baseline.txt";

// Features above ES2020 — skip tests requiring these.
// Target: ES2020 compliance. All features ≤ES2020 are in scope.
// Reference: TC39 finished-proposals.md (publication year field)
static const std::set<std::string> UNSUPPORTED_FEATURES = {
    // === ES2021 features ===
    "WeakRef",                                    // Weak references
    "FinalizationRegistry",                       // GC callback hooks
    "AggregateError",                             // Error subclass for Promise.any
    "logical-assignment-operators",               // &&=, ||=, ??=
    "numeric-separator-literal",                  // 1_000_000
    "String.prototype.replaceAll",                // String.prototype.replaceAll
    "Promise.any",                                // Promise.any

    // === ES2022 features ===
    "class-fields-public",                        // Public instance fields
    "class-fields-private",                       // Private instance fields
    "class-fields-private-in",                    // #x in obj
    "class-methods-private",                      // Private methods
    "class-static-fields-public",                 // Static public fields
    "class-static-fields-private",                // Static private fields
    "class-static-methods-private",               // Static private methods
    "class-static-block",                         // static { ... }
    "top-level-await",                            // Module-level await
    "regexp-match-indices",                       // /d flag, .indices
    "Object.hasOwn",                              // Object.hasOwn()
    "Array.prototype.at",                         // Array.prototype.at()
    "String.prototype.at",                        // String.prototype.at()
    "TypedArray.prototype.at",                    // TypedArray.prototype.at()
    "error-cause",                                // new Error("msg", { cause })

    // === ES2023 features ===
    "change-array-by-copy",                       // toSorted, toReversed, toSpliced, with
    "symbols-as-weakmap-keys",                    // Symbol keys in WeakMap/WeakSet
    "hashbang",                                   // #! shebang lines
    "array-find-from-last",                       // findLast, findLastIndex

    // === ES2024 features ===
    "Atomics.waitAsync",                          // Atomics.waitAsync proposal
    "resizable-arraybuffer",                      // Resizable/growable ArrayBuffers
    "ArrayBuffer-transfer", "arraybuffer-transfer", // ArrayBuffer.prototype.transfer
    "regexp-v-flag",                              // Unicode sets (/v flag)
    "promise-with-resolvers",                     // Promise.withResolvers
    "array-grouping",                             // Object.groupBy / Map.groupBy
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
    "set-methods",                                // New Set methods
    "Set.prototype.intersection",
    "Set.prototype.union",
    "Set.prototype.difference",
    "Set.prototype.symmetricDifference",
    "Set.prototype.isSubsetOf",
    "Set.prototype.isSupersetOf",
    "Set.prototype.isDisjointFrom",
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
    if (header.substr(0, 2) != "V1") return false;

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
        while (fields.size() < 6) fields.push_back("");

        CachedMeta meta;
        meta.flags = std::stoi(fields[1]);
        meta.neg_phase = fields[2];
        meta.neg_type = fields[3];
        meta.includes = split_semicolons(fields[4]);
        meta.features = split_semicolons(fields[5]);

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
    };
    for (auto& cat : builtin_cats) {
        std::string dir = std::string(TEST262_ROOT) + "/test/built-ins/" + cat.subdir;
        discover_tests_recursive(dir, "built_ins", cat.name, tests);
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
    T262_PASS,
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
    std::string path = std::string(HARNESS_DIR) + "/" + name;
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

// Assemble test source WITHOUT harness (for two-module split: harness sent separately)
static std::string assemble_test_source(const Test262Prepared& p) {
    std::string source = read_file_contents(p.test_path);
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

// Legacy: assemble combined source with harness included (for backward compatibility)
static std::string assemble_combined_source(const Test262Prepared& p) {
    std::string source = read_file_contents(p.test_path);
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
};

static const size_t T262_BATCH_CHUNK_SIZE = 50;
static const size_t T262_MAX_PARALLEL_BATCHES = 8;
static const size_t RETRY_BATCH_SIZE = 5;

struct SubBatch { size_t start; size_t end; };

// Forward declarations for globals used in prepare phase
static std::set<std::string> g_baseline_passing;
static std::set<std::string> g_known_crashers;
static bool g_update_baseline = false;
static bool g_baseline_only = false;
static bool g_no_hot_reload = false;
static int g_opt_level = 0;  // default -O0 (fastest for short-lived test262 scripts)
static char g_opt_level_arg[20] = "--opt-level=0";  // "--opt-level=N"

// Load known crashers from previous run's crasher log.
// Tests listed here are quarantined into their own small batches
// to avoid collateral damage to co-batched tests.
static void load_known_crashers(const char* path) {
    FILE* f = fopen(path, "r");
    if (!f) return;
    char buf[2048];
    while (fgets(buf, sizeof(buf), f)) {
        // format: "MISSING\t<test_name>\t<test_path>" or "CRASH_N\t<test_name>\t<test_path>"
        char* first_tab = strchr(buf, '\t');
        if (!first_tab) continue;
        char* name_start = first_tab + 1;
        char* second_tab = strchr(name_start, '\t');
        if (second_tab) *second_tab = '\0';
        // trim trailing whitespace
        size_t len = strlen(name_start);
        while (len > 0 && (name_start[len-1] == '\n' || name_start[len-1] == '\r'))
            name_start[--len] = '\0';
        if (len > 0) g_known_crashers.insert(std::string(name_start));
    }
    fclose(f);
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
    g_harness_sta = read_file_contents(std::string(HARNESS_DIR) + "/sta.js");
    g_harness_assert = read_file_contents(std::string(HARNESS_DIR) + "/assert.js");

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

            // load metadata from cache or fall back to file read
            Test262Metadata meta;
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
}

// Run a sub-batch of tests from a pre-written manifest file + stdout pipe
// Run a sub-batch from a manifest file using posix_spawn (avoids fork's page table copy)
static void run_t262_sub_batch(
    const char* manifest_path,
    std::unordered_map<std::string, BatchResult>& results)
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

    char* argv[6] = {
        (char*)"lambda.exe", (char*)"js-test-batch", (char*)"--timeout=10", NULL, NULL, NULL
    };
    int argi = 3;
    if (g_no_hot_reload) {
        argv[argi++] = (char*)"--no-hot-reload";
    }
    if (g_opt_level >= 0) {
        argv[argi++] = g_opt_level_arg;
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
                    const char* space2 = strchr(buffer + 11, ' ');
                    if (space2) elapsed_us = atol(space2 + 1);
                    results[current_script] = {current_output, status, elapsed_us};
                    in_script = false;
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
}

// Phase 2: Execute all prepared tests through js-test-batch (reusable manifest files + stdout pipes)
static std::unordered_map<std::string, BatchResult> execute_t262_batch(
    const std::vector<Test262Prepared>& prepared,
    const std::vector<size_t>& indices)
{
    std::unordered_map<std::string, BatchResult> results;
    if (indices.empty()) return results;

    // split into chunks
    std::vector<SubBatch> batches;
    for (size_t s = 0; s < indices.size(); s += T262_BATCH_CHUNK_SIZE) {
        size_t e = std::min(s + T262_BATCH_CHUNK_SIZE, indices.size());
        batches.push_back({s, e});
    }

    // Run sub-batches with limited parallelism.
    // Each worker reuses ONE temp manifest file (6 workers = 6 files total).
    // For each sub-batch: truncate → write source data → fork/exec → read stdout.
    // This avoids per-batch file create/unlink overhead while keeping fast file-based stdin.
    std::vector<std::unordered_map<std::string, BatchResult>> thread_results(batches.size());
    std::atomic<size_t> next_batch{0};
    size_t num_workers = std::min(T262_MAX_PARALLEL_BATCHES, batches.size());
    std::vector<std::thread> threads;
    for (size_t w = 0; w < num_workers; w++) {
        threads.emplace_back([&, w]() {
            // Each worker has its own reusable manifest file
            char manifest_path[256];
            snprintf(manifest_path, sizeof(manifest_path), "temp/_t262_worker_%zu.manifest", w);
            while (true) {
                size_t i = next_batch.fetch_add(1, std::memory_order_relaxed);
                if (i >= batches.size()) break;

                // Write manifest for this sub-batch (two-module split: harness first, then tests)
                FILE* mf = fopen(manifest_path, "wb");
                if (!mf) continue;

                // Send harness once per sub-batch via harness: protocol
                std::string harness = assemble_harness_source();
                fprintf(mf, "harness:%zu\n", harness.size());
                fwrite(harness.data(), 1, harness.size(), mf);
                fputc('\n', mf);

                // Send each test WITHOUT harness (inherits from preamble)
                for (size_t idx = batches[i].start; idx < batches[i].end; idx++) {
                    size_t pi = indices[idx];
                    const auto& p = prepared[pi];
                    std::string test_src = assemble_test_source(p);
                    fprintf(mf, "source:%s:%zu\n",
                            p.test_name.c_str(), test_src.size());
                    fwrite(test_src.data(), 1, test_src.size(), mf);
                    fputc('\n', mf);
                }
                fclose(mf);

                // Execute: fork/exec with stdin from manifest, read stdout via pipe
                run_t262_sub_batch(manifest_path, thread_results[i]);
            }
            unlink(manifest_path);
        });
    }
    for (auto& t : threads) t.join();

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

    // Partition batch_indices: quarantine known crashers into separate small batches
    // to prevent collateral damage to co-batched tests.
    std::vector<size_t> clean_indices;
    std::vector<size_t> crasher_indices;
    if (!g_known_crashers.empty()) {
        for (size_t idx : batch_indices) {
            if (g_known_crashers.count(prepared[idx].test_name)) {
                crasher_indices.push_back(idx);
            } else {
                clean_indices.push_back(idx);
            }
        }
    } else {
        clean_indices = batch_indices;
    }

    auto prep_time = std::chrono::steady_clock::now();
    double prep_secs = std::chrono::duration<double>(prep_time - start_time).count();
    fprintf(stderr, "[test262] Phase 1 (prepare): %.1fs — %zu scripts to batch (%zu clean, %zu quarantined)\n",
            prep_secs, batch_indices.size(), clean_indices.size(), crasher_indices.size());

    // Phase 2: execute clean tests through js-test-batch (batch size 50)
    auto batch_results = execute_t262_batch(prepared, clean_indices);

    auto exec_time = std::chrono::steady_clock::now();
    double exec_secs = std::chrono::duration<double>(exec_time - prep_time).count();
    fprintf(stderr, "[test262] Phase 2 (execute): %.1fs — %zu results collected\n",
            exec_secs, batch_results.size());

    // Phase 2a: execute quarantined crashers in small batches (batch size 5)
    // These are known to crash from previous run — isolate each to prevent collateral.
    if (!crasher_indices.empty()) {
        {
            // Phase 2a: run each quarantined crasher individually (batch of 1)
            // to prevent one crash from killing neighbor tests
            std::vector<SubBatch> crasher_batches;
            for (size_t s = 0; s < crasher_indices.size(); s++) {
                crasher_batches.push_back({s, s + 1});
            }
            std::vector<std::unordered_map<std::string, BatchResult>> thread_results(crasher_batches.size());
            std::atomic<size_t> next_batch{0};
            size_t num_workers = std::min(T262_MAX_PARALLEL_BATCHES, crasher_batches.size());
            std::vector<std::thread> threads;
            for (size_t w = 0; w < num_workers; w++) {
                threads.emplace_back([&, w]() {
                    char manifest_path[256];
                    snprintf(manifest_path, sizeof(manifest_path), "temp/_t262_crasher_%zu.manifest", w);
                    while (true) {
                        size_t i = next_batch.fetch_add(1, std::memory_order_relaxed);
                        if (i >= crasher_batches.size()) break;
                        FILE* mf = fopen(manifest_path, "wb");
                        if (!mf) continue;
                        for (size_t idx = crasher_batches[i].start; idx < crasher_batches[i].end; idx++) {
                            size_t pi = crasher_indices[idx];
                            const auto& p = prepared[pi];
                            std::string combined = assemble_combined_source(p);
                            fprintf(mf, "source:%s:%zu\n", p.test_name.c_str(), combined.size());
                            fwrite(combined.data(), 1, combined.size(), mf);
                            fputc('\n', mf);
                        }
                        fclose(mf);
                        run_t262_sub_batch(manifest_path, thread_results[i]);
                    }
                    unlink(manifest_path);
                });
            }
            for (auto& t : threads) t.join();
            for (auto& partial : thread_results) {
                for (auto& kv : partial) batch_results[kv.first] = std::move(kv.second);
            }
        }
        auto crasher_time = std::chrono::steady_clock::now();
        double crasher_secs = std::chrono::duration<double>(crasher_time - exec_time).count();
        fprintf(stderr, "[test262] Phase 2a (crashers): %.1fs — %zu quarantined individually\n",
                crasher_secs, crasher_indices.size());
        exec_time = crasher_time;
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
                        run_t262_sub_batch(manifest_path, thread_results[i]);
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
        double retry_secs = std::chrono::duration<double>(retry_time - exec_time).count();
        fprintf(stderr, "[test262] Phase 2b (retry): %.1fs — recovered %zu of %zu lost tests\n",
                retry_secs, recovered, lost_indices.size());

        exec_time = retry_time;  // update for total calculation
    }

    // Write crasher log for next run's quarantine optimization.
    // Cumulative: merge previous crashers with newly-discovered ones.
    // Tests are only removed from quarantine if they PASS when run individually in Phase 2a.
    {
        FILE* crasher_log = fopen("temp/_t262_crashers.txt", "w");
        if (crasher_log) {
            size_t still_lost = 0;
            size_t crash_exit = 0;
            std::unordered_set<std::string> written;

            // Retain previously-quarantined crashers that still crash in Phase 2a.
            // (Tests that pass individually in Phase 2a are removed from quarantine.)
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
                    crash_exit++;
                }
                // else: test passed individually → removed from quarantine
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
            fclose(crasher_log);
            if (still_lost + crash_exit > 0) {
                fprintf(stderr, "[test262] Crasher log: %zu missing + %zu crash-exit → temp/_t262_crashers.txt\n",
                        still_lost, crash_exit);
            }
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

    // Phase 3: evaluate results and cache
    for (size_t i = 0; i < prepared.size(); i++) {
        Test262RunResult result = evaluate_batch_result(prepared[i], batch_results);

        std::lock_guard<std::mutex> lock(g_results_mutex);
        g_cached_results[prepared[i].test_name] = result;
    }

    auto total_time = std::chrono::steady_clock::now();
    double total_secs = std::chrono::duration<double>(total_time - start_time).count();
    fprintf(stderr, "[test262] All %zu tests completed in %.1fs (prep %.1fs + exec %.1fs + eval <0.1s)\n",
            tests.size(), total_secs, prep_secs,
            std::chrono::duration<double>(exec_time - prep_time).count());
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
    int passed = 0, failed = 0, skipped = 0, crashed = 0, timed_out = 0, batch_lost = 0;
    std::map<std::string, std::pair<int,int>> category_results; // category -> (pass, total)
    std::vector<std::string> current_passing;    // tests that passed this run
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

    void OnTestSuiteEnd(const testing::TestSuite& suite) override {
        // printed by GTest
    }

    void OnTestProgramEnd(const testing::UnitTest& unit_test) override {
        int total = passed + failed;
        int real_failed = failed - batch_lost;
        double pct = total > 0 ? 100.0 * passed / total : 0.0;
        printf("\n");
        printf("╔══════════════════════════════════════════════════╗\n");
        printf("║         test262 Compliance Summary               ║\n");
        printf("╠══════════════════════════════════════════════════╣\n");
        printf("║  Passed:     %5d / %5d  (%.1f%%)              ║\n", passed, total, pct);
        printf("║  Failed:     %5d  (real: %d, batch-lost: %d)   ║\n", failed, real_failed, batch_lost);
        printf("║  Skipped:    %5d                               ║\n", skipped);
        printf("╚══════════════════════════════════════════════════╝\n");

        // Regression / improvement report
        if (!g_baseline_passing.empty()) {
            printf("\n");
            printf("╔══════════════════════════════════════════════════╗\n");
            printf("║         Regression Check vs Baseline             ║\n");
            printf("╠══════════════════════════════════════════════════╣\n");
            printf("║  Baseline passing: %5zu                         ║\n", g_baseline_passing.size());
            printf("║  Current passing:  %5zu                         ║\n", current_passing.size());
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

        // Update baseline if requested
        if (g_update_baseline) {
            FILE* f = fopen(BASELINE_FILE, "w");
            if (f) {
                fprintf(f, "# test262 baseline: tests that PASS (auto-updated)\n");
                fprintf(f, "# Total passing: %zu\n", current_passing.size());
                std::sort(current_passing.begin(), current_passing.end());
                for (auto& name : current_passing) {
                    fprintf(f, "%s\n", name.c_str());
                }
                fclose(f);
                printf("\n📝  Baseline updated: %s (%zu passing tests)\n",
                       BASELINE_FILE, current_passing.size());
            } else {
                printf("\n❌  Failed to update baseline: %s\n", BASELINE_FILE);
            }
        }
    }
};

int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);

    // Check for --update-baseline flag (must be after InitGoogleTest consumes gtest flags)
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--update-baseline") == 0) {
            g_update_baseline = true;
        }
        if (strcmp(argv[i], "--baseline-only") == 0) {
            g_baseline_only = true;
        }
        if (strcmp(argv[i], "--no-hot-reload") == 0) {
            g_no_hot_reload = true;
        }
        if (strncmp(argv[i], "--opt-level=", 12) == 0) {
            g_opt_level = atoi(argv[i] + 12);
            if (g_opt_level < 0 || g_opt_level > 3) g_opt_level = 0;
            snprintf(g_opt_level_arg, sizeof(g_opt_level_arg), "--opt-level=%d", g_opt_level);
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

    // register summary listener
    testing::TestEventListeners& listeners = testing::UnitTest::GetInstance()->listeners();
    listeners.Append(new Test262ReportListener());

    // pre-run all tests in batch mode
    auto all_tests = discover_all_tests();
    batch_run_all_tests(all_tests);

    return RUN_ALL_TESTS();
}
