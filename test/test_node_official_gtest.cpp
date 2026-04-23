// =============================================================================
// Official Node.js Test Suite Runner for LambdaJS
// =============================================================================
//
// Runs tests from the official Node.js repository (ref/node/test/parallel/)
// against lambda.exe js.
//
// Protocol:
//   1. Discover test-*.js files in ref/node/test/parallel/
//   2. Filter by enabled feature modules (path, os, buffer, crypto, etc.)
//   3. Skip tests that require features we don't support
//   4. Execute each test: ./lambda.exe js <test_file> --no-log
//   5. PASS if exit code == 0 and no "Uncaught" in output
//   6. Compare against baseline; regression = baseline test that now fails
//
// Feature flags (environment variables or CLI args):
//   --modules=path,os,buffer     Run only specified modules (default: all enabled)
//   --update-baseline            Update baseline file with current passing set
//   --timeout=<ms>               Per-test timeout in ms (default: 10000)
//
// Usage:
//   ./test/test_node_official_gtest.exe                    # run all enabled modules
//   ./test/test_node_official_gtest.exe --modules=path,os  # run only path + os
//   ./test/test_node_official_gtest.exe --update-baseline  # update baseline
//
// =============================================================================

#include <gtest/gtest.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <string>
#include <vector>
#include <set>
#include <map>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <thread>
#include <mutex>
#include <atomic>
#include <unordered_map>
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
    #include <sys/stat.h>
#endif

// =============================================================================
// Configuration
// =============================================================================

static const char* NODE_TEST_DIR   = "ref/node/test/parallel";
static const char* BASELINE_FILE   = "test/node/official_baseline.txt";
static const char* CRASHER_FILE    = "temp/_node_official_crashers.txt";
static int         g_timeout_ms    = 60000;    // per-test timeout (60s for JIT compilation)
static bool        g_update_baseline = false;

static int get_parallel_workers() {
    int cores = std::thread::hardware_concurrency();
    return (cores > 2) ? cores - 1 : 1;
}
static const int   PARALLEL_WORKERS  = get_parallel_workers();

// =============================================================================
// Feature modules — each can be enabled/disabled
// =============================================================================
//
// Module prefixes: a test file named test-<prefix>-*.js belongs to that module.
// For example, test-path-join.js → module "path".
//
// Modules marked enabled=true are included by default.
// Use --modules=X,Y to override and test only specific modules.

struct FeatureModule {
    const char* name;        // module identifier
    const char* prefix;      // test filename prefix after "test-"
    bool enabled;            // included in test runs by default
    const char* description; // why disabled (if applicable)
};

// Master list of Node.js module feature flags.
// Tests matching test-<prefix>*.js are included if the module is enabled.
static std::vector<FeatureModule> g_feature_modules = {
    // --- Supported modules ---
    {"assert",         "assert",          true,  nullptr},
    {"buffer",         "buffer",          true,  nullptr},
    {"child_process",  "child-process",   true,  nullptr},
    {"crypto",         "crypto",          true,  nullptr},
    {"dns",            "dns",             true,  nullptr},
    {"events",         "events",          true,  nullptr},
    {"events",         "event",           true,  nullptr},  // test-event-emitter-*
    {"fs",             "fs",              true,  nullptr},
    {"os",             "os",              true,  nullptr},
    {"path",           "path",            true,  nullptr},
    {"process",        "process",         true,  nullptr},
    {"querystring",    "querystring",     true,  nullptr},
    {"stream",         "stream",          true,  nullptr},
    {"string_decoder", "string-decoder",  true,  nullptr},
    {"url",            "url",             true,  nullptr},
    {"util",           "util",            true,  nullptr},
    {"zlib",           "zlib",            true,  nullptr},
    {"async_wrap",     "async-wrap",      true,  nullptr},
    {"eventsource",    "eventsource",     true,  nullptr},
    {"stringbytes",    "stringbytes",     true,  nullptr},

    // --- Modules with partial support (enabled for coverage) ---
    {"http",           "http",            true,  nullptr},
    {"https",          "https",           true,  nullptr},
    {"module",         "module",          true,  nullptr},
    {"net",            "net",             true,  nullptr},
    {"readline",       "readline",        true,  nullptr},
    {"timers",         "timers",          true,  nullptr},
    {"tls",            "tls",             true,  nullptr},
    {"vm",             "vm",              true,  nullptr},

    // --- Additional test prefixes (non-module tests) ---
    {"misc",           "next",            true,  nullptr},  // test-next-tick-*
    {"misc",           "microtask",       true,  nullptr},  // test-microtask-queue-*
    {"misc",           "promise",         true,  nullptr},  // test-promise-*
    {"misc",           "readable",        true,  nullptr},  // test-readable-*
    {"misc",           "global",          true,  nullptr},  // test-global-*
    {"misc",           "error",           true,  nullptr},  // test-error-*
    {"misc",           "eval",            true,  nullptr},  // test-eval-*
    {"misc",           "common",          true,  nullptr},  // test-common-*
    {"misc",           "console",         true,  nullptr},  // test-console-*
    {"misc",           "async",           true,  nullptr},  // test-async-*
    {"misc",           "whatwg",          true,  nullptr},  // test-whatwg-*
    {"misc",           "internal",        true,  nullptr},  // test-internal-*
    {"misc",           "regression",      true,  nullptr},  // test-regression-*
    {"misc",           "instanceof",      true,  nullptr},  // test-instanceof-*
    {"misc",           "messagechannel",  true,  nullptr},  // test-messagechannel-*
    {"misc",           "messageevent",    true,  nullptr},  // test-messageevent-*
    {"misc",           "utf8",            true,  nullptr},  // test-utf8-*
    {"misc",           "beforeexit",      true,  nullptr},  // test-beforeexit-*
    {"misc",           "json",            true,  nullptr},  // test-json-*
    {"misc",           "number",          true,  nullptr},  // test-number-*
    {"misc",           "object",          true,  nullptr},  // test-object-*
    {"misc",           "symbol",          true,  nullptr},  // test-symbol-*
    {"misc",           "math",            true,  nullptr},  // test-math-*
    {"misc",           "date",            true,  nullptr},  // test-date-*
    {"misc",           "eslint",          true,  nullptr},  // test-eslint-*
    {"misc",           "heap",            true,  nullptr},  // test-heap-*
    {"misc",           "warn",            true,  nullptr},  // test-warn-*
    {"misc",           "snapshot",        true,  nullptr},  // test-snapshot-*
    {"misc",           "compile",         true,  nullptr},  // test-compile-*
    {"misc",           "pipe",            true,  nullptr},  // test-pipe-*
    {"misc",           "socket",          true,  nullptr},  // test-socket-*
    {"misc",           "shadow",          true,  nullptr},  // test-shadow-*
    {"misc",           "sqlite",          true,  nullptr},  // test-sqlite-*
    {"misc",           "debugger",        true,  nullptr},  // test-debugger-*
    {"misc",           "abort",           true,  nullptr},  // test-abort-*
    {"misc",           "abortcontroller", true,  nullptr},  // test-abortcontroller-*
    {"misc",           "eventtarget",     true,  nullptr},  // test-eventtarget-*
    {"misc",           "domexception",    true,  nullptr},  // test-domexception-*
    {"misc",           "blob",            true,  nullptr},  // test-blob-*
    {"misc",           "performance",     true,  nullptr},  // test-performance-*
    {"misc",           "structuredClone", true,  nullptr},  // test-structuredClone-*
    {"misc",           "errors",          true,  nullptr},  // test-errors-*
    {"misc",           "esm",            true,  nullptr},  // test-esm-*
    {"misc",           "stdin",           true,  nullptr},  // test-stdin-*
    {"misc",           "stdout",          true,  nullptr},  // test-stdout-*
    {"misc",           "stdio",           true,  nullptr},  // test-stdio-*
    {"misc",           "require",         true,  nullptr},  // test-require-*
    {"misc",           "inspect",         true,  nullptr},  // test-inspect-*

    // --- Additional test prefixes (Phase 5b) ---
    {"misc",           "runner",          true,  nullptr},  // test-runner-*
    {"misc",           "webcrypto",       true,  nullptr},  // test-webcrypto-*
    {"misc",           "cli",             true,  nullptr},  // test-cli-*
    {"misc",           "file",            true,  nullptr},  // test-file-*
    {"misc",           "filehandle",      true,  nullptr},  // test-filehandle-*
    {"misc",           "fileurltopathbuffer", true, nullptr}, // test-fileurltopathbuffer*
    {"misc",           "heapsnapshot",    true,  nullptr},  // test-heapsnapshot-*
    {"misc",           "permission",      true,  nullptr},  // test-permission-*
    {"misc",           "preload",         true,  nullptr},  // test-preload-*
    {"misc",           "promises",        true,  nullptr},  // test-promises-*
    {"misc",           "temporal",        true,  nullptr},  // test-temporal-*
    {"misc",           "trace",           true,  nullptr},  // test-trace-*
    {"misc",           "v8",              true,  nullptr},  // test-v8-*
    {"misc",           "stream2",         true,  nullptr},  // test-stream2-*
    {"misc",           "stream3",         true,  nullptr},  // test-stream3-*
    {"misc",           "uncaught",        true,  nullptr},  // test-uncaught-*
    {"misc",           "aborted",         true,  nullptr},  // test-aborted-*
    {"misc",           "config",          true,  nullptr},  // test-config-*
    {"misc",           "disable",         true,  nullptr},  // test-disable-*
    {"misc",           "double",          true,  nullptr},  // test-double-*
    {"misc",           "env",             true,  nullptr},  // test-env-*
    {"misc",           "icu",             true,  nullptr},  // test-icu-*
    {"misc",           "intl",            true,  nullptr},  // test-intl-*
    {"misc",           "kill",            true,  nullptr},  // test-kill-*
    {"misc",           "macos",           true,  nullptr},  // test-macos-*
    {"misc",           "release",         true,  nullptr},  // test-release-*
    {"misc",           "source",          true,  nullptr},  // test-source-*
    {"misc",           "spawn",           true,  nullptr},  // test-spawn-*
    {"misc",           "strace",          true,  nullptr},  // test-strace-*
    {"misc",           "sync",            true,  nullptr},  // test-sync-*
    {"misc",           "tick",            true,  nullptr},  // test-tick-*
    {"misc",           "tz",              true,  nullptr},  // test-tz-*
    {"misc",           "webstorage",      true,  nullptr},  // test-webstorage-*
    {"misc",           "windows",         true,  nullptr},  // test-windows-*
    {"misc",           "bad",             true,  nullptr},  // test-bad-*
    {"misc",           "bash",            true,  nullptr},  // test-bash-*
    {"misc",           "btoa",            true,  nullptr},  // test-btoa-*
    {"misc",           "bootstrap",       true,  nullptr},  // test-bootstrap-*
    {"misc",           "blocklist",       true,  nullptr},  // test-blocklist-*
    {"misc",           "benchmark",       true,  nullptr},  // test-benchmark-*
    {"misc",           "diagnostic",      true,  nullptr},  // test-diagnostic-*
    {"misc",           "diagnostics",     true,  nullptr},  // test-diagnostics-*
    {"misc",           "nodeeventtarget", true,  nullptr},  // test-nodeeventtarget-*
    {"misc",           "webstreams",      true,  nullptr},  // test-webstreams-*
    {"misc",           "webstream",       true,  nullptr},  // test-webstream-*

    // --- Additional test prefixes (Phase 6) ---
    {"misc",           "abortsignal",     true,  nullptr},  // test-abortsignal-*
    {"misc",           "accessor",        true,  nullptr},  // test-accessor-*
    {"misc",           "als",             true,  nullptr},  // test-als-*
    {"misc",           "asyncresource",   true,  nullptr},  // test-asyncresource-*
    {"misc",           "atomics",         true,  nullptr},  // test-atomics-*
    {"misc",           "binding",         true,  nullptr},  // test-binding-*
    {"misc",           "broadcastchannel",true,  nullptr},  // test-broadcastchannel-*
    {"misc",           "code",            true,  nullptr},  // test-code-*
    {"misc",           "compression",     true,  nullptr},  // test-compression-*
    {"misc",           "corepack",        true,  nullptr},  // test-corepack-*
    {"misc",           "coverage",        true,  nullptr},  // test-coverage-*
    {"misc",           "cwd",             true,  nullptr},  // test-cwd-*
    {"misc",           "dotenv",          true,  nullptr},  // test-dotenv-*
    {"misc",           "duplex",          true,  nullptr},  // test-duplex-*
    {"misc",           "emit",            true,  nullptr},  // test-emit-*
    {"misc",           "err",             true,  nullptr},  // test-err-*
    {"misc",           "eventemitter",    true,  nullptr},  // test-eventemitter-*
    {"misc",           "exception",       true,  nullptr},  // test-exception-*
    {"misc",           "fastutf8stream",  true,  nullptr},  // test-fastutf8stream-*
    {"misc",           "fetch",           true,  nullptr},  // test-fetch-*
    {"misc",           "force",           true,  nullptr},  // test-force-*
    {"misc",           "gc",              true,  nullptr},  // test-gc-*
    {"misc",           "handle",          true,  nullptr},  // test-handle-*
    {"misc",           "listen",          true,  nullptr},  // test-listen-*
    {"misc",           "memory",          true,  nullptr},  // test-memory-*
    {"misc",           "messageport",     true,  nullptr},  // test-messageport-*
    {"misc",           "messaging",       true,  nullptr},  // test-messaging-*
    {"misc",           "mime",            true,  nullptr},  // test-mime-*
    {"misc",           "no",              true,  nullptr},  // test-no-*
    {"misc",           "node",            true,  nullptr},  // test-node-*
    {"misc",           "npm",             true,  nullptr},  // test-npm-*
    {"misc",           "openssl",         true,  nullptr},  // test-openssl-*
    {"misc",           "options",         true,  nullptr},  // test-options-*
    {"misc",           "outgoing",        true,  nullptr},  // test-outgoing-*
    {"misc",           "parse",           true,  nullptr},  // test-parse-*
    {"misc",           "pending",         true,  nullptr},  // test-pending-*
    {"misc",           "perf",            true,  nullptr},  // test-perf-*
    {"misc",           "performanceobserver", true, nullptr}, // test-performanceobserver-*
    {"misc",           "primordials",     true,  nullptr},  // test-primordials-*
    {"misc",           "primitive",       true,  nullptr},  // test-primitive-*
    {"misc",           "priority",        true,  nullptr},  // test-priority-*
    {"misc",           "queue",           true,  nullptr},  // test-queue-*
    {"misc",           "ref",             true,  nullptr},  // test-ref-*
    {"misc",           "resource",        true,  nullptr},  // test-resource-*
    {"misc",           "safe",            true,  nullptr},  // test-safe-*
    {"misc",           "sea",             true,  nullptr},  // test-sea-*
    {"misc",           "security",        true,  nullptr},  // test-security-*
    {"misc",           "set",             true,  nullptr},  // test-set-*
    {"misc",           "signal",          true,  nullptr},  // test-signal-*
    {"misc",           "stack",           true,  nullptr},  // test-stack-*
    {"misc",           "startup",         true,  nullptr},  // test-startup-*
    {"misc",           "streams",         true,  nullptr},  // test-streams-*
    {"misc",           "string",          true,  nullptr},  // test-string-*
    {"misc",           "tcp",             true,  nullptr},  // test-tcp-*
    {"misc",           "tojson",          true,  nullptr},  // test-tojson-*
    {"misc",           "tracing",         true,  nullptr},  // test-tracing-*
    {"misc",           "ttywrap",         true,  nullptr},  // test-ttywrap-*
    {"misc",           "unhandled",       true,  nullptr},  // test-unhandled-*
    {"misc",           "unicode",         true,  nullptr},  // test-unicode-*
    {"misc",           "urlpattern",      true,  nullptr},  // test-urlpattern-*
    {"misc",           "uv",              true,  nullptr},  // test-uv-*
    {"misc",           "web",             true,  nullptr},  // test-web-*
    {"misc",           "webapi",          true,  nullptr},  // test-webapi-*
    {"misc",           "websocket",       true,  nullptr},  // test-websocket-*
    {"misc",           "wrap",            true,  nullptr},  // test-wrap-*
    {"misc",           "x509",            true,  nullptr},  // test-x509-*

    // --- Unsupported modules (disabled by default) ---
    {"cluster",        "cluster",         true, ""},
    {"dgram",          "dgram",           false, "UDP sockets not implemented"},
    {"domain",         "domain",          true,  nullptr},  // enabled: some tests pass
    {"http2",          "http2",           false, "HTTP/2 not implemented"},
    {"inspector",      "inspector",       true,  nullptr},
    {"perf_hooks",     "perf-hooks",      true,  nullptr},  // enabled: try coverage
    {"repl",           "repl",            true,  nullptr},  // enabled: some tests pass
    {"tty",            "tty",             true, ""},
    {"wasi",           "wasi",            false, "WASI not implemented"},
    {"worker",         "worker",          true,  nullptr},  // enabled: some spawn tests pass
};

// Tests skipped by filename — known to be incompatible, non-deterministic, or
// requiring features beyond our scope (e.g., native addons, spawning node).
static const std::map<std::string, std::string> SKIPPED_TESTS = {
    // Tests that spawn 'node' binary directly
    {"test-child-process-exec-kill-throws.js",     "spawns node binary"},
    {"test-child-process-fork.js",                 "uses child_process.fork (spawns node)"},
    {"test-child-process-fork-abort-signal.js",    "uses child_process.fork"},
    {"test-child-process-fork-close.js",           "uses child_process.fork"},
    {"test-child-process-fork-dgram.js",           "uses child_process.fork + dgram"},
    {"test-child-process-fork-exec-path.js",       "uses child_process.fork"},
    {"test-child-process-fork-net.js",             "uses child_process.fork + net"},
    {"test-child-process-fork-net-socket.js",      "uses child_process.fork + net"},
    {"test-child-process-fork-ref.js",             "uses child_process.fork"},
    {"test-child-process-fork-ref2.js",            "uses child_process.fork"},

    // Tests requiring native addons / node-gyp
    {"test-crypto-fips.js",                        "requires FIPS OpenSSL build"},

    // Tests depending on process.execPath being 'node'
    // NOTE: test-process-execpath.js now passes with the common shim

    // Non-deterministic tests
    {"test-crypto-random.js",                      "non-deterministic (random output)"},

    // --- Genuine timeouts: event loop / async / unimplemented features ---
    // These hang because they depend on Node.js event loop (process.nextTick,
    // setImmediate, event callbacks) which Lambda doesn't implement.
    {"test-stream-pipe-needDrain.js",              "hangs: requires event loop (process.nextTick)"},
    {"test-stream-writable-write-cb-error.js",     "hangs: requires event loop (process.nextTick, common.mustCall)"},
    {"test-string-decoder-fuzz.js",                "hangs: Date.now() loop + StringDecoder instance methods"},
    {"test-util-inspect-long-running.js",          "hangs: util.inspect depth:Infinity on 1000-node circular graph"},
    {"test-zlib-brotli.js",                        "hangs: requires fixtures module + brotli compression"},
    {"test-os-process-priority.js",                "hangs: requires os.constants.priority (not implemented)"},
    {"test-buffer-alloc.js",                       "timeout: 1192-line test, JIT compilation exceeds 30s"},

    // Tests that use child_process.fork which spawns Node binary
    {"test-child-process-fork-stdio.js",           "uses child_process.fork"},
    // test-child-process-fork-exec-argv.js — now passes, removed from skip list
};

// =============================================================================
// Global state
// =============================================================================

static std::set<std::string>   g_baseline_passing;   // tests expected to pass
static std::set<std::string>   g_enabled_prefixes;    // active module prefixes
static bool                    g_prefixes_initialized = false;

// Initialize enabled prefixes with defaults (all enabled modules).
// Called lazily on first use (before main() for INSTANTIATE_TEST_SUITE_P).
static void ensure_prefixes_initialized() {
    if (g_prefixes_initialized) return;
    g_prefixes_initialized = true;
    for (auto& mod : g_feature_modules) {
        if (mod.enabled) {
            g_enabled_prefixes.insert(mod.prefix);
        }
    }
}

struct NodeTestResult {
    std::string test_name;     // filename without .js
    std::string output;        // stdout+stderr captured
    int exit_code;
    bool passed;               // exit_code == 0 && no "Uncaught"
    bool timed_out;
    double elapsed_ms;
};



// =============================================================================
// Helpers
// =============================================================================

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

// Extract module prefix from test filename.
// "test-path-join.js" → "path"
// "test-child-process-fork.js" → "child-process"
static std::string extract_module_prefix(const std::string& filename) {
    // filename is like "test-path-join.js"
    if (filename.substr(0, 5) != "test-") return "";

    std::string rest = filename.substr(5);   // "path-join.js"

    // try to match known prefixes (longest match first)
    // We need to match multi-word prefixes like "child-process", "string-decoder"
    // Sort prefixes by length descending for greedy match
    static std::vector<std::string> sorted_prefixes;
    static bool sorted = false;
    if (!sorted) {
        for (auto& mod : g_feature_modules) {
            sorted_prefixes.push_back(mod.prefix);
        }
        std::sort(sorted_prefixes.begin(), sorted_prefixes.end(),
                  [](const std::string& a, const std::string& b) {
                      return a.size() > b.size();
                  });
        sorted = true;
    }

    for (auto& prefix : sorted_prefixes) {
        if (rest.size() > prefix.size() &&
            rest.substr(0, prefix.size()) == prefix &&
            (rest[prefix.size()] == '-' || rest[prefix.size()] == '.')) {
            return prefix;
        }
        // exact match: test-os.js
        if (rest == prefix + ".js") {
            return prefix;
        }
    }

    return "";
}

// =============================================================================
// Test execution — run a single test with timeout
// =============================================================================

static NodeTestResult run_single_test(const std::string& test_path) {
    NodeTestResult result;
    result.test_name = test_path;
    result.timed_out = false;

    // extract just filename for display
    size_t slash = test_path.rfind('/');
    std::string filename = (slash != std::string::npos) ? test_path.substr(slash + 1) : test_path;

    auto start = std::chrono::steady_clock::now();

    // Build command with timeout.
    // Strip ASAN-related env vars: the gtest binary is ASAN-instrumented but
    // lambda.exe is not. ASAN sets MallocNanoZone=0 which can cause malloc
    // corruption in non-ASAN binaries.
    // Run inside temp/node_test/ so garbage files don't pollute project root.
    char command[2048];
#ifdef _WIN32
    snprintf(command, sizeof(command),
             "cd temp\\node_test && ..\\..\\lambda.exe js \"../../%s\" --no-log 2>&1",
             test_path.c_str());
#else
    snprintf(command, sizeof(command),
             "mkdir -p temp/node_test && cd temp/node_test && "
             "env -u DYLD_INSERT_LIBRARIES -u DYLD_LIBRARY_PATH -u ASAN_OPTIONS -u MallocNanoZone "
             "timeout %d ../../lambda.exe js \"../../%s\" --no-log 2>&1",
             (g_timeout_ms + 999) / 1000, test_path.c_str());
#endif

    FILE* pipe = popen(command, "r");
    if (!pipe) {
        result.exit_code = -1;
        result.passed = false;
        result.output = "ERROR: could not execute command";
        result.elapsed_ms = 0;
        return result;
    }

    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result.output += buffer;
    }

    int status = pclose(pipe);
    result.exit_code = WEXITSTATUS(status);

    auto end = std::chrono::steady_clock::now();
    result.elapsed_ms = std::chrono::duration<double, std::milli>(end - start).count();

    // timeout detection (exit code 124 from timeout command, or 137 for SIGKILL)
    if (result.exit_code == 124 || result.exit_code == 137) {
        result.timed_out = true;
        result.passed = false;
        return result;
    }

    // crash detection (signal exits: 128+N)
    // Note: exit code 1 is normal assertion failure, not a crash
    bool is_crash = (result.exit_code > 128 && result.exit_code != 137);
    (void)is_crash; // tracked in summary report

    // pass = exit code 0 AND no "Uncaught" errors in output
    bool has_uncaught = (result.output.find("Uncaught") != std::string::npos);
    result.passed = (result.exit_code == 0 && !has_uncaught);

    return result;
}

// =============================================================================
// Test discovery
// =============================================================================

struct NodeOfficialParam {
    std::string test_path;     // full relative path: ref/node/test/parallel/test-...
    std::string filename;      // test-path-join.js
    std::string module;        // "path"
    std::string test_name;     // sanitized for GTest: test_path_join
};

static std::vector<NodeOfficialParam> discover_node_official_tests() {
    ensure_prefixes_initialized();
    std::vector<NodeOfficialParam> tests;
    std::string dir_path = NODE_TEST_DIR;

#ifdef _WIN32
    std::string pattern = dir_path + "\\test-*.js";
    struct _finddata_t fd;
    intptr_t handle = _findfirst(pattern.c_str(), &fd);
    if (handle == -1) return tests;
    do {
        std::string filename = fd.name;
#else
    DIR* dir = opendir(dir_path.c_str());
    if (!dir) {
        fprintf(stderr, "[node-official] Cannot open directory: %s\n", dir_path.c_str());
        return tests;
    }

    // collect and sort for deterministic order
    std::vector<std::string> filenames;
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_type == DT_DIR) continue;
        filenames.push_back(entry->d_name);
    }
    closedir(dir);
    std::sort(filenames.begin(), filenames.end());

    for (auto& filename : filenames) {
#endif
        // must be test-*.js
        if (filename.size() < 8 || filename.substr(0, 5) != "test-") continue;
        if (filename.substr(filename.size() - 3) != ".js") continue;

        // extract module prefix
        std::string mod = extract_module_prefix(filename);
        if (mod.empty()) continue;

        // check if module is enabled
        if (g_enabled_prefixes.find(mod) == g_enabled_prefixes.end()) continue;

        // check if test is explicitly skipped
        if (SKIPPED_TESTS.find(filename) != SKIPPED_TESTS.end()) continue;

        NodeOfficialParam p;
        p.test_path = dir_path + "/" + filename;
        p.filename  = filename;
        p.module    = mod;

        // sanitize test name for GTest
        std::string base = filename.substr(0, filename.size() - 3); // strip .js
        p.test_name = base;
        for (auto& c : p.test_name) {
            if (!isalnum((unsigned char)c)) c = '_';
        }

        tests.push_back(p);

#ifdef _WIN32
    } while (_findnext(handle, &fd) == 0);
    _findclose(handle);
#else
    }
#endif

    return tests;
}

// =============================================================================
// Parallel test execution
// =============================================================================

static std::unordered_map<std::string, NodeTestResult> g_test_results;
static std::mutex g_test_results_mutex;

static void execute_all_tests(const std::vector<NodeOfficialParam>& tests) {
    if (tests.empty()) return;

    fprintf(stderr, "[node-official] Running %zu tests with %d workers...\n",
            tests.size(), PARALLEL_WORKERS);

    std::atomic<size_t> next_index{0};
    std::vector<std::thread> workers;

    for (int w = 0; w < PARALLEL_WORKERS; w++) {
        workers.emplace_back([&]() {
            while (true) {
                size_t idx = next_index.fetch_add(1);
                if (idx >= tests.size()) break;

                const auto& t = tests[idx];
                NodeTestResult result = run_single_test(t.test_path);

                {
                    std::lock_guard<std::mutex> lock(g_test_results_mutex);
                    g_test_results[t.filename] = std::move(result);
                }
            }
        });
    }

    for (auto& w : workers) w.join();
}

// =============================================================================
// Baseline file I/O
// =============================================================================

static void load_baseline() {
    FILE* f = fopen(BASELINE_FILE, "r");
    if (!f) {
        fprintf(stderr, "[node-official] No baseline file found (%s) — regression checking disabled\n",
                BASELINE_FILE);
        return;
    }
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) line[--len] = '\0';
        if (len > 0) g_baseline_passing.insert(std::string(line, len));
    }
    fclose(f);
    fprintf(stderr, "[node-official] Loaded baseline: %zu passing tests from %s\n",
            g_baseline_passing.size(), BASELINE_FILE);
}

static void write_baseline(const std::vector<std::string>& passing, int total, int skipped) {
    FILE* f = fopen(BASELINE_FILE, "w");
    if (!f) {
        fprintf(stderr, "[node-official] ERROR: Cannot write baseline file: %s\n", BASELINE_FILE);
        return;
    }
    std::string commit = get_git_commit_hash();
    fprintf(f, "# Node.js Official Test Baseline (auto-updated)\n");
    fprintf(f, "# Commit: %s\n", commit.c_str());
    fprintf(f, "# Source: ref/node/test/parallel/\n");
    fprintf(f, "# Total passing: %zu / %d  (skipped: %d)\n", passing.size(), total, skipped);
    fprintf(f, "#\n");
    fprintf(f, "# Feature modules enabled:\n");
    for (auto& mod : g_feature_modules) {
        if (mod.enabled) {
            fprintf(f, "#   %s\n", mod.name);
        }
    }
    fprintf(f, "#\n");
    for (auto& name : passing) {
        fprintf(f, "%s\n", name.c_str());
    }
    fclose(f);
    fprintf(stderr, "[node-official] Wrote baseline: %zu passing tests to %s\n",
            passing.size(), BASELINE_FILE);
}

static void write_crashers(const std::vector<NodeOfficialParam>& tests) {
    FILE* f = fopen(CRASHER_FILE, "w");
    if (!f) return;
    for (auto& t : tests) {
        auto it = g_test_results.find(t.filename);
        if (it == g_test_results.end()) continue;
        const auto& r = it->second;
        if (r.timed_out) {
            fprintf(f, "TIMEOUT\t%s\n", t.filename.c_str());
        } else if (r.exit_code > 128) {
            fprintf(f, "CRASH_%d\t%s\n", r.exit_code, t.filename.c_str());
        }
    }
    fclose(f);
}

// =============================================================================
// GTest parameterised test
// =============================================================================

class NodeOfficialTest : public testing::TestWithParam<NodeOfficialParam> {
public:
    static bool suite_executed;

    static void SetUpTestSuite() {
        if (suite_executed) return;
        suite_executed = true;

        auto tests = discover_node_official_tests();
        execute_all_tests(tests);
    }
};

bool NodeOfficialTest::suite_executed = false;

TEST_P(NodeOfficialTest, Run) {
    const auto& p = GetParam();

    // check if this test's module is in the currently enabled set
    // (may differ from discovery-time if --modules was specified at runtime)
    if (g_enabled_prefixes.find(p.module) == g_enabled_prefixes.end()) {
        GTEST_SKIP() << "Module not enabled: " << p.module;
        return;
    }

    auto it = g_test_results.find(p.filename);
    if (it == g_test_results.end()) {
        // test not executed (shouldn't happen, but handle gracefully)
        GTEST_SKIP() << "Test not executed: " << p.filename;
        return;
    }

    const auto& result = it->second;
    bool in_baseline = g_baseline_passing.count(p.filename) > 0;

    if (result.timed_out) {
        if (in_baseline) {
            FAIL() << "REGRESSION (timeout): " << p.filename
                   << " — was in baseline but timed out after " << g_timeout_ms << "ms";
        } else {
            GTEST_SKIP() << "Timeout (not in baseline): " << p.filename;
        }
        return;
    }

    if (result.passed) {
        // pass — this is always OK
        SUCCEED();
        return;
    }

    // test failed
    if (in_baseline) {
        // REGRESSION: was passing, now fails
        FAIL() << "REGRESSION: " << p.filename
               << " — was in baseline but failed (exit " << result.exit_code << ")\n"
               << "Output:\n" << result.output.substr(0, 2000);
    } else {
        // expected failure — not in baseline, skip (don't count as GTest failure)
        GTEST_SKIP() << "Expected failure (not in baseline): " << p.filename
                     << " (exit " << result.exit_code << ")";
    }
}

INSTANTIATE_TEST_SUITE_P(
    NodeOfficial,
    NodeOfficialTest,
    testing::ValuesIn(discover_node_official_tests()),
    [](const testing::TestParamInfo<NodeOfficialParam>& info) {
        return info.param.test_name;
    });

// =============================================================================
// Summary report listener
// =============================================================================

class NodeOfficialReportListener : public testing::EmptyTestEventListener {
public:
    void OnTestSuiteEnd(const testing::TestSuite& suite) override {
        if (std::string(suite.name()).find("NodeOfficial") == std::string::npos) return;

        auto tests = discover_node_official_tests();
        int total = (int)tests.size();
        int passed = 0, failed = 0, skipped = 0, timed_out = 0, crashed = 0;
        std::vector<std::string> current_passing;
        std::vector<std::string> regressions;
        std::vector<std::string> improvements;

        for (auto& t : tests) {
            auto it = g_test_results.find(t.filename);
            if (it == g_test_results.end()) { skipped++; continue; }
            const auto& r = it->second;

            if (r.passed) {
                passed++;
                current_passing.push_back(t.filename);
            } else if (r.timed_out) {
                timed_out++;
            } else if (r.exit_code > 128) {
                crashed++;
                failed++;
            } else {
                failed++;
            }
        }

        std::sort(current_passing.begin(), current_passing.end());

        // detect regressions and improvements vs baseline
        // only check regressions for tests that were in this run's scope
        if (!g_baseline_passing.empty()) {
            std::set<std::string> current_set(current_passing.begin(), current_passing.end());
            std::set<std::string> in_scope;
            for (auto& t : tests) in_scope.insert(t.filename);

            for (auto& name : g_baseline_passing) {
                // only flag regression if the test was in scope for this run
                if (in_scope.count(name) && current_set.find(name) == current_set.end()) {
                    regressions.push_back(name);
                }
            }
            for (auto& name : current_passing) {
                if (g_baseline_passing.find(name) == g_baseline_passing.end()) {
                    improvements.push_back(name);
                }
            }
        }

        // print summary
        printf("\n");
        printf("╔══════════════════════════════════════════════════╗\n");
        printf("║         Node.js Official Test Results            ║\n");
        printf("╠══════════════════════════════════════════════════╣\n");
        printf("║  Total tests:     %5d                          ║\n", total);
        printf("║  Passed:          %5d                          ║\n", passed);
        printf("║  Failed:          %5d                          ║\n", failed);
        printf("║  Timed out:       %5d                          ║\n", timed_out);
        printf("║  Crashed:         %5d                          ║\n", crashed);
        if (!g_baseline_passing.empty()) {
            printf("╠══════════════════════════════════════════════════╣\n");
            printf("║  Baseline:        %5zu                          ║\n", g_baseline_passing.size());
            printf("║  Regressions:     %5zu                          ║\n", regressions.size());
            printf("║  Improvements:    %5zu                          ║\n", improvements.size());
        }
        printf("╚══════════════════════════════════════════════════╝\n");

        if (!regressions.empty()) {
            printf("\n=== REGRESSIONS (baseline tests that now FAIL) ===\n");
            for (auto& r : regressions) {
                printf("  REGRESS  %s\n", r.c_str());
            }
        }

        if (!improvements.empty() && improvements.size() <= 50) {
            printf("\n=== IMPROVEMENTS (new passes not in baseline) ===\n");
            for (auto& im : improvements) {
                printf("  NEW_PASS %s\n", im.c_str());
            }
        } else if (!improvements.empty()) {
            printf("\n=== IMPROVEMENTS: %zu new passes (too many to list) ===\n",
                   improvements.size());
        }

        // update baseline if requested
        if (g_update_baseline && regressions.empty()) {
            write_baseline(current_passing, total, skipped);
        } else if (g_update_baseline && !regressions.empty()) {
            fprintf(stderr, "[node-official] NOT updating baseline: %zu regressions detected\n",
                    regressions.size());
        }

        // write crasher file for quarantine
        write_crashers(tests);
    }
};

// =============================================================================
// main() — parse args + run GTest
// =============================================================================

int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);

    // parse custom arguments
    std::set<std::string> cli_modules;
    bool modules_specified = false;

    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--modules=", 10) == 0) {
            modules_specified = true;
            std::string mods = argv[i] + 10;
            std::istringstream ss(mods);
            std::string mod;
            while (std::getline(ss, mod, ',')) {
                if (!mod.empty()) cli_modules.insert(mod);
            }
        } else if (strcmp(argv[i], "--update-baseline") == 0) {
            g_update_baseline = true;
        } else if (strncmp(argv[i], "--timeout=", 10) == 0) {
            g_timeout_ms = atoi(argv[i] + 10);
            if (g_timeout_ms < 1000) g_timeout_ms = 1000;
        }
    }

    // determine enabled prefixes (may override defaults from lazy init)
    if (modules_specified) {
        // user specified modules — reset and enable only those
        g_enabled_prefixes.clear();
        for (auto& mod : g_feature_modules) {
            if (cli_modules.count(mod.name) > 0) {
                g_enabled_prefixes.insert(mod.prefix);
            }
        }
        fprintf(stderr, "[node-official] Modules filter: %zu modules enabled via --modules\n",
                g_enabled_prefixes.size());
    } else {
        // defaults already set by ensure_prefixes_initialized()
        ensure_prefixes_initialized();
    }

    fprintf(stderr, "[node-official] Enabled module prefixes:");
    for (auto& p : g_enabled_prefixes) fprintf(stderr, " %s", p.c_str());
    fprintf(stderr, "\n");

    // check that test directory exists
    struct stat st;
    if (stat(NODE_TEST_DIR, &st) != 0 || !S_ISDIR(st.st_mode)) {
        fprintf(stderr, "[node-official] ERROR: Node.js test directory not found: %s\n", NODE_TEST_DIR);
        fprintf(stderr, "[node-official] Clone the Node.js repo: git clone https://github.com/nodejs/node ref/node\n");
        return 1;
    }

    // load baseline
    load_baseline();

    // register summary listener
    testing::TestEventListeners& listeners = testing::UnitTest::GetInstance()->listeners();
    listeners.Append(new NodeOfficialReportListener());

    return RUN_ALL_TESTS();
}
