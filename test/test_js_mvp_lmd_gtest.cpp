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
#include "lib/hashmap.h"
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
        ASSERT_TRUE(get_type_id(result) == LMD_TYPE_FLOAT || get_type_id(result) == LMD_TYPE_INT) << source;
        if (isnan(expected)) EXPECT_TRUE(isnan(it2d(result))) << source;
        else EXPECT_EQ(it2d(result), expected) << source;
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
    char* dump(const char* path) {
        create_dir("temp");
        FILE* output = file_open_regular_write(path, true);
        if (!output) return NULL;
        mvp_lmd_dump(execution, output); fclose(output);
        return read_text_file(path);
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
TEST_F(JsMvpLmd, IntegerRuntimeSubtype) {
    Item result = run("function id(x){return x} var f=id; [1, f(2), f(1/1), f(0/1), -0, 1.5, NaN, Infinity]");
    ASSERT_EQ(get_type_id(result), LMD_TYPE_ARRAY);
    Array* values = (Array*)result.item;
    ASSERT_EQ(values->length, 8);
    EXPECT_EQ(get_type_id(values->items[0]), LMD_TYPE_INT);
    EXPECT_EQ(get_type_id(values->items[1]), LMD_TYPE_INT);
    for (int i = 2; i < 8; i++) EXPECT_EQ(get_type_id(values->items[i]), LMD_TYPE_FLOAT);
    boolean("function f(x){return typeof x==='number' && x===1 && x=='1'} var g=f; g(1) && g(1/1)");
    boolean("var a=[1,1/1,0,0/1,NaN]; a[0]===a[1] && a[2]===a[3] && a[4]!==a[4]");
    boolean("function f(x){return x?true:false} var g=f; g(1) && !g(0) && !g(NaN)");
    boolean("var a=[1]; var i=0; a[i]=5e-324; var old=a[i]; a[i]=2; a[i]===2 && old===5e-324");
    numeric("function f(){let x=5;return -x} var g=f; function h(x){return x+1} h(f())", -4);
    result = run("function f(){let x=5;let fractional=0.5;return x+1} f()");
    EXPECT_EQ(get_type_id(result), LMD_TYPE_INT);
    EXPECT_EQ(it2d(result), 6);
    result = run("var a;function f(x){a=[1];return x/2} f(2);a");
    ASSERT_EQ(get_type_id(result), LMD_TYPE_ARRAY);
    EXPECT_EQ(get_type_id(((Array*)result.item)->items[0]), LMD_TYPE_INT);
}
TEST_F(JsMvpLmd, IntegerWideningAndZeroSign) {
    numeric("function f(a,b){return a+b} var g=f; g(9007199254740991,1)", 9007199254740992.0);
    numeric("function f(a,b){return a-b} var g=f; g(-9007199254740991,1)", -9007199254740992.0);
    numeric("function f(a,b){return a*b} var g=f; g(4503599627370496,4096)", 18446744073709551616.0);
    boolean("function f(a,b){return a%b} var g=f; 1/g(-8,2)===-Infinity && g(8,0)!==g(8,0)");
    boolean("function f(a,b){return a*b} var g=f; 1/g(-1,0)===-Infinity && 1/g(0,-1)===-Infinity");
    boolean("function f(x){return -x} var g=f; 1/g(0)===-Infinity && 1/g(-0)===Infinity");
    boolean("function f(x){return x+1} var g=f; (g(9007199254740992)-9007199254740992)===0");
    boolean("var n=0; function a(){n++;return 9007199254740991} function b(){n++;return 1} a()+b()===9007199254740992 && n===2");
    numeric("var x=9007199254740991; var old=x++; old===9007199254740991 && x===9007199254740992 ? 1 : 0", 1);
    numeric("function f(x){return x/2} f(5)", 2.5);
    numeric("function maybe(x){if(x)return 1} function f(){return maybe(false)+1} f()", NAN);
}
TEST_F(JsMvpLmd, IntegerLoopProofsAndInvalidation) {
    numeric("function f(n){let i=n;let s=0;while(i>=0){s+=i;i--}return s} f(10000)", 50005000);
    Item result = run("function f(n){let i=n;let s=0;while(i>=0){s+=i;i--}return s} f(10000)");
    EXPECT_EQ(get_type_id(result), LMD_TYPE_INT);
    char* mir = dump("temp/mvp_lmd_integer_loop.mir");
    ASSERT_NE(mir, nullptr);
    EXPECT_EQ(strstr(mir, "dadd"), nullptr);
    EXPECT_EQ(strstr(mir, "dsub"), nullptr);
    mem_free(mir);
    numeric("function f(x,y){let q=0;let r=x;while(r>=y){r-=y;q++}return q} f(10000,2)", 5000);
    numeric("function f(){let s=9007199254740990;for(let j=0;j<3;j++){for(let i=0;i<2;i++){s+=1}}return s} f()", 9007199254740992.0);
    numeric("function f(){let i=10;let s=9007199254740991;do{s+=1;i++}while(i<0);return s} f()", 9007199254740992.0);
    numeric("function f(){let i=0;let s=0;while(i<3){s+=1;if(i===1){i++;continue}i++}return s} f()", 3);
    numeric("function f(){let x=1;for(let i=0;i<3;i++){x+=0.5}return x} f()", 2.5);
    numeric("function f(n){let q=0;let i=0;while(i<n){q++;i++}return q} f(3)+f(3.5)", 7);
    numeric("function f(){let i=0;let s=9007199254740991;while(i<(s+=1)){i=9007199254740992}return s} f()", 9007199254740992.0);
    boolean("function f(){var y=x;var x=1;return y} f()===undefined");
    numeric("function h(x){return x+1} function f(){let s=9007199254740991;let out=0;let i=0;while(i<1){out=h(s--);i++}return out} f()", 9007199254740992.0);
    boolean("function h(x){return x+9007199254740990} function f(){let i=-9007199254740991;while(i<=2){i+=3002399751580331}return h(i)} f()-9007199254740990===3002399751580334");
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
TEST_F(JsMvpLmd, InlinedNumericRegions) {
    // integer callees can run inside a floating caller without changing JS rounding or snapshots.
    numeric("function quotient(x,y){let q=0;let r=x;while(r>=y){r-=y;q++}return q} "
        "function work(){let s=0.25;for(let i=0;i<4;i++){s+=quotient(20,2)}return s} work()", 40.25);
    numeric("function step(x){let before=x++;return before+x} "
        "function work(){let s=0;for(let i=0;i<2;i++){s+=step(0.5)}return s} work()", 4);
    numeric("function step(x){let before=x++;return before+x} "
        "function work(){let s=0;for(let i=0;i<2;i++){s=step(9007199254740991)}return s} work()",
        18014398509481984.0);
    boolean("function step(x){let before=x++;return 1/before} "
        "function work(){let s=0;for(let i=0;i<2;i++){s=step(-0)}return s} work()===-Infinity");
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
    numeric("4294967297.75 | 0", 1);
    numeric("-4294967297.75 | 0", -1);
    numeric("9007199254740992 | 0", 0);
    numeric("9007199254740994 | 0", 2);
}
TEST_F(JsMvpLmd, GuardedIntegerRemainder) {
    numeric("function rem(x){return x%8} rem(4294967303)", 7);
    numeric("function rem(x){return x%4} rem(5.5)", 1.5);
    numeric("function rem(x){return x%4} rem(-5)", -1);
    numeric("function rem(x){return x%4} rem(9007199254740994)", 2);
    numeric("function rem(x){return x%4} rem('7')", 3);
    boolean("function rem(x){return x%2} 1/rem(-0)===-Infinity && 1/rem(-4)===-Infinity && 1/rem(4)===Infinity");
    boolean("function rem(x){return x%2} rem(NaN)!==rem(NaN) && rem(Infinity)!==rem(Infinity)");
    numeric("function rem(x){return x%1} rem(5e-324)", 5e-324);
    numeric("var n=5; function rem(){return n++%4} rem()+n", 7);
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
TEST_F(JsMvpLmd, ClosedParameterKindsAndSnapshots) {
    // complete parameter domains should keep array indexing off the property-name conversion path.
    const char* sources[] = {
        "function at(a,i){let v=a[i];return v} at([1.5,2],1)",
        "function at(a,i){let v=a[i];return v} function work(){let a=[1.5,2];let r;"
        "for(let i=0;i<2;i++){r=at(a,1)}return r} work()"
    };
    for (const char* source : sources) {
        numeric(source, 2);
        char* mir = dump("temp/mvp_lmd_parameter_array.mir");
        ASSERT_NE(mir, nullptr);
        EXPECT_EQ(strstr(mir, "\timport\tmvp_lmd_number_to_string"), nullptr);
        EXPECT_EQ(strstr(mir, "\timport\tmvp_lmd_property_key"), nullptr);
        mem_free(mir);
    }
    numeric("function at(a,i){return a[i]} at([2],0)+at({'0':3},0)", 5);
    boolean("function at(a,i){return a[i]} at([2],5)===undefined && at('ab',1)==='b'");
    boolean("function size(a){return a===undefined?0:a.length} size()+size([1,2])===2");
    numeric("function at(a,i){a={'0':3};return a[i]} at([2],0)", 3);
    numeric("function at(a){let old=a[0];a[0]=1e-323;return old} at([5e-324])", 5e-324);
    boolean("function at(a,i){return a[i]} at([2],-0)===2");
    error("function at(a,i){return a[i]} at([2],'-0')", "capability");
    error("function at(a,i){return a[i]} at([2],1.5)", "capability");
}
TEST_F(JsMvpLmd, GenericElementCoercion) {
    // every read stays generic: the array contains numbers and coercible non-number values.
    numeric("function sum(a){let s=0;for(let i=0;i<6;i++){s+=+a[i]}return s} "
        "sum([1.25,-2,true,false,null,'3.5',undefined])", 3.75);
    boolean("let a=[-0,0,5e-324,1e-323,Infinity,-Infinity,NaN,undefined]; "
        "1/(+a[0])===-Infinity && 1/(+a[1])===Infinity && +a[2]===5e-324 && "
        "+a[3]===1e-323 && +a[4]===Infinity && +a[5]===-Infinity && "
        "+a[6]!==+a[6] && +a[7]!==+a[7] && +a[20]!==+a[20]");
    boolean("let a=[2,'3','20']; a[0]+a[1]==='23' && a[0]<a[1] && a[2]<a[1]");
    error("let a=[{}]; +a[0]", "capability");
    error("let a=[[]]; +a[0]", "capability");
    error("let a=[new Map()]; +a[0]", "capability");
}
TEST_F(JsMvpLmd, OwnedElementSnapshots) {
    // independent homes survive source-slot reuse, binding reassignment and array growth.
    boolean("function work(a){let x=a[0];let y=x;a[0]=1e-323;x=a[0];"
        "for(let i=1;i<80;i++){a[i]=[i]}return y===5e-324 && x===1e-323} work([5e-324])");
    char* mir = dump("temp/mvp_lmd_owned_elements.mir");
    ASSERT_NE(mir, nullptr);
    EXPECT_EQ(strstr(mir, "\timport\tlambda_item_adopt_scalar_home"), nullptr);
    mem_free(mir);
    boolean("function change(a){a[0]=1e-323;return a[0]} function work(a){"
        "let first=a[0];let total=first+change(a);return first===5e-324 && total===1.5e-323} "
        "work([5e-324])");
    boolean("function work(a){let x=a[0];let y=x;x=1;return 1/y===-Infinity} work([-0])");
    numeric("function work(a){let x=a[0];let y=x;x=2;return y} work([1e-300])", 1e-300);
    numeric("let a=[5e-324,1e-323]; a[0]-a[1]", -5e-324);
    numeric("function change(a){a[0]=1e-323;return a[0]} let a=[5e-324]; a[0]-change(a)", -5e-324);
    numeric("let a=[5e-324]; a[0]-(a[0]=1e-323)", -5e-324);
    numeric("let a=[5e-324]; a[0]-(a[1]=1e-323)", -5e-324);
    numeric("let a=[5e-324]; function key(){a[0]=1e-323;return 0} a[0]-a[key()]", -5e-324);
    boolean("let a=['4',true,null,undefined]; a[0]/2===2 && a[1]*3===3 && a[2]-1===-1 && "
        "a[3]*2!==a[3]*2");
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
TEST_F(JsMvpLmd, RecursiveReturnKindsAndNativeBoundaries) {
    numeric("function fib(n){if(n<2)return n;return fib(n-1)+fib(n-2)} fib(20)", 6765);
    boolean("function a(n){if(n===0)return true;return b(n-1)} function b(n){return a(n)} a(20)===true");
    boolean("function f(x){if(x)return 1} f(true)===1 && f(false)===undefined");
    boolean("function f(x){if(x)return 's';return 1} f(true)==='s' && f(false)===1");
    boolean("function f(x){return x+1} function g(x){return f(x)} g(1)===2 && g('s')==='s1'");
    boolean("function f(x){return x} function g(){return f()} g()===undefined && f(1)===1");
    boolean("function f(){return ()=>false} f()()===false");
    boolean("function f(){return x;var x=1} f()===undefined");
    numeric("var s=0; function f(a,b){return a*10+b} f(++s,++s,++s)+s", 15);
    boolean("function id(x){return x} id(-0)===0 && 1/id(-0)===-Infinity && id(5e-324)===5e-324");
    numeric("function f(){return 5e-324} function g(){return [f(),f()]} g()[0]", 5e-324);
    error("function bad(){const x=1;x=2;return 3} function caller(){return bad()+1} caller()", "TypeError");
    error("function bad(){let a=[];a[1]=2;return true} bad()", "capability");
    run("function fib(n){if(n<2)return n;return fib(n-1)+fib(n-2)} fib(5)");
    char* source = dump("temp/mvp_lmd_native_calls.mir");
    ASSERT_NE(source, nullptr);
    EXPECT_NE(strstr(source, "mvp_lmd_f1:\tfunc\td,"), nullptr);
    EXPECT_EQ(strstr(source, "\timport\tmvp_lmd_string_"), nullptr);
    EXPECT_EQ(strstr(source, "\timport\tlambda_item_adopt_scalar_home"), nullptr);
    mem_free(source);
}
TEST_F(JsMvpLmd, SelfTailCallsPreserveActivationAndArguments) {
    numeric("function sum(n,a){if(n===0)return a;return sum(n-1,a+n)} sum(100000,0)", 5000050000.0);
    numeric("function swap(n,a,b){if(n===0)return a*10+b;return swap(n-1,b,a)} swap(100001,1,2)", 21);
    numeric("var count=0;function f(n,a){if(n===0)return a;return f(n-1,a+1,count++)} f(10000,0)+count", 20000);
    numeric("function f(n){var previous=x;var x=n;if(n===0)return previous===undefined?1:0;return f(n-1)} f(10000)", 1);
    numeric("var a=[];function f(n){let row=['x'+n];a[0]=row;if(n===0)return row.length;return f(n-1)} f(200)", 1);
    numeric("function f(n){if(n===0)return 1;return f(n-1)+1} f(20)", 21);
    error("function f(n){if(n===0){const x=1;x=2}return f(n-1)} f(10)", "TypeError");
    // exhaustion must be checked independently of the host's available recursion depth.
    error("function f(n){if(n===0)return 0;return f(n+1)+1} f(1)", "execution failed");
}
TEST_F(JsMvpLmd, RejectsUnsupportedUnitBeforeExecution) {
    error("if(false){({get a(){return 1}})}", "scope");
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
    char* source = dump("temp/mvp_lmd_test.mir");
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
    source = dump("temp/mvp_lmd_numeric.mir");
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
TEST_F(JsMvpLmd, PlainObjectDataProperties) {
    boolean("let x=3;let o={x,a:1,['b']:2,n:{c:4},u:undefined}; o.x===3 && o['a']===1 && o.b===2 && o.n.c===4 && o.missing===undefined && Object.hasOwn(o,'u') && !Object.hasOwn(o,'missing')");
    boolean("let o={x:1,x:2}; o.x+=3; let old=o.x++; o.x*=2; o.x===12 && old===5 && delete o.x && !('x' in o) && delete o.x");
    boolean("let o={a:0,b:null,c:2};o.a ||= 3;o.b ?""?= 4;o.c &&= 5; o.a===3 && o.b===4 && o.c===5");
    boolean("let o={};o[true]=1;o[null]=2;o[undefined]=3;o[-0]=4;o[1.5]=5; o.true===1 && o.null===2 && o.undefined===3 && o['0']===4 && o['1.5']===5");
    boolean("let o={f:(x)=>x+1}; o.f(2)===3 && typeof o==='object' && o===o && o!=={}");
    boolean("let o={a:5e-324};let old=o.a;o.a='s';o.a=== 's' && old===5e-324");
}
TEST_F(JsMvpLmd, ObjectEvaluationOrderAndImmutableShapes) {
    boolean("let calls=0;let o={x:1};function key(){calls++;return 'x'}function rhs(){o.x='changed';o.y=4;return 2}o[key()]+=rhs();calls===1 && o.x===3 && o.y===4");
    boolean("let o={x:1};let alias=o;function rhs(){o={x:9};return 2}alias.x=rhs();alias.x===2 && o.x===9");
    Item value = run("function make(){return {x:1,b:true}}let a=make();let b=make();a.x='s';[a,b,make()]");
    ASSERT_EQ(get_type_id(value), LMD_TYPE_ARRAY);
    Array* rows = value.array;
    EXPECT_NE(rows->items[0].map->type, rows->items[1].map->type);
    EXPECT_EQ(rows->items[1].map->type, rows->items[2].map->type);
    boolean("function make(){return {x:1,b:true}}let a=make();let b=make();a.x='s';b.x='t';a.x==='s' && b.x==='t'");
    value = run("function make(){return {x:1,b:true}}let a=make();let b=make();a.x='s';b.x='t';[a,b]");
    EXPECT_EQ(value.array->items[0].map->type, value.array->items[1].map->type);
    boolean("let a={x:1,y:2};let b={x:3,y:4};delete a.x;a.x=5;Object.keys(a)[0]==='y' && Object.keys(a)[1]==='x' && b.x===3");
    value = run("let o={x:1};delete o.x;o");
    ASSERT_EQ(get_type_id(value), LMD_TYPE_MAP);
    EXPECT_EQ(value.map->data, nullptr);
    heap_gc_collect();
    EXPECT_EQ(value.map->data, nullptr);
}
TEST_F(JsMvpLmd, ObjectProjectionAndCanonicalKeys) {
    boolean("let o={b:1,'10':10,'2':2,a:3,'01':4,'4294967295':5};let k=Object.keys(o);k.length===6 && k[0]==='2' && k[1]==='10' && k[2]==='b' && k[3]==='a' && k[4]==='01' && k[5]==='4294967295'");
    boolean("let o={'':1,'a\\0b':2};o['']===1 && o['a\\0b']===2 && Object.keys(o)[1].length===3");
    boolean("let hi='\\uD83D',lo='\\uDE00';let o={};o[hi+lo]=3;o['\\uD83D']=4;o['\\uDE00']=5;o['😀']===3 && o[hi]===4 && o[lo]===5 && Object.keys(o).length===3");
    boolean("let o={};o['é']=1;o['e\\u0301']=2;o['é']===1 && o['e\\u0301']===2");
    boolean("let o={a:1,b:2};let keys=Object.keys(o);let values=Object.values(o);let entries=Object.entries(o);o.a=9;o.c=3;keys.length===2 && values[0]===1 && entries[0][0]==='a' && entries[0][1]===1 && entries[0]!==entries[1]");
    numeric("let total=0;for(let [k,v] of Object.entries({a:1,b:2})){total+=v}total", 3);
}
TEST_F(JsMvpLmd, MapKeysAndMutation) {
    boolean("let m=new Map();m.set('a',1).set('b',undefined);m.size===2 && m.get('a')===1 && m.has('b') && m.get('b')===undefined && !m.has('c') && m.get('c')===undefined");
    boolean("let m=new Map();m.set(1,'int');m.set(1/1,'float');m.set(-0,'zero');m.set(0,'same');m.set(NaN,3);m.set(0/0,4);m.size===3 && m.get(1)==='float' && m.get(0)==='same' && m.get(NaN)===4");
    boolean("let m=new Map();let a={},b={},c=[],f=()=>1;m.set(a,1).set(b,2).set(c,3).set(f,4).set(null,5).set(undefined,6).set(true,7);m.size===7 && m.get(a)===1 && m.get(b)===2 && m.get(c)===3 && m.get(f)===4 && m.get(null)===5 && m.get(undefined)===6 && m.get(true)===7");
    boolean("let m=new Map();m.set('\\uD83D'+'\\uDE00',3);m.set('😀',4);m.size===1 && m.get('😀')===4");
    boolean("let m=new Map();m.x=9;m.set('x',1);Object.keys(m)[0]==='x' && m.x===9 && m.get('x')===1 && m.delete('x') && !m.delete('x') && m.x===9 && m.size===0");
    boolean("let m=new Map();m.set('x',5e-324);let old=m.get('x');m.set('x',2);old===5e-324 && m.get('x')===2 && m.clear()===undefined && m.size===0");
}
TEST_F(JsMvpLmd, LiveMapIteration) {
    numeric("let m=new Map();m.set('a',1).set('b',2);let sum=0;for(let [k,v] of m){sum+=v}sum", 3);
    boolean("let m=new Map();m.set('a',1).set('b',2).set('c',3);let s='';for(let [k,v] of m.entries()){s+=k+v;if(k==='a'){m.delete('b');m.set('c',4);m.set('d',5)}}s==='a1c4d5'");
    boolean("let m=new Map();m.set('a',1).set('b',2);let s='';for(let k of m.keys()){s+=k;if(k==='a'){m.delete('b');m.set('b',3)}}s==='ab'");
    numeric("let m=new Map();m.set('a',1).set('b',2);let s=0;for(let v of m.values()){s+=v;if(v===1){m.clear();m.set('c',4)}}s", 5);
    numeric("let m=new Map();m.set(1,1).set(2,2);let n=0;for(let a of m.keys()){for(let b of m.values()){n+=a*b}}n", 9);
    boolean("let m=new Map();m.set('a',1).set('b',2);let a=[];for(let pair of m){a[a.length]=pair}a[0]!==a[1] && a[0][0]==='a' && a[1][0]==='b'");
    numeric("let m=new Map();m.set(1,1).set(2,2).set(3,3);let s=0;outer:for(let v of m.values()){if(v===1)continue outer;s+=v;if(v===2)break outer}s", 2);
}
TEST_F(JsMvpLmd, ObjectAndMapScopeBoundary) {
    error("({get x(){return 1}})", "scope");
    error("({__proto__:null})", "scope");
    error("let o={};o.__proto__", "scope");
    error("let o={};o['__'+'proto__']=1", "capability");
    error("new Map([])", "capability");
    error("let m=new Map();let f=m.get", "capability");
    error("let m=new Map();m.set=()=>1", "capability");
    error("let o={};o.toString", "capability");
    error("let o={};o[{}]=1", "capability");
    error("for(let k in {}){}", "scope");
    numeric("let Object={keys:()=>7};Object.keys({})", 7);
    numeric("let Map=()=>7;Map()", 7);
    error("let Map=()=>7;new Map()", "capability");
}
static void unexpected_js_native_gc(void*, gc_heap_t*) { js_gc_callback_entries++; }
static void unexpected_js_native_destroy(void*) { js_gc_callback_entries++; }
static void (*ordered_index_free)(void*);
static int ordered_index_frees;
static void count_ordered_index_free(void* allocation) {
    ordered_index_frees++;
    ordered_index_free(allocation);
}
TEST_F(JsMvpLmd, OrderedMapPreciseEdgesAndNativeCleanup) {
    Item result = run("let m=new Map();let o={x:5e-324};m.set(o,[o,()=>1]);m.set('wide',5e-324);m");
    ASSERT_EQ(get_type_id(result), LMD_TYPE_MAP);
    EXPECT_EQ(result.map->map_kind, MAP_KIND_ORDERED);
    EXPECT_EQ(context_capsule(context, CONTEXT_CAPSULE_JS_RUNTIME), nullptr);
    gc_heap_t* gc = context->heap->gc;
    js_gc_callback_entries = 0;
    gc->js_native_trace = unexpected_js_native_gc;
    gc->js_native_destroy = unexpected_js_native_destroy;
    heap_gc_collect();
    EXPECT_EQ(js_gc_callback_entries, 0);
    String* wide = heap_strcpy("wide", 4);
    Item read = mvp_lmd_map_call(result, Item{.item = mvp_lmd_method_token(1)}, Item{.item = s2it(wide)}, ItemNull);
    EXPECT_EQ(it2d(read), 5e-324);
    // replace only these indices' allocator callbacks to observe sweep and teardown.
    Item abandoned = mvp_lmd_object_new((TypeMap*)result.map->type, 1);
    ASSERT_EQ(get_type_id(abandoned), LMD_TYPE_MAP);
    OrderedMap* dead = (OrderedMap*)abandoned.map;
    ordered_index_free = dead->index->_free; ordered_index_frees = 0;
    dead->index->_free = count_ordered_index_free;
    heap_gc_collect();
    EXPECT_GT(ordered_index_frees, 0);
    ((OrderedMap*)result.map)->index->_free = count_ordered_index_free;
    int swept_frees = ordered_index_frees;
    mvp_lmd_destroy(execution); execution = NULL;
    EXPECT_GT(ordered_index_frees, swept_frees);
    EXPECT_EQ(js_gc_callback_entries, 0);
}
TEST_F(JsMvpLmd, ObjectShapeGuardsAndDirectMapPairs) {
    // retain observable identity so this fixture still exercises shape guards after scalar replacement.
    numeric("let o={x:1};let alias=o;let s=0;for(let i=0;i<100;i++){alias.x++;s+=o.x}s", 5150);
    char* mir = dump("temp/mvp_lmd_object_fields.mir");
    ASSERT_NE(mir, nullptr);
    // finalized MIR renames registers; retain the register-to-register shape guard.
    bool guarded = false;
    for (const char* line = mir; (line = strstr(line, "\tbne\t")); line++) {
        const char* end = strchr(line, '\n');
        const char* first = strstr(line, ", %r");
        const char* second = first ? strstr(first + 1, ", %r") : NULL;
        if (second && end && second < end) guarded = true;
    }
    EXPECT_TRUE(guarded);
    EXPECT_EQ(strstr(mir, "\timport\tjs_"), nullptr);
    mem_free(mir);
    boolean("let o={x:1,b:true};function rhs(){delete o.x;o.c=3;o.x='changed';return 2}o.x=rhs();o.x===2 && o.b && o.c===3");
    error("let m=new Map();m.x=1;m.x()", "TypeError");
    numeric("let m=new Map();m.set(1,2).set(3,4);let s=0;for(let [k,v] of m){s+=k*v}s", 14);
    mir = dump("temp/mvp_lmd_direct_map_pairs.mir");
    ASSERT_NE(mir, nullptr);
    EXPECT_EQ(strstr(mir, "\timport\tmvp_lmd_map_entry"), nullptr);
    mem_free(mir);
    boolean("let o={};o.self=o;let m=new Map();m.set(m,o);m.get(m).self===o");
}
TEST_F(JsMvpLmd, CollectionProjectionBindingAndGrowth) {
    numeric("let Object={entries:()=>[[1,2],[3,4]]};let s=0;for(let [k,v] of Object.entries({})){s+=k*v}s", 14);
    numeric("let o={keys:()=>[2,3]};let s=0;for(let k of o.keys()){s+=k}s", 5);
    numeric("let m=new Map();m.set(2,3);let o={keys:()=>m,values:()=>m};let s=0;for(let pair of o.keys()){s+=pair[0]+pair[1]}for(let [k,v] of o.values()){s+=k*v}s", 11);
    boolean("let v=1;let m=new Map();m.set(1,'text');for(v of m.values()){}v==='text'");
    boolean("let m=new Map();for(let i=0;i<1000;i++){m.set('k'+i,{i})}for(let i=0;i<1000;i+=2){m.delete('k'+i)}let s=0;for(let [k,v] of m){s+=v.i}m.size===500 && s===250000");
    numeric("let m=new Map();m.set(1,5e-324);let s=0;for(let [k,v] of m){m.clear();s=v}s", 5e-324);
    Item result = run("let o={};o['\\uD83D'+'\\uDE00']=7;o['a\\0b']=9;o");
    ASSERT_EQ(get_type_id(result), LMD_TYPE_MAP);
    const char cesu[] = "\xed\xa0\xbd\xed\xb8\x80";
    String* raw = heap_strcpy(cesu, 6);
    EXPECT_EQ(it2i(map_get(result.map, Item{.item = s2it(raw)})), 7);
    raw = heap_strcpy("a\0b", 3);
    EXPECT_EQ(it2i(map_get(result.map, Item{.item = s2it(raw)})), 9);
}
TEST_F(JsMvpLmd, MapCursorCleanupAndTombstoneCompaction) {
    Item result = run("let m=new Map();m.set(1,1);function f(){for(let pair of m){return pair}}f();for(let k of m.keys()){break}for(let i=0;i<300;i++){m.set(i,i);m.delete(i)}m.set('live',5e-324);m");
    ASSERT_EQ(get_type_id(result), LMD_TYPE_MAP);
    OrderedMap* map = (OrderedMap*)result.map;
    EXPECT_EQ(map->cursors, 0);
    EXPECT_LT(map->entries->length, 140);
    boolean("let m=new Map();m.set(1,1);outer:for(let a of m){for(let b of m){break outer}}m.clear();m.set(2,2);let s=0;for(let [k,v] of m){s+=k+v}s===4");
    numeric("let m=new Map();m.set(1,1).set(2,2);let s=0;for(let [k,v] of m){m.delete(k);m.set(k+2,v+2);s+=v;if(k===4)break}s", 10);
}
