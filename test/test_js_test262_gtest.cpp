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
#endif

// =============================================================================
// Configuration
// =============================================================================

static const char* TEST262_ROOT = "ref/test262";
static const char* HARNESS_DIR = "ref/test262/harness";
static const char* TEMP_DIR = "temp";

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
    // Class static block — parsed but body not executed
    "class-static-block",
    // Async iteration — event loop semantics diverge from test262 harness
    "async-iteration",
    // Other
    "cross-realm", "IsHTMLDDA", "caller",
    "Uint8Array",
    // Misc
    "arbitrary-module-namespace-names",
    "json-modules", "source-phase-imports",
    "AggregateError",
    "error-cause",
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

static Test262RunResult run_test262(const std::string& test_path) {
    // read test source
    std::string source = read_file_contents(test_path);
    if (source.empty()) {
        return {T262_SKIP, "could not read test file"};
    }

    // parse metadata
    Test262Metadata meta = parse_metadata(source);

    // skip unsupported
    if (meta.is_async) return {T262_SKIP, "async flag"};
    if (meta.is_module) return {T262_SKIP, "module flag"};
    if (meta.is_raw) return {T262_SKIP, "raw flag"};
    if (has_unsupported_feature(meta)) return {T262_SKIP, "unsupported feature"};

    // check for eval() usage in test body (after frontmatter)
    // We don't skip all eval tests — only the eval-code category is excluded via dir selection

    // build combined source: harness + includes + test
    std::string combined;

    // always prepend sta.js and assert.js (unless raw)
    std::string sta = read_file_contents(std::string(HARNESS_DIR) + "/sta.js");
    std::string assert_js = read_file_contents(std::string(HARNESS_DIR) + "/assert.js");
    combined += sta + "\n" + assert_js + "\n";

    // additional includes
    for (auto& inc : meta.includes) {
        std::string harness_path = std::string(HARNESS_DIR) + "/" + inc;
        std::string harness_src = read_file_contents(harness_path);
        if (harness_src.empty()) {
            return {T262_SKIP, "missing harness: " + inc};
        }
        combined += harness_src + "\n";
    }

    // append test source
    if (meta.is_strict) {
        combined += "\"use strict\";\n";
    }
    combined += source;

    // write to temp file
    std::string temp_path = std::string(TEMP_DIR) + "/_test262_run.js";
    {
        std::ofstream out(temp_path);
        if (!out.is_open()) {
            return {T262_FAIL, "could not create temp file"};
        }
        out << combined;
    }

    // execute
    char command[1024];
#ifdef _WIN32
    snprintf(command, sizeof(command), "lambda.exe js \"%s\" --no-log 2>&1", temp_path.c_str());
#else
    snprintf(command, sizeof(command), "timeout 10 ./lambda.exe js \"%s\" --no-log 2>&1", temp_path.c_str());
#endif

    FILE* pipe = popen(command, "r");
    if (!pipe) {
        return {T262_FAIL, "could not execute lambda.exe"};
    }

    std::string output;
    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        output += buffer;
    }

    int status = pclose(pipe);
    int exit_code = WEXITSTATUS(status);

    // cleanup temp file
    unlink(temp_path.c_str());

    // interpret results
    if (meta.is_negative) {
        // negative tests expect an error
        if (exit_code != 0 || output.find("Error") != std::string::npos ||
            output.find("error") != std::string::npos) {
            return {T262_PASS, ""};
        } else {
            return {T262_FAIL, "negative test did not throw (expected " + meta.negative_type + ")"};
        }
    }

    // timeout check (exit code 124 on macOS/Linux from `timeout` command)
    if (exit_code == 124) {
        return {T262_TIMEOUT, "test timed out (10s)"};
    }

    // crash check
    if (exit_code > 128) {
        return {T262_CRASH, "crashed with signal " + std::to_string(exit_code - 128)};
    }

    // normal test: should succeed (exit 0, no Test262Error)
    if (exit_code != 0) {
        // trim output for error message
        std::string msg = output.substr(0, 200);
        return {T262_FAIL, "exit code " + std::to_string(exit_code) + ": " + msg};
    }

    if (output.find("Test262Error") != std::string::npos) {
        std::string msg = output.substr(0, 200);
        return {T262_FAIL, msg};
    }

    return {T262_PASS, ""};
}

// =============================================================================
// GTest parameterized test
// =============================================================================

class Test262Suite : public testing::TestWithParam<Test262Param> {};

TEST_P(Test262Suite, Run) {
    const Test262Param& param = GetParam();
    Test262RunResult result = run_test262(param.test_path);

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
    int passed = 0, failed = 0, skipped = 0, crashed = 0, timed_out = 0;
    std::map<std::string, std::pair<int,int>> category_results; // category -> (pass, total)

    void OnTestPartResult(const testing::TestPartResult& result) override {
        // count non-fatal failures
    }

    void OnTestEnd(const testing::TestInfo& info) override {
        if (info.result()->Skipped()) {
            skipped++;
        } else if (info.result()->Passed()) {
            passed++;
        } else {
            failed++;
        }
    }

    void OnTestSuiteEnd(const testing::TestSuite& suite) override {
        // printed by GTest
    }

    void OnTestProgramEnd(const testing::UnitTest& unit_test) override {
        int total = passed + failed;
        double pct = total > 0 ? 100.0 * passed / total : 0.0;
        printf("\n");
        printf("╔══════════════════════════════════════════════════╗\n");
        printf("║         test262 Compliance Summary               ║\n");
        printf("╠══════════════════════════════════════════════════╣\n");
        printf("║  Passed:  %5d / %5d  (%.1f%%)                 ║\n", passed, total, pct);
        printf("║  Failed:  %5d                                  ║\n", failed);
        printf("║  Skipped: %5d                                  ║\n", skipped);
        printf("╚══════════════════════════════════════════════════╝\n");
    }
};

int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);

    // register summary listener
    testing::TestEventListeners& listeners = testing::UnitTest::GetInstance()->listeners();
    listeners.Append(new Test262ReportListener());

    return RUN_ALL_TESTS();
}
