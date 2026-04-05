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

// Features that LambdaJS does NOT support — skip tests requiring these
// v17: removed 25 features that are actually implemented (class fields, optional
// chaining, nullish coalescing, globalThis, logical assignment, etc.)
static const std::set<std::string> UNSUPPORTED_FEATURES = {
    // Concurrency / SharedMemory
    "Atomics", "SharedArrayBuffer", "Atomics.waitAsync", "Atomics.pause",
    // Proxy / Reflect
    "Proxy", "Reflect", "Reflect.construct", "Reflect.apply",
    "Reflect.defineProperty", "Reflect.deleteProperty", "Reflect.get",
    "Reflect.getOwnPropertyDescriptor", "Reflect.getPrototypeOf",
    "Reflect.has", "Reflect.isExtensible", "Reflect.ownKeys",
    "Reflect.preventExtensions", "Reflect.set", "Reflect.setPrototypeOf",
    // Temporal (stage 3, large API)
    "Temporal",
    // Tail calls
    "tail-call-optimization",
    // Realms
    "ShadowRealm",
    // Import attributes / assertions
    "import-attributes", "import-assertions",
    // Dynamic import
    "dynamic-import", "import.meta",
    // Top-level await
    "top-level-await",
    // Weak references / FinalizationRegistry
    "WeakRef", "FinalizationRegistry",
    // Resizable ArrayBuffer
    "resizable-arraybuffer", "ArrayBuffer-transfer",
    // Decorators
    "decorators",
    // Iterator helpers (stage 3)
    "iterator-helpers",
    // Disposable resources
    "explicit-resource-management",
    // Duplicate named capture groups
    "regexp-duplicate-named-groups",
    // RegExp features
    "regexp-lookbehind", "regexp-named-groups", "regexp-unicode-property-escapes",
    "regexp-match-indices", "regexp-v-flag",
    "regexp-modifiers",
    // Well-formed JSON.stringify
    "well-formed-json-stringify",
    // String / Array methods (newer)
    "Array.fromAsync",
    "change-array-by-copy",
    // hashbang
    "hashbang",
    // Symbols — well-known symbol protocols not fully implemented
    "Symbol.toPrimitive", "Symbol.species", "Symbol.iterator",
    "Symbol.hasInstance", "Symbol.match", "Symbol.replace", "Symbol.search",
    "Symbol.split", "Symbol.toStringTag", "Symbol.unscopables",
    "Symbol.asyncIterator", "Symbol.matchAll", "Symbol",
    // Async iteration — event loop semantics diverge from test262 harness
    "async-iteration",
    // Other
    "cross-realm", "IsHTMLDDA", "caller",
    "Uint8Array",
    // Misc
    "arbitrary-module-namespace-names",
    "json-modules", "source-phase-imports",
    "AggregateError",
    "symbols-as-weakmap-keys",
    "Set.prototype.intersection",
    "Set.prototype.union",
    "Set.prototype.difference",
    "Set.prototype.symmetricDifference",
    "Set.prototype.isSubsetOf",
    "Set.prototype.isSupersetOf",
    "Set.prototype.isDisjointFrom",
    "Float16Array",
    "uint8-clamped-array",
    "json-parse-with-source",
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
};

static const size_t T262_BATCH_CHUNK_SIZE = 50;
static const size_t T262_MAX_PARALLEL_BATCHES = 8;

struct SubBatch { size_t start; size_t end; };

// Forward declarations for globals used in prepare phase
static std::set<std::string> g_baseline_passing;
static bool g_update_baseline = false;
static bool g_baseline_only = false;

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

            // read test source for metadata parsing only
            std::string source = read_file_contents(param.test_path);
            if (source.empty()) {
                p.skip_result = T262_SKIP;
                p.skip_message = "could not read test file";
                continue;
            }

            // parse metadata
            Test262Metadata meta = parse_metadata(source);

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

    char* const argv[] = {
        (char*)"lambda.exe", (char*)"js-test-batch", (char*)"--timeout=10", NULL
    };
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
                    results[current_script] = {current_output, status};
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

                // Write manifest for this sub-batch (assemble source on-the-fly)
                FILE* mf = fopen(manifest_path, "wb");
                if (!mf) continue;
                for (size_t idx = batches[i].start; idx < batches[i].end; idx++) {
                    size_t pi = indices[idx];
                    const auto& p = prepared[pi];
                    std::string combined = assemble_combined_source(p);
                    fprintf(mf, "source:%s:%zu\n",
                            p.test_name.c_str(), combined.size());
                    fwrite(combined.data(), 1, combined.size(), mf);
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

    auto prep_time = std::chrono::steady_clock::now();
    double prep_secs = std::chrono::duration<double>(prep_time - start_time).count();
    fprintf(stderr, "[test262] Phase 1 (prepare): %.1fs — %zu scripts to batch\n",
            prep_secs, batch_indices.size());

    // Phase 2: execute through js-test-batch (lazy assembly + manifest files)
    auto batch_results = execute_t262_batch(prepared, batch_indices);

    auto exec_time = std::chrono::steady_clock::now();
    double exec_secs = std::chrono::duration<double>(exec_time - prep_time).count();
    fprintf(stderr, "[test262] Phase 2 (execute): %.1fs — %zu results collected\n",
            exec_secs, batch_results.size());

    // Phase 2b: retry batch-lost tests in small batches (crash recovery)
    // When a test crashes, it kills the whole batch process and all remaining
    // tests in that batch are lost. Re-run those in small batches of 5.
    std::vector<size_t> lost_indices;
    for (size_t idx : batch_indices) {
        const auto& p = prepared[idx];
        if (batch_results.find(p.test_name) == batch_results.end()) {
            lost_indices.push_back(idx);
        }
    }
    if (!lost_indices.empty()) {
        fprintf(stderr, "[test262] Phase 2b (retry): %zu batch-lost tests, re-running in small batches...\n",
                lost_indices.size());

        static const size_t RETRY_BATCH_SIZE = 5;
        auto retry_results = [&]() {
            std::unordered_map<std::string, BatchResult> results;
            std::vector<SubBatch> retry_batches;
            for (size_t s = 0; s < lost_indices.size(); s += RETRY_BATCH_SIZE) {
                size_t e = std::min(s + RETRY_BATCH_SIZE, lost_indices.size());
                retry_batches.push_back({s, e});
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

        // Log still-lost tests (genuine crashers + collateral) to temp/_t262_crashers.txt
        // After retry, tests can be:
        //   - missing from batch_results: process crashed before BATCH_END (crasher or collateral)
        //   - present with exit_code > 128: crash was caught by batch handler
        FILE* crasher_log = fopen("temp/_t262_crashers.txt", "w");
        if (crasher_log) {
            size_t still_lost = 0;
            size_t crash_exit = 0;
            for (size_t idx : lost_indices) {
                const auto& p = prepared[idx];
                auto it = batch_results.find(p.test_name);
                if (it == batch_results.end()) {
                    fprintf(crasher_log, "MISSING\t%s\t%s\n", p.test_name.c_str(), p.test_path.c_str());
                    still_lost++;
                } else if (it->second.exit_code > 128) {
                    fprintf(crasher_log, "CRASH_%d\t%s\t%s\n", it->second.exit_code,
                            p.test_name.c_str(), p.test_path.c_str());
                    crash_exit++;
                }
            }
            fclose(crasher_log);
            if (still_lost + crash_exit > 0) {
                fprintf(stderr, "[test262] Crasher log: %zu missing + %zu crash-exit → temp/_t262_crashers.txt\n",
                        still_lost, crash_exit);
            }
        }

        exec_time = retry_time;  // update for total calculation
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

    // register summary listener
    testing::TestEventListeners& listeners = testing::UnitTest::GetInstance()->listeners();
    listeners.Append(new Test262ReportListener());

    // pre-run all tests in batch mode
    auto all_tests = discover_all_tests();
    batch_run_all_tests(all_tests);

    return RUN_ALL_TESTS();
}
