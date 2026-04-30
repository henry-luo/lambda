// =============================================================================
// test_js_coerce_gtest.cpp
//
// Stage-B-style harness for the J39-1 ToPrimitive / OrdinaryToPrimitive
// coercion kernel (lambda/js/js_coerce.{h,cpp}).
//
// Each TEST exercises one row of the {hint default | number | string} ×
// {@@toPrimitive | OrdinaryToPrimitive} × {primitive result | object result |
// non-callable | throws} matrix described in vibe/jube/Transpile_Js39.md
// §J39-1. Drives the engine via subprocess (./lambda.exe js) so it
// validates the full path including JIT lowering.
//
// These tests are the inner loop for any change to js_to_primitive or to
// the call sites that re-use it (js_add, comparisons, js_to_number,
// js_to_string, js_to_numeric, the Date constructor).
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

constexpr const char* kTempDir = "./temp/js_coerce";

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
    std::string output;
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

    const char* marker = strstr(r.output.c_str(), "##### Script");
    if (marker) {
        const char* nl = strchr(marker, '\n');
        if (nl) r.output = std::string(nl + 1);
    }

    while (!r.output.empty() &&
           (r.output.back() == '\n' || r.output.back() == '\r' ||
            r.output.back() == ' '  || r.output.back() == '\t')) {
        r.output.pop_back();
    }
    return r;
}

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
// 1. SymbolToPrimitive_HintDefault — `+` operator triggers ToPrimitive(default)
// =============================================================================
TEST(Coerce, SymbolToPrimitive_HintDefault) {
    auto out = expect_ok("SymbolToPrimitive_HintDefault", R"JS(
        var seen = "";
        var obj = { [Symbol.toPrimitive]: function(hint) { seen = hint; return 7; } };
        var sum = obj + 1;
        console.log("hint=" + seen);
        console.log("sum=" + sum);
        console.log("OK");
    )JS");
    EXPECT_NE(out.find("hint=default"), std::string::npos) << out;
    EXPECT_NE(out.find("sum=8"),        std::string::npos) << out;
    EXPECT_NE(out.find("OK"),           std::string::npos) << out;
}

// =============================================================================
// 2. SymbolToPrimitive_HintNumber — unary `+` triggers ToPrimitive(number)
// =============================================================================
TEST(Coerce, SymbolToPrimitive_HintNumber) {
    auto out = expect_ok("SymbolToPrimitive_HintNumber", R"JS(
        var seen = "";
        var obj = { [Symbol.toPrimitive]: function(hint) { seen = hint; return 5; } };
        var v = +obj;
        console.log("hint=" + seen);
        console.log("v=" + v);
        console.log("OK");
    )JS");
    EXPECT_NE(out.find("hint=number"), std::string::npos) << out;
    EXPECT_NE(out.find("v=5"),         std::string::npos) << out;
    EXPECT_NE(out.find("OK"),          std::string::npos) << out;
}

// =============================================================================
// 3. SymbolToPrimitive_HintString — String(obj) triggers ToPrimitive(string)
// =============================================================================
TEST(Coerce, SymbolToPrimitive_HintString) {
    auto out = expect_ok("SymbolToPrimitive_HintString", R"JS(
        var seen = "";
        var obj = { [Symbol.toPrimitive]: function(hint) { seen = hint; return "hi"; } };
        var s = String(obj);
        console.log("hint=" + seen);
        console.log("s=" + s);
        console.log("OK");
    )JS");
    EXPECT_NE(out.find("hint=string"), std::string::npos) << out;
    EXPECT_NE(out.find("s=hi"),        std::string::npos) << out;
    EXPECT_NE(out.find("OK"),          std::string::npos) << out;
}

// =============================================================================
// 4. SymbolToPrimitive_ReturnsObject_TypeError
// =============================================================================
TEST(Coerce, SymbolToPrimitive_ReturnsObject_TypeError) {
    auto out = expect_ok("SymbolToPrimitive_ReturnsObject_TypeError", R"JS(
        var obj = { [Symbol.toPrimitive]: function() { return {}; } };
        var threw = false, name = "";
        try { var x = obj + 1; }
        catch (e) { threw = true; name = e && e.name; }
        console.log("threw=" + threw);
        console.log("name=" + name);
        console.log("OK");
    )JS");
    EXPECT_NE(out.find("threw=true"),    std::string::npos) << out;
    EXPECT_NE(out.find("name=TypeError"), std::string::npos) << out;
    EXPECT_NE(out.find("OK"),            std::string::npos) << out;
}

// =============================================================================
// 5. SymbolToPrimitive_NotCallable_TypeError
// =============================================================================
TEST(Coerce, SymbolToPrimitive_NotCallable_TypeError) {
    auto out = expect_ok("SymbolToPrimitive_NotCallable_TypeError", R"JS(
        var obj = { [Symbol.toPrimitive]: 42 };
        var threw = false, name = "";
        try { var x = obj + 1; }
        catch (e) { threw = true; name = e && e.name; }
        console.log("threw=" + threw);
        console.log("name=" + name);
        console.log("OK");
    )JS");
    EXPECT_NE(out.find("threw=true"),    std::string::npos) << out;
    EXPECT_NE(out.find("name=TypeError"), std::string::npos) << out;
    EXPECT_NE(out.find("OK"),            std::string::npos) << out;
}

// =============================================================================
// 6. OrdinaryToPrimitive_HintNumber_ValueOfFirst
// =============================================================================
TEST(Coerce, OrdinaryToPrimitive_HintNumber_ValueOfFirst) {
    auto out = expect_ok("OrdinaryToPrimitive_HintNumber_ValueOfFirst", R"JS(
        var calls = [];
        var obj = {
            valueOf:  function() { calls.push("v"); return 11; },
            toString: function() { calls.push("t"); return "x"; }
        };
        var v = +obj;       // hint=number → valueOf first
        console.log("v=" + v);
        console.log("calls=" + calls.join(","));
        console.log("OK");
    )JS");
    EXPECT_NE(out.find("v=11"),   std::string::npos) << out;
    EXPECT_NE(out.find("calls=v"), std::string::npos) << out;
    EXPECT_NE(out.find("OK"),      std::string::npos) << out;
}

// =============================================================================
// 7. OrdinaryToPrimitive_HintString_ToStringFirst
// =============================================================================
TEST(Coerce, OrdinaryToPrimitive_HintString_ToStringFirst) {
    auto out = expect_ok("OrdinaryToPrimitive_HintString_ToStringFirst", R"JS(
        var calls = [];
        var obj = {
            valueOf:  function() { calls.push("v"); return 11; },
            toString: function() { calls.push("t"); return "X"; }
        };
        var s = String(obj);  // hint=string → toString first
        console.log("s=" + s);
        console.log("calls=" + calls.join(","));
        console.log("OK");
    )JS");
    EXPECT_NE(out.find("s=X"),     std::string::npos) << out;
    EXPECT_NE(out.find("calls=t"), std::string::npos) << out;
    EXPECT_NE(out.find("OK"),      std::string::npos) << out;
}

// =============================================================================
// 8. OrdinaryToPrimitive_ValueOfReturnsObject_FallsToToString
// =============================================================================
TEST(Coerce, OrdinaryToPrimitive_ValueOfReturnsObject_FallsToToString) {
    auto out = expect_ok("OrdinaryToPrimitive_ValueOfReturnsObject_FallsToToString", R"JS(
        var calls = [];
        var obj = {
            valueOf:  function() { calls.push("v"); return {}; },
            toString: function() { calls.push("t"); return "Y"; }
        };
        var s = obj + "";   // hint=default → valueOf, then toString
        console.log("s=" + s);
        console.log("calls=" + calls.join(","));
        console.log("OK");
    )JS");
    EXPECT_NE(out.find("s=Y"),      std::string::npos) << out;
    EXPECT_NE(out.find("calls=v,t"), std::string::npos) << out;
    EXPECT_NE(out.find("OK"),       std::string::npos) << out;
}

// =============================================================================
// 9. OrdinaryToPrimitive_BothReturnObject_TypeError
// =============================================================================
TEST(Coerce, OrdinaryToPrimitive_BothReturnObject_TypeError) {
    auto out = expect_ok("OrdinaryToPrimitive_BothReturnObject_TypeError", R"JS(
        var obj = {
            valueOf:  function() { return {}; },
            toString: function() { return {}; }
        };
        var threw = false, name = "";
        try { var x = obj + 1; }
        catch (e) { threw = true; name = e && e.name; }
        console.log("threw=" + threw);
        console.log("name=" + name);
        console.log("OK");
    )JS");
    EXPECT_NE(out.find("threw=true"),    std::string::npos) << out;
    EXPECT_NE(out.find("name=TypeError"), std::string::npos) << out;
    EXPECT_NE(out.find("OK"),            std::string::npos) << out;
}

// =============================================================================
// 10. SymbolToPrimitive_OverridesValueOfAndToString
// =============================================================================
TEST(Coerce, SymbolToPrimitive_OverridesValueOfAndToString) {
    auto out = expect_ok("SymbolToPrimitive_OverridesValueOfAndToString", R"JS(
        var calls = [];
        var obj = {
            [Symbol.toPrimitive]: function(h) { calls.push("p:" + h); return 100; },
            valueOf:  function() { calls.push("v"); return 1; },
            toString: function() { calls.push("t"); return "x"; }
        };
        var v = +obj;
        console.log("v=" + v);
        console.log("calls=" + calls.join(","));
        console.log("OK");
    )JS");
    EXPECT_NE(out.find("v=100"),         std::string::npos) << out;
    EXPECT_NE(out.find("calls=p:number"), std::string::npos) << out;
    EXPECT_NE(out.find("OK"),            std::string::npos) << out;
}

// =============================================================================
// 11. AddString_TriggersDefaultHint — string concat on object operand
// =============================================================================
TEST(Coerce, AddString_TriggersDefaultHint) {
    auto out = expect_ok("AddString_TriggersDefaultHint", R"JS(
        var seen = "";
        var obj = { [Symbol.toPrimitive]: function(h) { seen = h; return "abc"; } };
        var s = obj + "Z";
        console.log("hint=" + seen);
        console.log("s=" + s);
        console.log("OK");
    )JS");
    EXPECT_NE(out.find("hint=default"), std::string::npos) << out;
    EXPECT_NE(out.find("s=abcZ"),       std::string::npos) << out;
    EXPECT_NE(out.find("OK"),           std::string::npos) << out;
}

// =============================================================================
// 12. ToNumber_ObjectWithValueOfReturningSymbol_TypeError
// =============================================================================
TEST(Coerce, ToNumber_SymbolToPrimitiveReturnsSymbol_TypeError) {
    auto out = expect_ok("ToNumber_SymbolToPrimitiveReturnsSymbol_TypeError", R"JS(
        var sym = Symbol("s");
        var obj = { [Symbol.toPrimitive]: function() { return sym; } };
        var threw = false, name = "";
        try { var x = +obj; }
        catch (e) { threw = true; name = e && e.name; }
        console.log("threw=" + threw);
        console.log("name=" + name);
        console.log("OK");
    )JS");
    EXPECT_NE(out.find("threw=true"),    std::string::npos) << out;
    EXPECT_NE(out.find("name=TypeError"), std::string::npos) << out;
    EXPECT_NE(out.find("OK"),            std::string::npos) << out;
}

// =============================================================================
// 13. CompareLT_TriggersNumberHint — `<` operator does ToPrimitive(number)
// =============================================================================
TEST(Coerce, CompareLT_TriggersNumberHint) {
    auto out = expect_ok("CompareLT_TriggersNumberHint", R"JS(
        var seen = "";
        var obj = { [Symbol.toPrimitive]: function(h) { seen = h; return 3; } };
        var b = obj < 5;
        console.log("hint=" + seen);
        console.log("b=" + b);
        console.log("OK");
    )JS");
    EXPECT_NE(out.find("hint=number"), std::string::npos) << out;
    EXPECT_NE(out.find("b=true"),      std::string::npos) << out;
    EXPECT_NE(out.find("OK"),          std::string::npos) << out;
}

// =============================================================================
// 14. WrapperFastPath_BoxedNumberArithmetic
// Boxed primitives (Number wrapper) without custom valueOf/toString must
// round-trip through ToPrimitive and yield the underlying number.
// =============================================================================
TEST(Coerce, WrapperFastPath_BoxedNumberArithmetic) {
    auto out = expect_ok("WrapperFastPath_BoxedNumberArithmetic", R"JS(
        var n = new Number(42);
        var r = n + 8;
        console.log("r=" + r);
        console.log("typeof=" + typeof r);
        console.log("OK");
    )JS");
    EXPECT_NE(out.find("r=50"),       std::string::npos) << out;
    EXPECT_NE(out.find("typeof=number"), std::string::npos) << out;
    EXPECT_NE(out.find("OK"),         std::string::npos) << out;
}

// =============================================================================
// 15. ArrayToString_HintDefault — Array implicitly stringifies via toString.
// `[1,2,3] + ""` → "1,2,3"
// =============================================================================
TEST(Coerce, ArrayToString_HintDefault) {
    auto out = expect_ok("ArrayToString_HintDefault", R"JS(
        var s = [1,2,3] + "";
        console.log("s=" + s);
        console.log("OK");
    )JS");
    EXPECT_NE(out.find("s=1,2,3"), std::string::npos) << out;
    EXPECT_NE(out.find("OK"),      std::string::npos) << out;
}
