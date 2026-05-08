// =============================================================================
// test_js_props_gtest.cpp
//
// Stage B harness for the property-model invariant matrix described in
// vibe/jube/Transpile_Js38_Refactor.md §B / §4.
//
// Each TEST exercises one row of the {own/inherited/accessor/data/deleted/proxy}
// × {INT/FLOAT/string/symbol/numeric-string/negative-int} × {strict/sloppy}
// interaction matrix. The 16 cases listed in §4 of the refactor doc are
// implemented verbatim below.
//
// Today these tests drive the engine via subprocess (./lambda.exe js).
// Once Stage A1 lands the abstract operations (OrdinaryGet/Set/Delete/
// Define/Has) as standalone C functions, the body of each TEST will be
// converted to direct in-process calls — but the test names and
// invariants stay stable.
//
// Companion to the integration scripts in test/js_props/*.js (see README.md
// in that directory). Those run end-to-end through test_js_gtest.exe and
// validate observable JS semantics. This file is the inner loop for the
// Stage A property-model refactor.
// =============================================================================

#include <gtest/gtest.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    #include <io.h>
    #include <direct.h>
    #define popen _popen
    #define pclose _pclose
    #define WEXITSTATUS(s) (s)
#else
    #include <unistd.h>
    #include <sys/wait.h>
#endif

namespace {

constexpr const char* kTempDir = "./temp/js_props";

// ---------------------------------------------------------------------------
// Helpers: write snippet to ./temp/js_props, run via lambda.exe, capture
// stdout+stderr, strip the "##### Script ..." banner the runner injects.
// ---------------------------------------------------------------------------

void ensure_temp_dir() {
    struct stat st;
    if (stat("./temp", &st) != 0) {
#ifdef _WIN32
        _mkdir("./temp");
#else
        mkdir("./temp", 0755);
#endif
    }
    if (stat(kTempDir, &st) != 0) {
#ifdef _WIN32
        _mkdir(kTempDir);
#else
        mkdir(kTempDir, 0755);
#endif
    }
}

std::string write_snippet(const char* test_name, const char* js_source) {
    ensure_temp_dir();
    std::string path = std::string(kTempDir) + "/" + test_name + ".js";
    FILE* f = fopen(path.c_str(), "w");
    if (!f) return std::string();
    fwrite(js_source, 1, strlen(js_source), f);
    fclose(f);
    return path;
}

struct RunResult {
    int exit_code;
    std::string output;   // stdout (post-banner) + stderr merged
};

RunResult run_js(const std::string& script_path) {
    RunResult r{-1, std::string()};
    std::string cmd;
#ifdef _WIN32
    cmd = "lambda.exe js \"" + script_path + "\" --no-log 2>&1";
#else
    cmd = "./lambda.exe js \"" + script_path + "\" --no-log 2>&1";
#endif

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return r;

    char buf[1024];
    while (fgets(buf, sizeof(buf), pipe)) r.output += buf;
    int status = pclose(pipe);
    r.exit_code = WEXITSTATUS(status);

    // Strip "##### Script ..." banner if present.
    const char* marker = strstr(r.output.c_str(), "##### Script");
    if (marker) {
        const char* nl = strchr(marker, '\n');
        if (nl) r.output = std::string(nl + 1);
    }

    // Trim trailing whitespace.
    while (!r.output.empty() &&
           (r.output.back() == '\n' || r.output.back() == '\r' ||
            r.output.back() == ' '  || r.output.back() == '\t')) {
        r.output.pop_back();
    }
    return r;
}

// Convenience: run snippet, expect successful exit, return trimmed output.
std::string expect_ok(const char* test_name, const char* js_source) {
    std::string path = write_snippet(test_name, js_source);
    EXPECT_FALSE(path.empty()) << "could not write snippet to " << kTempDir;
    if (path.empty()) return std::string();
    RunResult r = run_js(path);
    EXPECT_EQ(r.exit_code, 0)
        << "non-zero exit for " << test_name << "\noutput:\n" << r.output;
    return r.output;
}

}  // namespace

// =============================================================================
// 1. OwnDataGetSet
// =============================================================================
TEST(Props, OwnDataGetSet) {
    auto out = expect_ok("OwnDataGetSet", R"JS(
        var obj = {};
        obj.x = 7;
        console.log(obj.x);
        obj.x = "hello";
        console.log(obj.x);
        console.log("OK");
    )JS");
    EXPECT_NE(out.find("7"),     std::string::npos) << out;
    EXPECT_NE(out.find("hello"), std::string::npos) << out;
    EXPECT_NE(out.find("OK"),    std::string::npos) << out;
}

// =============================================================================
// 2. OwnAccessorGet_GetterDispatch
// =============================================================================
TEST(Props, OwnAccessorGet_GetterDispatch) {
    auto out = expect_ok("OwnAccessorGet_GetterDispatch", R"JS(
        var hits = 0;
        var obj = {};
        Object.defineProperty(obj, "x", {
            get: function() { hits++; return 42; },
            configurable: true
        });
        console.log(obj.x);
        console.log(obj.x);
        console.log("hits=" + hits);
        console.log("OK");
    )JS");
    EXPECT_NE(out.find("42"),     std::string::npos) << out;
    EXPECT_NE(out.find("hits=2"), std::string::npos) << out;
    EXPECT_NE(out.find("OK"),     std::string::npos) << out;
}

// =============================================================================
// 3. OwnAccessorSet_SetterDispatch
// =============================================================================
TEST(Props, OwnAccessorSet_SetterDispatch) {
    auto out = expect_ok("OwnAccessorSet_SetterDispatch", R"JS(
        var captured = null;
        var obj = {};
        Object.defineProperty(obj, "x", {
            set: function(v) { captured = v; },
            configurable: true
        });
        obj.x = 99;
        console.log("captured=" + captured);
        console.log("OK");
    )JS");
    EXPECT_NE(out.find("captured=99"), std::string::npos) << out;
    EXPECT_NE(out.find("OK"),          std::string::npos) << out;
}

// =============================================================================
// 4. AccessorOnlyGetter_SetIsTypeError_Strict
// =============================================================================
TEST(Props, AccessorOnlyGetter_SetIsTypeError_Strict) {
    auto out = expect_ok("AccessorOnlyGetter_SetIsTypeError_Strict", R"JS(
        "use strict";
        var obj = {};
        Object.defineProperty(obj, "x", { get: function(){ return 1; }, configurable: true });
        var threw = false;
        try { obj.x = 2; } catch (e) { threw = (e instanceof TypeError); }
        console.log("threw=" + threw);
        console.log("OK");
    )JS");
    EXPECT_NE(out.find("threw=true"), std::string::npos) << out;
    EXPECT_NE(out.find("OK"),         std::string::npos) << out;
}

// =============================================================================
// 5. AccessorOnlyGetter_SetIsSilent_NonStrict
// =============================================================================
TEST(Props, AccessorOnlyGetter_SetIsSilent_NonStrict) {
    auto out = expect_ok("AccessorOnlyGetter_SetIsSilent_NonStrict", R"JS(
        // sloppy mode by default
        var obj = {};
        Object.defineProperty(obj, "x", { get: function(){ return 1; }, configurable: true });
        var threw = false;
        try { obj.x = 2; } catch (e) { threw = true; }
        console.log("threw=" + threw);
        console.log("x=" + obj.x);
        console.log("OK");
    )JS");
    EXPECT_NE(out.find("threw=false"), std::string::npos) << out;
    EXPECT_NE(out.find("x=1"),         std::string::npos) << out;
    EXPECT_NE(out.find("OK"),          std::string::npos) << out;
}

// =============================================================================
// 6. DeleteThenGet_FallsThroughToProto
// =============================================================================
TEST(Props, DeleteThenGet_FallsThroughToProto) {
    auto out = expect_ok("DeleteThenGet_FallsThroughToProto", R"JS(
        var proto = { x: "from-proto" };
        var obj = Object.create(proto);
        obj.x = "from-own";
        console.log(obj.x);
        delete obj.x;
        console.log(obj.x);
        console.log("OK");
    )JS");
    EXPECT_NE(out.find("from-own"),   std::string::npos) << out;
    EXPECT_NE(out.find("from-proto"), std::string::npos) << out;
    EXPECT_NE(out.find("OK"),         std::string::npos) << out;
}

// =============================================================================
// 7. DeleteOnFloatKey_NoSentinelLeak
// Mirrors test/js_props/delete_float_key_no_sentinel_leak.js.
// Regression: SIGSEGV at 0x400dead00dead08, fixed by canonicalizing FLOAT keys
// in js_delete_property.
// =============================================================================
TEST(Props, DeleteOnFloatKey_NoSentinelLeak) {
    auto out = expect_ok("DeleteOnFloatKey_NoSentinelLeak", R"JS(
        var obj = {};
        Object.defineProperty(obj, "1.5", { get: function(){ return 99; }, configurable: true });
        console.log(obj[1.5]);
        delete obj[1.5];
        console.log(obj[1.5] === undefined);
        Object.defineProperty(obj, "1.5", { get: function(){ return 42; }, configurable: true });
        console.log(obj[1.5]);
        console.log("OK");
    )JS");
    EXPECT_NE(out.find("99"),   std::string::npos) << out;
    EXPECT_NE(out.find("true"), std::string::npos) << out;
    EXPECT_NE(out.find("42"),   std::string::npos) << out;
    EXPECT_NE(out.find("OK"),   std::string::npos) << out;
}

// =============================================================================
// 8. DeleteOnNegativeIntKey_NoSentinelLeak
// Mirrors test/js_props/delete_negative_int_key_canonical.js.
// =============================================================================
TEST(Props, DeleteOnNegativeIntKey_NoSentinelLeak) {
    auto out = expect_ok("DeleteOnNegativeIntKey_NoSentinelLeak", R"JS(
        var obj = {};
        obj[-1] = "neg";
        console.log(obj[-1]);
        delete obj[-1];
        console.log(obj[-1] === undefined);
        Object.defineProperty(obj, "-1", { get: function(){ return 7; }, configurable: true });
        console.log(obj[-1]);
        console.log("OK");
    )JS");
    EXPECT_NE(out.find("neg"),  std::string::npos) << out;
    EXPECT_NE(out.find("true"), std::string::npos) << out;
    EXPECT_NE(out.find("7"),    std::string::npos) << out;
    EXPECT_NE(out.find("OK"),   std::string::npos) << out;
}

// =============================================================================
// 9. DefineAccessorAfterDeletedAccessor_NoCrash
// =============================================================================
TEST(Props, DefineAccessorAfterDeletedAccessor_NoCrash) {
    auto out = expect_ok("DefineAccessorAfterDeletedAccessor_NoCrash", R"JS(
        var obj = {};
        Object.defineProperty(obj, "x", { get: function(){ return 1; }, configurable: true });
        console.log(obj.x);
        delete obj.x;
        console.log(obj.x === undefined);
        Object.defineProperty(obj, "x", { get: function(){ return 2; }, configurable: true });
        console.log(obj.x);
        console.log("OK");
    )JS");
    EXPECT_NE(out.find("1"),    std::string::npos) << out;
    EXPECT_NE(out.find("true"), std::string::npos) << out;
    EXPECT_NE(out.find("2"),    std::string::npos) << out;
    EXPECT_NE(out.find("OK"),   std::string::npos) << out;
}

// =============================================================================
// 10. ClassMethodDispatch_HonorsOwnDeletedSentinel
// Regression: `delete Boolean.prototype.toString` then calling toString
// stack-overflowed because two call sites of class-method dispatch did not
// honor the deleted sentinel.
// =============================================================================
TEST(Props, ClassMethodDispatch_HonorsOwnDeletedSentinel) {
    auto out = expect_ok("ClassMethodDispatch_HonorsOwnDeletedSentinel", R"JS(
        // Override toString on a Boolean wrapper instance.
        var b = Object.create(Boolean.prototype);
        b.toString = function(){ return "own"; };
        console.log(b.toString());
        delete b.toString;
        // After delete, dispatch falls through to Boolean.prototype.toString.
        // Should NOT recurse / stack-overflow.
        console.log(typeof b.toString === "function");
        console.log("OK");
    )JS");
    EXPECT_NE(out.find("own"),  std::string::npos) << out;
    EXPECT_NE(out.find("true"), std::string::npos) << out;
    EXPECT_NE(out.find("OK"),   std::string::npos) << out;
}

// =============================================================================
// 11. ClassMethodDispatch_HonorsProtoDeletedSentinel
// Mirrors test/js_props/class_method_dispatch_honors_proto_deletion.js.
// =============================================================================
TEST(Props, ClassMethodDispatch_HonorsProtoDeletedSentinel) {
    auto out = expect_ok("ClassMethodDispatch_HonorsProtoDeletedSentinel", R"JS(
        function Mid(){}
        Mid.prototype = Object.create({ greet: function(){ return "deep"; } });
        Mid.prototype.greet = function(){ return "mid"; };
        var inst = new Mid();
        console.log(inst.greet());
        delete Mid.prototype.greet;
        // Should fall through to deeper proto, not loop.
        console.log(inst.greet());
        console.log("OK");
    )JS");
    EXPECT_NE(out.find("mid"),  std::string::npos) << out;
    EXPECT_NE(out.find("deep"), std::string::npos) << out;
    EXPECT_NE(out.find("OK"),   std::string::npos) << out;
}

// =============================================================================
// 12. ConfigurableFalse_DefineThrows
// =============================================================================
TEST(Props, ConfigurableFalse_DefineThrows) {
    auto out = expect_ok("ConfigurableFalse_DefineThrows", R"JS(
        var obj = {};
        Object.defineProperty(obj, "x", { value: 1, configurable: false, writable: true });
        var threw = false;
        try {
            // changing configurability or accessor↔data is forbidden when !configurable
            Object.defineProperty(obj, "x", { get: function(){ return 2; } });
        } catch (e) { threw = (e instanceof TypeError); }
        console.log("threw=" + threw);
        console.log("x=" + obj.x);
        console.log("OK");
    )JS");
    EXPECT_NE(out.find("threw=true"), std::string::npos) << out;
    EXPECT_NE(out.find("x=1"),        std::string::npos) << out;
    EXPECT_NE(out.find("OK"),         std::string::npos) << out;
}

// =============================================================================
// 13. WritableFalse_StrictSetThrows
// =============================================================================
TEST(Props, WritableFalse_StrictSetThrows) {
    auto out = expect_ok("WritableFalse_StrictSetThrows", R"JS(
        "use strict";
        var obj = {};
        Object.defineProperty(obj, "x", { value: 1, writable: false, configurable: true });
        var threw = false;
        try { obj.x = 2; } catch (e) { threw = (e instanceof TypeError); }
        console.log("threw=" + threw);
        console.log("x=" + obj.x);
        console.log("OK");
    )JS");
    EXPECT_NE(out.find("threw=true"), std::string::npos) << out;
    EXPECT_NE(out.find("x=1"),        std::string::npos) << out;
    EXPECT_NE(out.find("OK"),         std::string::npos) << out;
}

// =============================================================================
// 14. NumericStringKey_EquivalentToIntKey
// Mirrors test/js_props/numeric_string_key_equivalence.js.
// =============================================================================
TEST(Props, NumericStringKey_EquivalentToIntKey) {
    auto out = expect_ok("NumericStringKey_EquivalentToIntKey", R"JS(
        var obj = {};
        obj[1] = "via-int";
        console.log(obj["1"]);
        obj["2"] = "via-str";
        console.log(obj[2]);
        delete obj["1"];
        console.log(obj[1] === undefined);
        console.log("OK");
    )JS");
    EXPECT_NE(out.find("via-int"), std::string::npos) << out;
    EXPECT_NE(out.find("via-str"), std::string::npos) << out;
    EXPECT_NE(out.find("true"),    std::string::npos) << out;
    EXPECT_NE(out.find("OK"),      std::string::npos) << out;
}

// =============================================================================
// 15. ProxyGetTrap_PreservesReceiver
// =============================================================================
TEST(Props, ProxyGetTrap_PreservesReceiver) {
    auto out = expect_ok("ProxyGetTrap_PreservesReceiver", R"JS(
        var target  = { x: 1 };
        var proxy = new Proxy(target, {
            get: function(t, key, receiver) {
                // receiver should be the proxy when accessed via proxy.x
                return (receiver === proxy) ? "proxy-receiver" : "wrong-receiver";
            }
        });
        console.log(proxy.x);
        console.log("OK");
    )JS");
    EXPECT_NE(out.find("proxy-receiver"), std::string::npos) << out;
    EXPECT_NE(out.find("OK"),             std::string::npos) << out;
}

// =============================================================================
// 16. SuperSet_FindsInheritedSetter
// Mirrors test/js_props/super_property_set_finds_inherited_setter.js.
// =============================================================================
TEST(Props, SuperSet_FindsInheritedSetter) {
    auto out = expect_ok("SuperSet_FindsInheritedSetter", R"JS(
        var captured = null;
        class Base {
            set x(v) { captured = "base:" + v; }
        }
        class Derived extends Base {
            assign(v) { super.x = v; }
        }
        new Derived().assign(42);
        console.log(captured);
        console.log("OK");
    )JS");
    EXPECT_NE(out.find("base:42"), std::string::npos) << out;
    EXPECT_NE(out.find("OK"),      std::string::npos) << out;
}

// =============================================================================
// 17. SiblingMapSharingTypeMap_NonWritablePerMap
// A2-T4 invariant: two `new Foo()` instances share a TypeMap via the
// per-callsite shape cache (transpile_js_mir.cpp §7). When one instance
// freezes a property non-writable via Object.defineProperty, the other
// instance must still be writable. The Map-local TypeMap clone (A2-T1+T2)
// is what makes this safe.
// =============================================================================
TEST(Props, SiblingMapSharingTypeMap_NonWritablePerMap) {
    auto out = expect_ok("SiblingMapSharingTypeMap_NonWritablePerMap", R"JS(
        function Foo() { this.x = 1; this.y = 2; }
        var a = new Foo();
        var b = new Foo();
        Object.defineProperty(a, 'x', { writable: false });
        b.x = 99;
        console.log("a.x=" + a.x);
        console.log("b.x=" + b.x);
        console.log("OK");
    )JS");
    // a.x must remain 1 (non-writable on a); b.x must update to 99.
    EXPECT_NE(out.find("a.x=1"),  std::string::npos) << out;
    EXPECT_NE(out.find("b.x=99"), std::string::npos) << out;
    EXPECT_NE(out.find("OK"),     std::string::npos) << out;
}

// =============================================================================
// 18. AccessorToData_NoSiblingCorruption
// A2-T3 invariant: clearing JSPD_IS_ACCESSOR on one Map's shape entry must
// not cause sibling Maps (sharing the original TypeMap) to lose their own
// accessor dispatch. Two instances are created from the same constructor
// callsite, then one redefines an accessor as a data property; the sibling
// must keep dispatching to the accessor.
// =============================================================================
TEST(Props, AccessorToData_NoSiblingCorruption) {
    auto out = expect_ok("AccessorToData_NoSiblingCorruption", R"JS(
        function Foo() {
            Object.defineProperty(this, 'x', {
                get: function() { return 'getter'; },
                configurable: true
            });
        }
        var a = new Foo();
        var b = new Foo();
        // Convert a.x from accessor to data property — must not affect b.x.
        Object.defineProperty(a, 'x', { value: 'data', writable: true, configurable: true });
        console.log("a.x=" + a.x);
        console.log("b.x=" + b.x);
        console.log("OK");
    )JS");
    EXPECT_NE(out.find("a.x=data"),    std::string::npos) << out;
    EXPECT_NE(out.find("b.x=getter"),  std::string::npos) << out;
    EXPECT_NE(out.find("OK"),          std::string::npos) << out;
}

// =============================================================================
// 19. CloneIdempotent_NoDoubleAlloc
// A2-T1 idempotency: repeated attribute mutations on the same Map must not
// trigger repeated TypeMap cloning. Verified observationally: after the
// first defineProperty on `a`, subsequent mutations apply in place to the
// same private TypeMap (is_private_clone == true short-circuit). We can't
// directly observe the TypeMap pointer from JS, but we can verify that
// many mutations succeed without corrupting state — a regression in the
// idempotency check would surface as either a crash or stale flag reads.
// =============================================================================
TEST(Props, CloneIdempotent_NoDoubleAlloc) {
    auto out = expect_ok("CloneIdempotent_NoDoubleAlloc", R"JS(
        // Each mutation targets a distinct property to avoid
        // ES re-define restrictions (configurable=false→writable change rules).
        function Foo() { this.a = 1; this.b = 2; this.c = 3; }
        var o = new Foo();
        Object.defineProperty(o, 'a', { writable: false });
        Object.defineProperty(o, 'b', { enumerable: false });
        Object.defineProperty(o, 'c', { configurable: false });
        var da = Object.getOwnPropertyDescriptor(o, 'a');
        var db = Object.getOwnPropertyDescriptor(o, 'b');
        var dc = Object.getOwnPropertyDescriptor(o, 'c');
        console.log("a.writable="     + da.writable);
        console.log("b.enumerable="   + db.enumerable);
        console.log("c.configurable=" + dc.configurable);
        // Sibling created from same callsite must have all defaults.
        var s = new Foo();
        var sa = Object.getOwnPropertyDescriptor(s, 'a');
        var sb = Object.getOwnPropertyDescriptor(s, 'b');
        var sc = Object.getOwnPropertyDescriptor(s, 'c');
        console.log("s.a.writable="     + sa.writable);
        console.log("s.b.enumerable="   + sb.enumerable);
        console.log("s.c.configurable=" + sc.configurable);
        console.log("OK");
    )JS");
    EXPECT_NE(out.find("a.writable=false"),     std::string::npos) << out;
    EXPECT_NE(out.find("b.enumerable=false"),   std::string::npos) << out;
    EXPECT_NE(out.find("c.configurable=false"), std::string::npos) << out;
    EXPECT_NE(out.find("s.a.writable=true"),     std::string::npos) << out;
    EXPECT_NE(out.find("s.b.enumerable=true"),   std::string::npos) << out;
    EXPECT_NE(out.find("s.c.configurable=true"), std::string::npos) << out;
    EXPECT_NE(out.find("OK"),                    std::string::npos) << out;
}

// =============================================================================
// 20. Tombstone_DeletePropagatesAcrossReaders (A2-T8 foundation)
// Verifies that deleting a property leaves it unobservable to all standard
// observers (in/hasOwnProperty/getOwnPropertyDescriptor/Object.keys/for-in).
// Acts as a regression gate for the upcoming JSPD_DELETED bit migration:
// readers must keep agreeing on tombstone semantics whether they consult the
// slot sentinel, the shape bit, or both.
// =============================================================================
TEST(Props, Tombstone_DeletePropagatesAcrossReaders) {
    auto out = expect_ok("Tombstone_DeletePropagatesAcrossReaders", R"JS(
        var o = { a: 1, b: 2, c: 3 };
        delete o.b;
        console.log("in="              + ('b' in o));
        console.log("hasOwn="          + o.hasOwnProperty('b'));
        console.log("desc="            + (Object.getOwnPropertyDescriptor(o, 'b') === undefined));
        console.log("keys="            + Object.keys(o).join(','));
        var seen = [];
        for (var k in o) seen.push(k);
        console.log("forin="           + seen.join(','));
        // Re-defining after delete must work (clears the tombstone).
        o.b = 42;
        console.log("revived="         + o.b);
        console.log("revived.in="      + ('b' in o));
        console.log("OK");
    )JS");
    EXPECT_NE(out.find("in=false"),       std::string::npos) << out;
    EXPECT_NE(out.find("hasOwn=false"),   std::string::npos) << out;
    EXPECT_NE(out.find("desc=true"),      std::string::npos) << out;
    EXPECT_NE(out.find("keys=a,c"),       std::string::npos) << out;
    EXPECT_NE(out.find("forin=a,c"),      std::string::npos) << out;
    EXPECT_NE(out.find("revived=42"),     std::string::npos) << out;
    EXPECT_NE(out.find("revived.in=true"),std::string::npos) << out;
    EXPECT_NE(out.find("OK"),             std::string::npos) << out;
}

// =============================================================================
// 21. J41_5_TypedArrayExotic_AbstractOps
// TypedArray hooks for [[HasProperty]], [[Delete]], [[OwnPropertyKeys]], and
// [[GetOwnProperty]] must remain explicit around ordinary property storage.
// =============================================================================
TEST(Props, J41_5_TypedArrayExotic_AbstractOps) {
    auto out = expect_ok("J41_5_TypedArrayExotic_AbstractOps", R"JS(
        var ta = new Uint8Array(2);
        ta[0] = 11;
        ta.extra = "own";
        var d0 = Object.getOwnPropertyDescriptor(ta, "0");
        console.log("has0=" + (0 in ta));
        console.log("has2=" + (2 in ta));
        console.log("desc0=" + d0.value + ":" + d0.enumerable + ":" + d0.configurable);
        console.log("names=" + Object.getOwnPropertyNames(ta).join(","));
        console.log("del0=" + (delete ta[0]));
        console.log("del99=" + (delete ta[99]));
        console.log("extra=" + ta.extra);
        console.log("OK");
    )JS");
    EXPECT_NE(out.find("has0=true"),        std::string::npos) << out;
    EXPECT_NE(out.find("has2=false"),       std::string::npos) << out;
    EXPECT_NE(out.find("desc0=11:true:true"), std::string::npos) << out;
    EXPECT_NE(out.find("names=0,1,extra"),  std::string::npos) << out;
    EXPECT_NE(out.find("del0=false"),       std::string::npos) << out;
    EXPECT_NE(out.find("del99=true"),       std::string::npos) << out;
    EXPECT_NE(out.find("extra=own"),        std::string::npos) << out;
    EXPECT_NE(out.find("OK"),               std::string::npos) << out;
}

// =============================================================================
// 22. J41_5_ProxyExotic_AbstractOps
// Proxy traps should remain explicit hooks for has/delete/ownKeys/GOPD.
// =============================================================================
TEST(Props, J41_5_ProxyExotic_AbstractOps) {
    auto out = expect_ok("J41_5_ProxyExotic_AbstractOps", R"JS(
        var target = {};
        Object.defineProperty(target, "x", { value: 7, configurable: true });
        var p = new Proxy(target, {
            has: function(t, k) { console.log("trap.has=" + k); return k === "x"; },
            deleteProperty: function(t, k) { console.log("trap.delete=" + k); return true; },
            ownKeys: function(t) { console.log("trap.keys"); return ["x"]; },
            getOwnPropertyDescriptor: function(t, k) {
                console.log("trap.gopd=" + k);
                return { value: 9, writable: true, enumerable: true, configurable: true };
            }
        });
        console.log("has=" + ("x" in p));
        console.log("del=" + (delete p.x));
        console.log("keys=" + Reflect.ownKeys(p).join(","));
        console.log("desc=" + Object.getOwnPropertyDescriptor(p, "x").value);
        console.log("OK");
    )JS");
    EXPECT_NE(out.find("trap.has=x"),    std::string::npos) << out;
    EXPECT_NE(out.find("has=true"),      std::string::npos) << out;
    EXPECT_NE(out.find("trap.delete=x"), std::string::npos) << out;
    EXPECT_NE(out.find("del=true"),      std::string::npos) << out;
    EXPECT_NE(out.find("trap.keys"),     std::string::npos) << out;
    EXPECT_NE(out.find("keys=x"),        std::string::npos) << out;
    EXPECT_NE(out.find("trap.gopd=x"),   std::string::npos) << out;
    EXPECT_NE(out.find("desc=9"),        std::string::npos) << out;
    EXPECT_NE(out.find("OK"),            std::string::npos) << out;
}

// =============================================================================
// 23. J41_5_ArrayAndStringExotic_AbstractOps
// Arrays and strings keep their indexed exotic behavior while ordinary named
// properties and descriptors continue through the shared property readers.
// =============================================================================
TEST(Props, J41_5_ArrayAndStringExotic_AbstractOps) {
    auto out = expect_ok("J41_5_ArrayAndStringExotic_AbstractOps", R"JS(
        var a = [10, 20];
        delete a[0];
        a.note = "arr";
        console.log("array.has0=" + (0 in a));
        console.log("array.has1=" + (1 in a));
        console.log("array.names=" + Object.getOwnPropertyNames(a).join(","));
        console.log("array.desc0=" + (Object.getOwnPropertyDescriptor(a, "0") === undefined));
        console.log("array.note=" + Object.getOwnPropertyDescriptor(a, "note").value);
        var s = "abc";
        var sd = Object.getOwnPropertyDescriptor(s, "1");
        console.log("str.has1=" + ("1" in Object(s)));
        console.log("str.names=" + Object.getOwnPropertyNames(s).join(","));
        console.log("str.desc=" + sd.value + ":" + sd.enumerable + ":" + sd.configurable);
        console.log("OK");
    )JS");
    EXPECT_NE(out.find("array.has0=false"),     std::string::npos) << out;
    EXPECT_NE(out.find("array.has1=true"),      std::string::npos) << out;
    EXPECT_NE(out.find("array.names=1,length,note"), std::string::npos) << out;
    EXPECT_NE(out.find("array.desc0=true"),     std::string::npos) << out;
    EXPECT_NE(out.find("array.note=arr"),       std::string::npos) << out;
    EXPECT_NE(out.find("str.has1=true"),        std::string::npos) << out;
    EXPECT_NE(out.find("str.names=0,1,2,length"), std::string::npos) << out;
    EXPECT_NE(out.find("str.desc=b:true:false"), std::string::npos) << out;
    EXPECT_NE(out.find("OK"),                   std::string::npos) << out;
}

// =============================================================================
// 24. J41_4_BuiltinRegistry_DescriptorSynthesis
// Registry-backed prototype methods and accessors should expose descriptors from
// the same table data used for lookup/installation.
// =============================================================================
TEST(Props, J41_4_BuiltinRegistry_DescriptorSynthesis) {
    auto out = expect_ok("J41_4_BuiltinRegistry_DescriptorSynthesis", R"JS(
        function data(label, obj, name) {
            var d = Object.getOwnPropertyDescriptor(obj, name);
            console.log(label + "=" + d.value.name + ":" + d.value.length + ":" +
                d.writable + ":" + d.enumerable + ":" + d.configurable);
        }
        function accessor(label, obj, name) {
            var d = Object.getOwnPropertyDescriptor(obj, name);
            console.log(label + "=" + d.get.name + ":" + (d.set === undefined) + ":" +
                d.enumerable + ":" + d.configurable);
        }
        data("date", Date.prototype, "getTime");
        data("regexp", RegExp.prototype, "exec");
        data("map", Map.prototype, "set");
        data("set", Set.prototype, "add");
        data("promise", Promise.prototype, "then");
        data("arrayLocale", Array.prototype, "toLocaleString");
        data("numberLocale", Number.prototype, "toLocaleString");
        data("symbolValue", Symbol.prototype, "valueOf");
        data("stringAlias", String.prototype, "trimLeft");
        var tap = Object.getPrototypeOf(Uint8Array.prototype);
        data("typedArray", tap, "set");
        accessor("typedArrayLength", tap, "length");
        accessor("dataViewByteLength", DataView.prototype, "byteLength");
        console.log("names.has.mapSet=" + (Object.getOwnPropertyNames(Map.prototype).indexOf("set") >= 0));
        console.log("names.has.taLength=" + (Object.getOwnPropertyNames(tap).indexOf("length") >= 0));
        console.log("OK");
    )JS");
    EXPECT_NE(out.find("date=getTime:0:true:false:true"), std::string::npos) << out;
    EXPECT_NE(out.find("regexp=exec:1:true:false:true"), std::string::npos) << out;
    EXPECT_NE(out.find("map=set:2:true:false:true"), std::string::npos) << out;
    EXPECT_NE(out.find("set=add:1:true:false:true"), std::string::npos) << out;
    EXPECT_NE(out.find("promise=then:2:true:false:true"), std::string::npos) << out;
    EXPECT_NE(out.find("arrayLocale=toLocaleString:0:true:false:true"), std::string::npos) << out;
    EXPECT_NE(out.find("numberLocale=toLocaleString:0:true:false:true"), std::string::npos) << out;
    EXPECT_NE(out.find("symbolValue=valueOf:0:true:false:true"), std::string::npos) << out;
    EXPECT_NE(out.find("stringAlias=trimStart:0:true:false:true"), std::string::npos) << out;
    EXPECT_NE(out.find("typedArray=set:1:true:false:true"), std::string::npos) << out;
    EXPECT_NE(out.find("typedArrayLength=get length:true:false:true"), std::string::npos) << out;
    EXPECT_NE(out.find("dataViewByteLength=get byteLength:true:false:true"), std::string::npos) << out;
    EXPECT_NE(out.find("names.has.mapSet=true"), std::string::npos) << out;
    EXPECT_NE(out.find("names.has.taLength=true"), std::string::npos) << out;
    EXPECT_NE(out.find("OK"), std::string::npos) << out;
}
