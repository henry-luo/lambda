#include <gtest/gtest.h>
#include "lambda/js/mvp-lmd/mvp_lmd.h"
#include "lambda/js/mvp-lmd/mvp_lmd_runtime.h"
#include "lambda/runtime/runtime-state.h"
#include "lambda/runtime/context_capsule.h"
#include "lambda/runtime/heap_api.h"
#include "lambda/runtime/transpiler.hpp"
#include "lambda/runtime/gc/gc_heap.h"
#include "lib/file.h"
#include "lib/mem.h"
#include <math.h>
#include <string.h>

class JsMvpLmd : public ::testing::Test {
protected:
    MvpLmdExecution* execution = NULL;
    void TearDown() override { mvp_lmd_destroy(execution); }
    Item run(const char* source) {
        mvp_lmd_destroy(execution);
        execution = mvp_lmd_execute(source, strlen(source));
        const char* diagnostic = mvp_lmd_diagnostic(execution);
        EXPECT_EQ(diagnostic, nullptr) << source << "\n" << (diagnostic ? diagnostic : "");
        return mvp_lmd_result(execution);
    }
    void numeric(const char* source, double expected) {
        Item result = run(source);
        ASSERT_EQ(get_type_id(result), LMD_TYPE_FLOAT) << source;
        if (isnan(expected)) EXPECT_TRUE(isnan(result.get_double())) << source;
        else EXPECT_EQ(result.get_double(), expected) << source;
    }
    void boolean(const char* source) {
        Item result = run(source);
        ASSERT_EQ(get_type_id(result), LMD_TYPE_BOOL) << source;
        EXPECT_TRUE(result.item & 1) << source;
    }
    void error(const char* source, const char* expected) {
        mvp_lmd_destroy(execution);
        execution = mvp_lmd_execute(source, strlen(source));
        ASSERT_NE(mvp_lmd_diagnostic(execution), nullptr) << source;
        EXPECT_NE(strstr(mvp_lmd_diagnostic(execution), expected), nullptr) << source;
        EXPECT_EQ(get_type_id(mvp_lmd_result(execution)), LMD_TYPE_ERROR);
    }
};
TEST_F(JsMvpLmd, ScalarNumbers) {
    numeric("1 + 2 * 3 / 2", 4);
    numeric("2 ** 3 ** 2", 512);
    numeric("(-1) ** Infinity", NAN);
    numeric("NaN ** 0", 1);
    numeric("-5 % 2", -1);
    numeric("1 / 0", INFINITY);
    numeric("0 / 0", NAN);
    numeric("9007199254740992 + 1", 9007199254740992.0);
    Item negative = run("-0"); EXPECT_TRUE(signbit(negative.get_double()));
    numeric("function tiny(){return 5e-324} tiny()", 5e-324);
}
TEST_F(JsMvpLmd, PrimitiveConversions) {
    numeric("+'' + +null + +true", 1);
    numeric("+'  0x10  ' + +'0b11' + +'0o10'", 27);
    numeric("+'\\uFEFF\\u2000-0\\u2029'", -0.0);
    numeric("+'-0x1'", NAN);
    numeric("+'1_0'", NAN);
    numeric("+'1\\u0000'", NAN);
    numeric("+'1e999'", INFINITY);
    numeric("+'0x1000000000000081'", 1152921504606847232.0);
    numeric("+'1e-999'", 0);
    boolean("+'Infinity' === Infinity && +undefined !== +undefined");
}
TEST_F(JsMvpLmd, BitwiseAndShifts) {
    numeric("4294967297 | 0", 1);
    numeric("-1 >>> 0", 4294967295.0);
    numeric("2147483648 >> 0", -2147483648.0);
    numeric("1 << 33", 2);
    numeric("~4294967296", -1);
    numeric("NaN | Infinity | -Infinity", 0);
    numeric("1e100 | 0", 0);
    numeric("-4294967297 & 3", 3);
}
TEST_F(JsMvpLmd, EqualityAndTruth) {
    boolean("null == undefined && null !== undefined && 0 == false && '1' == true");
    boolean("'x' === 'x' && '1' !== 1 && NaN !== NaN && 0 === -0");
    boolean("!NaN && !undefined && !null && !0 && !'' && !![]");
    boolean("typeof [] === 'object' && typeof null === 'object' && typeof missing === 'undefined'");
    boolean("typeof (()=>1) === 'function' && typeof 1 === 'number'");
    boolean("var a=[]; var b=a; a===b && a!==[]");
    boolean("[] != (()=>1) && (()=>1) != [] && [] != []");
    numeric("let n=0; false && n++; true || n++; null ?? n++; n", 1);
    numeric("let n=0; n ||= 2; n &&= 3; n ?" "?= 4; n", 3);
}
TEST_F(JsMvpLmd, StringsUseUtf16) {
    boolean("'a' + 1 === 'a1' && 'x' + null === 'xnull' && '' + undefined === 'undefined'");
    boolean("'' + 1e21 === '1e+21' && '' + 1e-7 === '1e-7' && '' + -0 === '0'");
    boolean("'😀'.length === 2 && '😀'[0] === '\\uD83D' && '😀'[1] === '\\uDE00'");
    boolean("'😀' === ('\\uD83D' + '\\uDE00') && '😀' < '\\uE000'");
    boolean("'a\\u0000b'.length === 3 && 'a\\u0000b'[1] === '\\u0000'");
    boolean("'abc'[3] === undefined && 'abc'['1'] === 'b'");
}
TEST_F(JsMvpLmd, MutableBindingsAndHoisting) {
    boolean("var a = x; var x=1; var x; a === undefined && x === 1");
    boolean("var x=1; x='s'; x=false; x=[]; typeof x === 'object'");
    numeric("let x=1; {let x=2; x++} x", 1);
    numeric("let x=1; const y=[]; y[0]=x++; y[1]=++x; y[0]+y[1]", 4);
    boolean("function f(){var a=x; var x=1; var x; return a===undefined && x===1} f()");
    boolean("{let undefined=3; let NaN=4; let Infinity=5; undefined+NaN+Infinity===12}");
    boolean("var NaN=1; var Infinity=2; var undefined=3; NaN!==NaN && Infinity===1/0 && typeof undefined==='undefined'");
    numeric("function f(){var Infinity=3; return Infinity} f()", 3);
    numeric("var x=1; x+=(x=2); x", 3);
    boolean("let x='a'; let y=(x+=1); y==='a1'");
    boolean("let x=false; let y=(x&&=[]); y===false");
    error("'use strict'; var NaN=1", "TypeError");
    error("'use strict'; missing=1", "ReferenceError");
    error("let NaN=1", "SyntaxError");
    error("function Infinity(){}", "SyntaxError");
    error("let x=x", "ReferenceError");
    error("typeof x; let x=1", "ReferenceError");
    error("const x=1; x=2", "TypeError");
    error("f(); let x=1; function f(){return x}", "ReferenceError");
    boolean("var a=f(); var x=1; function f(){return x} a===undefined");
}
TEST_F(JsMvpLmd, DenseArrayAliasesAndMutation) {
    numeric("var a=[1,2]; var b=a; b[0]=3; a[2]=4; a.length=2; a[2]=5; a[0]+a[2]", 8);
    boolean("var a=[[],true,null,undefined,'s']; a[0][0]=a; a[0][0]===a && a[3]===undefined");
    boolean("var a=[1]; a[-0]=2; a['0']===2 && a[7]===undefined && a['length']===1");
    numeric("var a=[1]; function f(){a[1]=2; a[2]=3; return 4} a[0]+=f(); a[0]+a.length", 8);
    numeric("var n=0; var a=[1]; function key(){n++;return 0} a[key()]++; a[key()]+=2; a[0]+n", 6);
    numeric("var a=[5e-324]; var x=a[0]; a[0]=1e-323; x", 5e-324);
    boolean("var a=[1,2]; a.length=0; a[0]=3; a.length===1 && a[1]===undefined");
}
TEST_F(JsMvpLmd, ArrayCapabilityAndRangeFailures) {
    error("var a=[]; a[1]=2", "capability");
    error("var a=[]; a.length=1", "capability");
    error("var a=[]; a['01']=1", "capability");
    error("var a=[]; a['-0']=1", "capability");
    error("var a=[]; a[4294967295]=1", "capability");
    error("var a=[]; a.length=-1", "RangeError");
    error("var a=[]; a.length=1.5", "RangeError");
    error("var a=[]; a.length=NaN", "RangeError");
}
TEST_F(JsMvpLmd, StandardControlFlow) {
    numeric("let s=0; for(let i=0;i<10;i++){if(i===3)continue;if(i===8)break;s+=i} s", 25);
    numeric("let i=0; let s=0; while(i<3){s+=++i} do{s++}while(false); s", 7);
    numeric("let x=0; switch(2){case 1:x=1;break;case 2:x=2;case 3:x+=3;break;default:x=9} x", 5);
    numeric("let x=0; switch(3){default:x=2;case 1:x+=1;case 3:x+=4} x", 4);
    error("switch(2){case 1:let x=1;case 2:x}", "ReferenceError");
    numeric("switch(1){case 1:let x=1;case 2:x}", 1);
    numeric("let s=0; outer:for(let i=0;i<3;i++){for(let j=0;j<3;j++){if(j===1)continue outer;s++}} s", 3);
    numeric("let x=0; done:{x++;break done;x++} x", 1);
    numeric("a:b:for(let i=0;i<2;i++){if(i===0)continue a;break b} 1", 1);
    numeric("true ? 7 : 8", 7);
    numeric("let x=0; (x=1,x+=2,x)", 3);
    EXPECT_EQ(run("1; if(false)2").item, ITEM_JS_UNDEFINED);
    EXPECT_EQ(run("1; while(false){2}").item, ITEM_JS_UNDEFINED);
    EXPECT_EQ(run("1; switch(0){case 1:2}").item, ITEM_JS_UNDEFINED);
    numeric("1; {var x=2}", 1);
}
TEST_F(JsMvpLmd, FunctionsAndMutableTargets) {
    numeric("function f(n){if(n<=1)return 1;return n*f(n-1)} f(6)", 720);
    boolean("function even(n){return n===0 || odd(n-1)} function odd(n){return n!==0 && even(n-1)} even(10)");
    numeric("function f(x){return x+1} var g=f; f=(x)=>x+2; g(2)+f(2)", 7);
    boolean("function f(x){return x} f('s')==='s' && f([]).length===0 && f()===undefined");
    numeric("var f=function self(n){return n===0?0:self(n-1)+1}; f(5)", 5);
    numeric("function f(x){x+=2;return x} f(1,2,3)", 3);
    boolean("function f() {1;2;} f()===undefined");
    numeric("{function f(){return 1}} f()", 1);
    numeric("if(true) function f(){return 2} f()", 2);
    boolean("function f(x){return x+1} f(1)===2 && f('s')==='s1'");
    boolean("function f(x){x='s';return x} f(1)==='s'");
    boolean("function create(){return ()=>1} var a=create(); var b=create(); a!==b && a===a");
    numeric("var n=0; function f(){return (x)=>x} f()(++n)+n", 2);
}
TEST_F(JsMvpLmd, RejectsUnsupportedUnitBeforeExecution) {
    error("if(false){({a:1})}", "scope");
    error("function f(){return ()=>x; var x=1} 1", "capture");
    error("{let x=1; function f(){return x}} 1", "capture");
    error("function f(a=1){return a} 1", "scope");
    error("function f(){return this} 1", "scope");
    error("[1,,2]", "scope");
    error("var x=1; with([]){x=2}", "scope");
    error("try{1}catch(e){2}", "scope");
    error("async function f(){return 1} 1", "scope");
}
TEST_F(JsMvpLmd, ExactRootsAndBoundedScalarHomes) {
    numeric("var a=[5e-324]; function f(x){return x} for(let i=0;i<10000;i++){a[0]=f(a[0]);} a[0]", 5e-324);
    ASSERT_NE(context, nullptr);
    EXPECT_EQ(context->side_root_top, context->side_root_base);
    EXPECT_LT(context->side_number_top - context->side_number_base, 4);
    heap_gc_collect();
    EXPECT_EQ(mvp_lmd_result(execution).get_double(), 5e-324);
    boolean("var a=[]; for(let i=0;i<200;i++){a[i]=['x'+i,()=>1]} a.length===200");
}
TEST_F(JsMvpLmd, ImportBoundaryAndOneBodyPerFunction) {
    run("function f(x){return x+1} var g=f; g(2)");
    create_dir("temp");
    FILE* dump = file_open_regular_write("temp/mvp_lmd_test.mir", true);
    ASSERT_NE(dump, nullptr); mvp_lmd_dump(execution, dump); fclose(dump);
    char* source = read_text_file("temp/mvp_lmd_test.mir");
    ASSERT_NE(source, nullptr);
    EXPECT_EQ(strstr(source, "\timport\tjs_"), nullptr);
    EXPECT_EQ(strstr(source, "mvp_runtime_"), nullptr);
    EXPECT_EQ(strstr(source, "lambda_dynamic_call"), nullptr);
    EXPECT_NE(strstr(source, "mvp_lmd_f1"), nullptr);
    int bodies = 0;
    for (const char* body = source; (body = strstr(body, ":\tfunc\t")); body++) bodies++;
    EXPECT_EQ(bodies, 2);
    mem_free(source);
    run("function f(x){return x+1} f(2)");
    dump = file_open_regular_write("temp/mvp_lmd_numeric.mir", true);
    ASSERT_NE(dump, nullptr); mvp_lmd_dump(execution, dump); fclose(dump);
    source = read_text_file("temp/mvp_lmd_numeric.mir");
    ASSERT_NE(source, nullptr);
    EXPECT_EQ(strstr(source, "\timport\tmvp_lmd_string_"), nullptr);
    EXPECT_EQ(strstr(source, "\timport\tmvp_lmd_number_to_string"), nullptr);
    EXPECT_EQ(strstr(source, "\timport\tmvp_lmd_function_new"), nullptr);
    mem_free(source);
}
static int js_gc_callback_entries;
static int unexpected_js_function_gc(void*, gc_heap_t*) {
    js_gc_callback_entries++;
    return 0;
}
TEST_F(JsMvpLmd, SharedGcDoesNotEnterJsCallbacks) {
    run("var f=()=>1; var a=[f,['x']]; a");
    ASSERT_NE(context, nullptr);
    EXPECT_EQ(context_capsule(context, CONTEXT_CAPSULE_JS_RUNTIME), nullptr);
    gc_heap_t* gc = context->heap->gc;
    js_gc_callback_entries = 0;
    gc->js_function_trace = unexpected_js_function_gc;
    gc->js_function_compact = unexpected_js_function_gc;
    heap_gc_collect();
    EXPECT_EQ(js_gc_callback_entries, 0);
    EXPECT_EQ(get_type_id(mvp_lmd_result(execution)), LMD_TYPE_ARRAY);
}
