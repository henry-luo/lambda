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
    Item run(const char* source, const MvpLmdHost* host = NULL) {
        mvp_lmd_destroy(execution);
        execution = mvp_lmd_execute(source, strlen(source), NULL, host);
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
TEST_F(JsMvpLmd, HostClockOutputAndFiles) {
    ASSERT_TRUE(create_dir("temp"));
    FILE* output = file_open_regular_write("temp/mvp_host_output.txt", true);
    ASSERT_NE(output, nullptr);
    const char* arguments[] = {"lambda.exe", "host.js", "123"};
    MvpLmdHost host = {3, arguments, output};
    Item result = run(R"JS(
        const first=performance.now(); const clock=performance.now;
        const fs=require('fs');
        fs.writeFileSync('temp/mvp_host_file.txt','a\u0000b');
        console.log('host',123,true,null,undefined);
        process.stdout.write(fs.readFileSync('temp/mvp_host_file.txt','utf8'));
        typeof clock==='function' && clock()>=first && first>=0 &&
          process.argv.length===3 && process.argv[2]==='123';
    )JS", &host);
    EXPECT_EQ(result.item, ITEM_TRUE);
    ASSERT_EQ(fclose(output), 0);
    char* bytes = NULL; size_t size = 0;
    ASSERT_TRUE(file_read_all("temp/mvp_host_output.txt", MEM_CAT_TEMP, &bytes, &size));
    const char expected[] = "host 123 true null undefined\na\0b";
    EXPECT_EQ(size, sizeof(expected) - 1);
    if (size == sizeof(expected) - 1) EXPECT_EQ(memcmp(bytes, expected, size), 0);
    mem_free(bytes);
}
TEST_F(JsMvpLmd, PublicStaticFields) {
    boolean(R"JS(
        let order='';
        function mark(v){order+=v;return v;}
        class A { static first=mark('a'); static second=A.first+mark('b'); static self=this; }
        class B extends A { static third=B.second+mark('c'); }
        let ok=order==='abc' && A.second==='ab' && B.third==='abc' && A.self===A;
        B.first=7; ok=ok && A.first==='a' && B.first===7;
        delete B.first; ok=ok && B.first==='a';
        A.first=8; delete A.second; A.second=9;
        ok && Object.keys(A).join(',')==='first,self,second' && Object.keys(B).join(',')==='third';
    )JS");
    error("class A { static x=outside(); } function outside(){return A;} A", "ReferenceError");
}
TEST_F(JsMvpLmd, Utf16RangesSearchAndSplit) {
    boolean(R"JS(
        let text='a\ud83d\ude00b';
        text.substring(3,1)==='\ud83d\ude00' && text.slice(-3,-1)==='\ud83d\ude00' &&
        text.substring(1,2)==='\ud83d' && text.slice(2,3)==='\ude00' &&
        text.substring(NaN,undefined)===text && text.slice(3,1)==='' &&
        text.indexOf('\ude00')===2 && text.indexOf('b',Infinity)===-1 &&
        text.startsWith('\ude00',2) && text.startsWith('',Infinity) &&
        text.split('').length===4 && text.split('')[1]==='\ud83d' &&
        'a,b,'.split(',').join('|')==='a|b|' && ',a'.split(',',1)[0]==='' &&
        'a,b'.split(undefined)[0]==='a,b' && 'anullb'.split(null)[1]==='b' &&
        ''.split('').length===0 && ''.split(',').length===1 &&
        'a'.split(',',0).length===0 && 'abc'.toUpperCase()==='ABC' &&
        Number(' 12.5 ')===12.5 && String(-0)==='0';
    )JS");
}
TEST_F(JsMvpLmd, MapCallbackSharesLiveTraversal) {
    boolean(R"JS(
        let values=new Array(4); values[1]=3; values[3]=5; let calls=0;
        let result=values.map(function(value,index,array){
            calls++; if(index===1){array[2]=4;array.push(7);} return value*index;
        });
        calls===3 && result.length===4 && !(0 in result) &&
        result[1]===3 && result[2]===8 && result[3]===15 && values.length===5;
    )JS");
}
TEST_F(JsMvpLmd, IntegerParsingAndFixedRounding) {
    boolean(R"JS(
        parseInt('  -0x10tail')===-16 && parseInt('11',2)===3 &&
        parseInt('123xyz',10)===123 && parseInt('11',4294967298)===3 &&
        parseInt('xyz')!==parseInt('xyz') && 1/parseInt('-0')===-Infinity &&
        (1.25).toFixed(1)==='1.3' && (1.75).toFixed(1)==='1.8' &&
        (-1.25).toFixed(1)==='-1.3' && (1.005).toFixed(2)==='1.00' &&
        (2.55).toFixed(1)==='2.5' && (-0).toFixed(2)==='0.00' &&
        (-0.001).toFixed(2)==='-0.00' && (Infinity).toFixed(3)==='Infinity' &&
        (1e21).toFixed(2)==='1e+21' && Math.round(-1.5)===-1 &&
        Math.round(1.5)===2 && 1/Math.round(-0.1)===-Infinity &&
        1/Math.round(-0)===-Infinity;
    )JS");
    error("(NaN).toFixed(101)", "RangeError");
}
TEST_F(JsMvpLmd, ArrayMutationAndEntrySpread) {
    boolean(R"JS(
        let a=new Array(5); a[0]=3; a[2]=undefined; a[4]=1;
        let alias=a; let reverse=a.reverse();
        let ok=reverse===a && alias[0]===1 && !(1 in a) && (2 in a) && a[4]===3;
        let sorted=a.sort(function(x,y){return x-y;});
        ok=ok && sorted===a && a[0]===1 && a[1]===3 && (2 in a) && !(3 in a);
        let ties=[{k:1,v:'a'},{k:0,v:'b'},{k:1,v:'c'}];
        ties.sort(function(x,y){return x.k-y.k;});
        let map=new Map(); map.set('b',2); map.set('a',1); map.delete('b'); map.set('b',3);
        let pairs=[0,...map.entries(),4]; let keys=[...map.keys()]; let values=[...map.values()];
        let holes=[...new Array(2)];
        ok && ties[1].v==='a' && ties[2].v==='c' && pairs.length===4 && pairs[1][0]==='a' &&
          pairs[2][1]===3 && keys.join(',')==='a,b' && values.join(',')==='1,3' &&
          (0 in holes) && holes[0]===undefined && [9,80,2].sort().join(',')==='2,80,9';
    )JS");
    error("[2,1].sort(function(){throw 'compare failed';})", "compare failed");
}
TEST_F(JsMvpLmd, ReorderedArrayScalarHomes) {
    boolean("let a=[5e-324,1e-323];a.reverse();a[0]=2e-323;a[1]===5e-324");
    boolean("let a=[5e-324,7,1e-323].slice();a[0]=2e-323;a[2]===1e-323");
    boolean("let a=Object.values({a:7,b:5e-324,c:1e-323});a[0]=2e-323;a[1]===5e-324 && a[2]===1e-323");
    boolean("let a=Object.entries({a:5e-324})[0];a[0]=1e-323;a[1]===5e-324");
    boolean(R"JS(
        let a=[5e-324,1e-323,7];a.reverse();let saved=a[2];
        a[0]=a[2];a[1]=2e-323;a[100]=a[2];a[2]=1e-323;
        a.sort(function(x,y){return x-y;});
        saved===5e-324 && a[0]===5e-324 && a[1]===5e-324 &&
          a[2]===1e-323 && a[3]===2e-323 && !(4 in a);
    )JS");
    Item result = run(R"JS(
        let a=new Array(4);a.tag='kept';a[3]=1e-323;
        for(let i=0;i<200;i++){a[0]=0;a[0]=5e-324;}
        a.tag==='kept' ? a : [];
    )JS");
    ASSERT_EQ(get_type_id(result), LMD_TYPE_ARRAY);
    ASSERT_EQ(result.array->length, 4);
    EXPECT_EQ(result.array->items[0].get_double(), 5e-324);
    EXPECT_EQ(result.array->items[1].item, ITEM_JS_DELETED_SENTINEL);
    EXPECT_EQ(result.array->items[3].get_double(), 1e-323);
    EXPECT_LE(result.array->extra, 2 * result.array->length + 1);
    heap_gc_collect();
    result = mvp_lmd_result(execution);
    EXPECT_EQ(result.array->items[0].get_double(), 5e-324);
    EXPECT_EQ(result.array->items[3].get_double(), 1e-323);
    boolean("let a=[5e-324,1e-323];a.length=0;a[3]=2e-323;a[3]===2e-323 && !(0 in a)");
}
TEST_F(JsMvpLmd, OrdinaryConstructionAndPrototypeData) {
    boolean(R"JS(
        function Point(x){this.x=x;}
        Point.extra=5; Point.prototype.get=function(){return this.x;};
        const p=new Point(3); let ok=p instanceof Point && p.constructor===Point && p.get()===3;
        Point.prototype.get=function(){return this.x+Point.extra;};
        ok=ok && p.get()===8;
        Point.prototype.tag='base'; p.tag='own'; delete p.tag;
        function ReturnObject(){this.x=1;return {x:9};}
        function ReturnNumber(){this.x=2;return 99;}
        function factory(value){return function(){this.get=function(){return value;};};}
        let A=factory(7), B=factory(8), a=new A(), b=new B();
        ok && p.tag==='base' && new ReturnObject().x===9 && new ReturnNumber().x===2 &&
          a.get()===7 && b.get()===8 && A.prototype!==B.prototype && !(a instanceof B) &&
          Object.keys(Point).join(',')==='extra';
    )JS");
    error("function F(){} F.prototype={};", "capability");
    error("let f=()=>1;new f();", "TypeError");
}
TEST_F(JsMvpLmd, OrdinaryReceiversAndNamedArrays) {
    boolean("function put(a,k,v){a[k]=v}let a=[1];put(a,0,2);put(a,-1,3);put(a,1.5,4);put(a,NaN,5);put(a,4294967295,6);a.length===1 && a[0]===2 && a[-1]===3 && a[1.5]===4 && a.NaN===5 && a[4294967295]===6");
    boolean(R"JS(
        function bare(){this.answer=42;return this;}
        let global=bare();
        function strict(){'use strict';return this;}
        function Box(value){this.value=value;this.read=()=>this.value;}
        let box=new Box(7); let read=box.read;
        let a=[]; a.label={value:3}; a[-1]=4; a[2]=5;
        let alias=a; a.label='changed'; delete a[-1];
        let ok=alias.label==='changed' && a.length===3 && !(0 in a) && !('-1' in a);
        a.push=function(v){this.label=v;return 8;};
        ok && a.push(9)===8 && a.label===9 && a.length===3 &&
          Object.keys(a).join(',')==='2,label,push' &&
          global===this && global.answer===42 && strict()===undefined && read()===7 &&
          Object(box)===box && new Object(null)!==new Object(null) &&
          Math.random()>=0 && Math.random()<1;
    )JS");
    boolean("created=3; this.created===3 && created===3 && typeof created==='number' && typeof absent==='undefined'");
    boolean("this.created=4; function strict(){'use strict';created=5;} strict();created===5");
    error("'use strict'; absent=1", "ReferenceError");
    error("function missing(){return absent;} missing();created=1", "ReferenceError");
    boolean("function make(){return ()=>this;} let a=make(),b=make();a()===b() && typeof a()==='object'");
    boolean("[1].map(function(){'use strict';return this;},3)[0]===3");
    error("[1].map(function(){return this;},3)", "capability");
    boolean("let a=[2,3], indices=new Int32Array(1); a[indices[9]]===undefined && a[indices[0]]===2");
    boolean("let a=[2,3], indices=new Int32Array(1); a.undefined=7; a[indices[9]]===7 && a[indices[0]]===2");
    boolean("let a=[2,3]; a[-1]===undefined && a[NaN]===undefined && a[1.5]===undefined && a[4294967295]===undefined");
    boolean("let a=[2,3]; a[-1]=7; a[NaN]=8; a[1.5]=9; a[-1]===7 && a[NaN]===8 && a[1.5]===9");
    boolean("let a=[]; function replace(){a.push=function(){return 9;};return 3;}"
        "a.push(replace())===1 && a[0]===3 && a.push(4)===9 && a.length===1");
    boolean("function f(){let a=[];a.push=function(){return 'x';};let value=a.push(1);"
        "return typeof value==='string' && value==='x';} f()");
    boolean("function f(){let a=[];let alias=Object(a);alias['join']=function(){return 9;};"
        "let value=a.join();return typeof value==='number' && value===9;} f()");
}
TEST_F(JsMvpLmd, CubeConstructionAndCapturedArrays) {
    char* source = read_text_file("test/benchmark/jetstream/3d-cube.js");
    ASSERT_NE(source, nullptr);
    const char trailer[] = "\nrun(); true;";
    size_t length = strlen(source);
    char* program = (char*)mem_alloc(length + sizeof(trailer), MEM_CAT_TEMP);
    ASSERT_NE(program, nullptr);
    memcpy(program, source, length);
    memcpy(program + length, trailer, sizeof(trailer));
    mem_free(source);
    boolean(program);
    mem_free(program);
}
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
TEST_F(JsMvpLmd, NumericRegionLayoutsAndFallback) {
    boolean(R"JS(
        class Point { constructor(x,y) { this.x=x; this.y=y; } }
        class Motion { constructor(first,last) { this.first=first; this.last=last; } }
        function kernel(p,m) {
            if (p.x < 0) return -1;
            const a=m.first; const b=m.last;
            let dx=b.x-a.x; let dy=b.y-a.y;
            let lo=(p.x-a.x)/dx; let hi=(p.y-a.y)/dy;
            if (dx<0) { const t=lo; lo=hi; hi=t; }
            if (dy<0) { const t=lo; lo=hi; hi=t; }
            return lo+hi;
        }
        let p=new Point(2,3);
        let m=new Motion(new Point(0,0),new Point(4,6));
        let ok=kernel(p,m)===1 && kernel(p,m)===1;
        p.x=1.5; ok=ok && kernel(p,m)===0.875;
        p.x='2'; ok=ok && kernel(p,m)===1;
        p.x=2; p.extra=8; ok=ok && kernel(p,m)===1;
        delete p.y; ok=ok && kernel(p,m)!==kernel(p,m);
        p.x=-1; m.first=null; ok && kernel(p,m)===-1;
    )JS");
    boolean(R"JS(
        class Point { constructor(x,y) { this.x=x; this.y=y; } }
        function kernel(a,b) {
            if (a.x<0) return -1;
            const x=a.x+b.x; const y=a.y+b.y;
            let lo=x-y; let hi=y-x;
            if(x<y) { const t=lo; lo=hi; hi=t; }
            if(y<0) { const t=lo; lo=hi; hi=t; }
            return lo+hi+x+y+a.x+b.y;
        }
        function invoke(a,b) { return kernel(a,b); }
        let a=new Point(1,2), b=new Point(3,4); let ok=true;
        for(let i=0;i<5;i++) ok=ok && invoke(a,b)===15;
        a.x='1'; ok=ok && invoke(a,b)==='013614';
        a.x=-1; ok && invoke(a,null)===-1;
    )JS");
}
TEST_F(JsMvpLmd, NumericMethodGuardsAndSpecialNumbers) {
    boolean(R"JS(
        class Compare {
            order(a,b) { if(a===b)return 0; if(a<b)return -1;
                if(a>b)return 1; if(a===a)return 1; return -1; }
            twice(a,b) { const x=this.order(a,b); if(x)return x; return this.order(b,a); }
        }
        let c=new Compare();
        c.twice(1,2)===-1 && c.twice(2,1)===1 && c.twice(-0,0)===0 &&
        c.twice(NaN,NaN)===-1 && c.twice(5e-324,0)===1 &&
        c.twice('10','2')===-1 && c.twice(undefined,1)===1;
    )JS");
    boolean(R"JS(
        class Compare { order(a,b) { if(a===b)return 0; if(a<b)return -1; return 1; } }
        let c=new Compare(); let sum=0;
        for(let i=0;i<4;i++)sum+=c.order(i,2);
        function change() { c.order=function(a,b){return 7;}; return 1; }
        sum===-1 && c.order(change(),2)===-1 && c.order(1,2)===7;
    )JS");
    boolean(R"JS(
        class Compare {
            order(a,b) { if(a===b)return 0; if(a<b)return -1; return 1; }
            choose(a) { const first=this.order(a>0,0); if(first<0)return -1; return this.order(a,2); }
        }
        let c=new Compare(); let ok=true;
        for(let i=0;i<4;i++)ok=ok && c.choose(3)===1 && c.choose(2)===0 && c.choose(1)===-1;
        c.order=function(a,b){return 8;}; ok && c.choose(3)===8;
    )JS");
}
TEST_F(JsMvpLmd, NumericRegionEffectsStayOrdered) {
    error(R"JS(
        class Probe {
            order(a,b) { if(a===b)return 0; if(a<b)return -1; return 1; }
            read(a) { const first=this.order(a,2); return first+this.order.x+this.order.y; }
        }
        let p=new Probe(); p.read(1);
    )JS", "capability");
    numeric(R"JS(
        class Point { constructor(x,y) { this.x=x; this.y=y; } }
        function kernel(p) {
            const a=p.x; p.x=9; const b=p.x;
            let dx=b-a; let dy=p.y-a;
            if(dx<0) { const t=dx; dx=dy; dy=t; }
            if(dy<0) { const t=dx; dx=dy; dy=t; }
            return a+b+dx+dy+p.x+p.y;
        }
        let p=new Point(1,2); kernel(p)+kernel(p);
    )JS", 52);
    boolean(R"JS(
        class Point {
            constructor(x,y) { this.x=x; this.y=y; }
            order(a,b) { if(a===b)return 0; if(a<b)return -1; return 1; }
            compare(other) { const x=this.order(this.x,other.x);
                if(x)return x; return this.order(this.y,other.y); }
        }
        let a=new Point(1,2), b=new Point(1,3); let ok=true;
        for(let i=0;i<4;i++)ok=ok && a.compare(b)===-1;
        a.order=function(x,y) { a.y=7; a.order=function(x,y){return x-y;}; return 0; };
        ok && a.compare(b)===4 && a.y===7;
    )JS");
}
TEST_F(JsMvpLmd, NumericConstructorRegionsKeepIdentityAndLanes) {
    boolean(R"JS(
        class Pair { constructor(x,y){this.x=x;this.y=y}
            shift(n){return new Pair(this.x+n,this.y+n)} }
        class Triple { constructor(x,y,z){this.x=x;this.y=y;this.z=z}
            shift(n){return new Triple(this.x+n,this.y+n,this.z+n)} }
        function step(p,n){return p.shift(n)}
        let a=new Pair(1,2), b=new Triple(3,4,5), ok=true;
        for(let i=0;i<20;i++) {
            let x=step(a,i), y=step(b,i);
            ok=ok && x.x===i+1 && x.y===i+2 && x instanceof Pair && x!==a &&
                y.x===i+3 && y.y===i+4 && y.z===i+5 && y instanceof Triple && y!==b;
        }
        ok;
    )JS");
    boolean(R"JS(
        class Point {
            constructor(x,y) { this.x=x; this.y=y; }
            plus(other) { return new Point(this.x+other.x,this.y+other.y); }
        }
        let p=new Point(1,-0), q=new Point(2,-0);
        let a=p.plus(q), b=p.plus(q);
        let ok=a!==b && a.x===3 && b.x===3 && 1/b.y===-Infinity;
        p.x='x'; let c=p.plus(q); ok=ok && c.x==='x2' && a.x===3;
        p.x=5e-324; q.x=0; let tiny=p.plus(q);
        p.x=2; let later=p.plus(q);
        ok && tiny.x===5e-324 && later.x===2 && tiny instanceof Point;
    )JS");
    char* mir = dump("temp/mvp_native_constructor.mir");
    ASSERT_NE(mir, nullptr);
    char* method = strstr(mir, "mvp_lmd_f2:\tfunc");
    ASSERT_NE(method, nullptr);
    char* allocation = strstr(method, "mvp_lmd_object_new");
    ASSERT_NE(allocation, nullptr);
    char* fallback = strstr(allocation, "mvp_lmd_class_invoke");
    ASSERT_NE(fallback, nullptr); *fallback = 0;
    EXPECT_EQ(strstr(allocation, "mvp_lmd_class_property"), nullptr);
    mem_free(mir);
    boolean(R"JS(
        class Pair { constructor(a,b) { this.y=b; this.x=a; this.ready=true; } }
        class Point {
            constructor(x,y) { this.x=x; this.y=y; }
            plus(other) { return new Pair(this.x+other.x,this.y+other.y); }
        }
        let p=new Point(1,2), q=new Point(3,4), a=p.plus(q), b=p.plus(q);
        let ok=a!==b && a instanceof Pair && Object.keys(a).join(',')==='y,x,ready';
        a.x=9; p.x='1'; let c=p.plus(q);
        ok && b.x===4 && b.y===6 && b.ready===true && c.x==='13' && c.y===6;
    )JS");
    boolean(R"JS(
        class Flag { constructor() { this.value=true; } get() { return this.value; } }
        let f=new Flag(); let ok=true;
        for(let i=0;i<4;i++)ok=ok && f.get()===true;
        f.value=1; ok=ok && f.get()===1 && f.get()!==true;
        f.value=false; ok && f.get()===false;
    )JS");
}
TEST_F(JsMvpLmd, ScalarFactoryReturnsPreserveEffectsAndEscape) {
    boolean("const f=()=>3;const o={f:f};o.f===f && o.f()===3 && o.f===f");
    numeric(R"JS(
        class Pair { constructor(x,y) { this.x=x; this.y=y; } }
        function pair(a,b) { return new Pair(b,a); }
        let effect=0;
        function sum() { const p=pair(++effect,++effect,++effect); p.x+=p.y; return p.x; }
        sum()+effect;
    )JS", 6);
    char* mir = dump("temp/mvp_scalar_factory.mir");
    ASSERT_NE(mir, nullptr);
    char* consumer = strstr(mir, "mvp_lmd_f3:\tfunc");
    ASSERT_NE(consumer, nullptr);
    char* end = strstr(consumer, "\tendfunc");
    ASSERT_NE(end, nullptr); *end = 0;
    EXPECT_EQ(strstr(consumer, "mvp_lmd_class_invoke"), nullptr);
    EXPECT_EQ(strstr(consumer, "mvp_lmd_object_new"), nullptr);
    mem_free(mir);
    boolean(R"JS(
        class Pair { constructor(x,y) { this.x=x; this.y=y; } }
        function pair(x) { return new Pair(x,5e-324); }
        function local() { const p=pair(1e-323); let old=p.y; p.y=1; return old===5e-324 && p.x===1e-323; }
        function escape() { const p=pair(2); return p; }
        let a=escape(), b=escape();
        local() && a!==b && a instanceof Pair && a.x===2 && a.y===5e-324;
    )JS");
    error(R"JS(
        function pair(x) { return new Pair(x); }
        function use() { const p=pair(1); return p.x; }
        use(); class Pair { constructor(x) { this.x=x; } }
    )JS", "ReferenceError");
}
TEST_F(JsMvpLmd, PlainMutationsUseLambdaShapes) {
    numeric(R"JS(
        function work() {
            let o={x:5e-324,y:2}; let saved=o.x;
            delete o.y; o.z=3; o.x=1;
            return saved;
        }
        work();
    )JS", 5e-324);
    char* mir = dump("temp/mvp_plain_shape_mutation.mir");
    ASSERT_NE(mir, nullptr);
    EXPECT_NE(strstr(mir, "call\tmap_shape_set"), nullptr);
    EXPECT_NE(strstr(mir, "call\tmap_shape_delete"), nullptr);
    mem_free(mir);
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
TEST_F(JsMvpLmd, OptionalIntegerElementsPreserveUndefined) {
    boolean("function f(i){const a=[-2,0,7];return a[i]} var g=f;"
        "g(0)===-2 && g(1)===0 && g(3)===undefined && g(3)!==0 && g(3)==null &&"
        "typeof g(3)==='undefined' && (g(3)+1)!==(g(3)+1)");
    boolean("function f(){const a=[-2,0,7];let i=Math.sqrt(16);"
        "return a[i]===undefined && a[i]!==0 && !a[i] && (a[i]|0)===0 &&"
        "(a[i]+1)!==(a[i]+1) && -a[i]!==-a[i] && (''+a[i])==='undefined'} f()");
    boolean("function f(){const a=new Int32Array(2);a[0]=-7;let i=Math.sqrt(9);"
        "const old=a[i];a[0]=11;return old===undefined && a[i]!==0 && a[i]===a[i] &&"
        "!(a[i]<1) && !(a[i]>=0) && (a[i]|0)===0 && a[0]===11} f()");
    boolean("function f(){const a=new Uint8Array(1);a[0]=255;let i=Math.sqrt(4);"
        "const missing=a[i];a.fill(missing);return a[0]===0 && missing===undefined} f()");
    boolean("function f(i){const a=[-0,1.5,NaN];return a[i]} var g=f;"
        "1/g(0)===-Infinity && g(1)===1.5 && g(2)!==g(2) && g(3)===undefined");
    error("function f(){const keys=[0,1];const a=new Uint8Array(2);let i=Math.sqrt(9);"
        "return a[keys[i]]} f()", "capability");
}
TEST_F(JsMvpLmd, ConditionShortCircuitOrder) {
    numeric("var n=0;function hit(x){n=n*10+x;return x}"
        "if(hit(0)&&hit(1))n=9;if(hit(2)||hit(3))n=n*10+4;"
        "if(!(hit(0)||hit(5)&&hit(0)))n=n*10+6;n", 240506);
    boolean("function f(){const from=[0,1];const board=new Uint8Array(2).fill(1);"
        "let i=Math.sqrt(1);return board[from[i]] && !board[2]} f()");
}
TEST_F(JsMvpLmd, ImmutableContainerFactsAcrossCalls) {
    numeric("function sum(a){let s=0;for(let i=0;i<3;i++)s+=a[i];return s}"
        "function f(){const a=[1.5,-2,4];return sum(a)} f()", 3.5);
    numeric("function change(a){a[1]='3'} function sum(a){return a[0]+a[1]}"
        "function f(){const a=[1,2];change(a);return sum(a)==='13'?1:0} f()", 1);
    boolean("function make(n){if(n===0)return {left:null,right:null};"
        "return {left:make(n-1),right:make(n-1)}}"
        "function check(n){if(n.left===null)return 1;return 1+check(n.left)+check(n.right)}"
        "check(make(5))===63");
    boolean("function get(o){return o.child} function wrap(o){return {child:get(o)}}"
        "var a={child:{value:7}};var b={other:1};"
        "wrap(a).child.value===7 && wrap(b).child===undefined");
    boolean("function get(o){return o.x} var a={x:1};a.x='new';get(a)==='new'");
    boolean("function get(o){return o.x} var a={x:1};delete a.x;get(a)===undefined");
    boolean("function get(o){return o.x} var key='x';var a={[key]:3};get(a)===3");
    boolean("function f(){const make=()=>({t:1});return make()!==make() && make().t===1} f()");
    numeric("function f(){let g=()=>1;g=()=>2;return g()} f()", 2);
    boolean("function f(){const g=()=>1;const alias=g;return alias===g && alias()===1} f()");
    error("function f(){return g();const g=()=>1} f()", "ReferenceError");
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
TEST_F(JsMvpLmd, PowerOfTwoZeroTestsAndScaling) {
    boolean("let d=2;function fast(x){return x%2===0}function slow(x){return x%d===0}"
        "let values=[0,-0,1,-1,2,-2,1.5,-1.5,5e-324,-5e-324,9007199254740991,"
        "9007199254740992,9007199254740994,1e100,1.7976931348623157e308,Infinity,-Infinity,NaN,'4',null,undefined];"
        "let ok=true;for(let x of values){if(fast(x)!==slow(x))ok=false}ok");
    boolean("let d=9007199254740992;function fast(x){return x%9007199254740992!==0}"
        "function slow(x){return x%d!==0}let values=[0,-0,1,-1,9007199254740991,9007199254740992,"
        "9007199254740994,-9007199254740992,1e100,Infinity,NaN];"
        "let ok=true;for(let x of values){if(fast(x)!==slow(x))ok=false}ok");
    boolean("function integer(x){return x%1==0}integer(-0)&&integer(-3)&&!integer(0.5)&&"
        "!integer(5e-324)&&!integer(Infinity)&&!integer(NaN)");
    boolean("let d=4;function fast(x){return x%4===0}function slow(x){return x%d===0}"
        "let values=[5e-324,-5e-324,1e-323,0.9999999999999999,3.9999999999999996,"
        "4.000000000000001,18014398509481982,18014398509481984,18014398509481988,"
        "-18014398509481982,1.7976931348623157e308];"
        "let ok=true;for(let x of values){if(fast(x)!==slow(x))ok=false}ok");
    boolean("let n=0;function next(){n++;return 4}next()%2===0&&n===1");
    boolean("function half(x){return x/2}half(7)===3.5&&1/half(-0)===-Infinity&&"
        "1/half(-5e-324)===-Infinity&&half(1e-323)===5e-324&&half(Infinity)===Infinity&&half(NaN)!==half(NaN)");
    error("function f(x){return x%2===0}f({})", "capability");
}
TEST_F(JsMvpLmd, SquareBoundsAndResetCursors) {
    numeric("function f(){let a=[2,3];a[-1]=9;let s=0;for(let i=0;i<4;i++){if(i<2){s+=a[i]}else{s+=a[i-2]}}return s}f()", 10);
    char* conditional = dump("temp/mvp_lmd_conditional_bounds.mir");
    ASSERT_NE(conditional, nullptr);
    EXPECT_EQ(strstr(conditional, "mvp_lmd_property_get"), nullptr);
    mem_free(conditional);
    boolean("function f(){let a=[7];let i=0;while(i<1){if(i<1){i++;return a[i]}}}f()===undefined");
    boolean("function f(){let a=[7];let i=1;while(i>0){if(i>=1){i--;return a[i-1]}}}f()===undefined");
    numeric("function f(n){let i=0,sum=0;while(i+2<n){sum+=i;i+=3}return sum+i}f(10)", 18);
    numeric("function f(){let i=10,sum=0;while(i-2>0){sum+=i;i-=3}return sum+i}f()", 22);
    numeric("function f(){let i=0;while(i+0.5<3){i++}return i}f()", 3);
    numeric("function f(){let i=9007199254740989;while(i+2<9007199254740991){i++}return i}f()", 9007199254740989.0);
    boolean("function f(){const a=[7];let i=3;do{const x=a[i];i=0;return x}while(i<1)}f()===undefined");
    boolean("function f(){const a=[7];let i=0;while(i<1){i++;return a[i]}}f()===undefined");
    boolean("function f(){const a=[7];let i=0,x=0;while(i<1){let j=0;"
        "while(j<2){x=a[i];i=1;j++}}return x}f()===undefined");
    boolean("function f(){const a=new Uint8Array(1);let i=0,x=0;while(i<1){"
        "for(let j of [0,1]){x=a[i];i=1}}return x}f()===undefined");
    numeric("function f(n){let count=0;for(let i=2;i*i<=n;i++){count++}return count}f(1000)", 30);
    boolean("function f(n){let a=new Uint8Array(n+1);for(let i=2;i*i<=n;i++){"
        "for(let j=i*i;j<=n;j+=i){a[j]=1}}return a[100]}f(100)===1");
    numeric("function f(){let count=0;for(let repeat=0;repeat<3;repeat++){"
        "let lo=0,hi=7;while(lo<hi){count++;lo++;hi--}}return count}f()", 12);
    numeric("function f(){let count=0;for(let repeat=0;repeat<3;repeat++){"
        "let hi=8,lo=0;while(hi>lo){count++;hi-=2;lo++}}return count}f()", 9);
    numeric("function f(){let count=0;for(let i=-2;i*i<=9;i--){count++}return count}f()", 2);
    numeric("function f(){let count=0;for(let i=0.5;i*i<=4;i++){count++}return count}f()", 2);
    numeric("function f(){let count=0;let lo=0;for(let repeat=0;repeat<3;repeat++){"
        "let hi=7;while(lo<hi){count++;lo++;hi--}}return count}f()", 7);
    numeric("function f(){let count=0;for(let repeat=0;repeat<3;repeat++){"
        "let lo=0,hi=3;while(lo<hi){count++;hi++;lo+=2}}return count}f()", 9);
}
TEST_F(JsMvpLmd, GuardedIntegerLoopReplay) {
    numeric("function f(n){let x=n,steps=1;while(x!==1){if(x%2===0){x=x/2}else{x=3*x+1}steps++}return steps}f(27)", 112);
    numeric("function f(n){let x=n,s=1;while(x>1){s++;x=x/2}return s+x}f(5)", 4.625);
    numeric("function f(n){let x=n,s=1;while(x<9007199254740994){s++;x=3*x+1}return s}f(4503599627370495)", 2);
    numeric("function f(n){let x=n,s=1;while(x<9007199254740992){s++;x=x+1}return s}f(9007199254740991)", 2);
    numeric("function f(n){let x=n,s=1;while(x>0){s++;x=x-1}return s}f(3.5)", 5);
    numeric("function f(n){let x=n,s=1;while(x>0){s++;x=x-1}return s}f(3)", 4);
    boolean("function f(n){let x=n;while(x>1){x=x/2}return x}1/f(-0)===-Infinity&&f(-2)===-2&&f(0)===0");
    boolean("function f(n){let x=n;while(x>1){x=x/2}return x}let x=f(NaN);x!==x");
    numeric("function f(n){let x=n,s=1;while(x>1){s++;x=3*0}return s+x}f(5)", 2);
    numeric("function f(n){let x=n,s=1;while(x>1){s++;x=x/2;s++}return s+x}f(5)", 7.625);
    numeric("function f(n){let x=n;while(x++<3){x=x/2}return x}f(3)", 4);
    numeric("let seen=0;function hit(){seen++}function f(n){let x=n;while(x>1){hit();x=x/2}return x}f(5)+seen", 3.625);
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
TEST_F(JsMvpLmd, ArrayStringBuiltinInferenceAndRowSwaps) {
    boolean("function repeat(s,n){return s.repeat(n)} function code(s,i){return s.charCodeAt(i)} "
        "repeat('ab',3)==='ababab' && code(repeat('x',2),1)===120 && code('x',2)!==code('x',2)");
    boolean("function rows(){let a=new Int32Array(3),b=new Int32Array(3);a[0]=7;b[0]=9;"
        "for(let i=0;i<5;i++){[a,b]=[b,a]}return a[0]===9&&b[0]===7} rows()");
    char* mir = dump("temp/mvp_lmd_row_swap.mir");
    ASSERT_NE(mir, nullptr);
    EXPECT_EQ(strstr(mir, "\timport\tmvp_lmd_property_get"), nullptr);
    EXPECT_EQ(strstr(mir, "\timport\tarray\n"), nullptr);
    mem_free(mir);
    boolean("let a=new Int32Array(1),b=new Uint8Array(1);a[0]=-7;b[0]=255;"
        "[a,b]=[b,a];a[0]=257;b[0]=-9;a[0]===1&&b[0]===-9");
    boolean("let a=1,b=2;[a,b]=[b,a];a===2&&b===1");
    boolean("let a=1,b=2;let rhs=([a,b]=[b,a]);rhs[0]===2&&rhs[1]===1&&a===2&&b===1");
    boolean("let a=1,b=2;[a,b]=[3];a===3&&b===undefined");
    boolean("let a=1,b=2;[a,b]=[a=5,a+1];a===5&&b===6");
    boolean("let a=1,b=2;[a,b]=[5e-324,-0];a===5e-324&&1/b===-Infinity");
    boolean("function f(){let a=1,b=2;[a,b]=[a=5,a+1];"
        "if(a!==5||b!==6)return false;[a,b]=[5e-324,-0];"
        "if(a!==5e-324||1/b!==-Infinity)return false;[a,b]=[3];return a===3&&b===undefined}f()");
    Item completion = run("let a=0,b=0;[a,b]=[4,5]");
    ASSERT_EQ(get_type_id(completion), LMD_TYPE_ARRAY);
    ASSERT_EQ(completion.array->length, 2);
    EXPECT_EQ(it2d(completion.array->items[0]), 4);
    EXPECT_EQ(it2d(completion.array->items[1]), 5);
    error("const a=1;let b=2;[b,a]=[4,5];b", "TypeError");
    error("[a]=[1];let a", "ReferenceError");
    error("function f(){const a=1;let b=2;[b,a]=[4,5];return b}f()", "TypeError");
    error("function f(){[a]=[1];let a}f()", "ReferenceError");
}
TEST_F(JsMvpLmd, OrdinaryArrayHolesFillAndJoin) {
    boolean("let a=new Array(3);a[1]=undefined;!(0 in a)&&(1 in a)&&a[0]===undefined&&"
        "a.join('|')==='||'&&Object.keys(a).join(',')==='1'");
    boolean("let a=Array(3).fill(false);a[1]=true;delete a[0];"
        "a.join(',')===',true,false'&&!(0 in a)&&a.pop()===false&&a.length===2");
    boolean("let a=['a','b'];a.push('c','d');a.join('')==='abcd'&&a.join() === 'a,b,c,d'");
    boolean("let a=['a',null,undefined,2,false];a.join('|')==='a|||2|false'");
    boolean("let a=Array(2);a.fill('x',-1);a.join('-')==='-x'&&!(0 in a)&&(1 in a)");
    boolean("let a=[];a.push(5e-324);let n=a.pop();"
        "for(let i=0;i<100;i++){a.push(''+i)}n===5e-324&&a.length===100");
    boolean("let a=[1,2];let alias=a;delete alias[0];a[0]===undefined&&a[1]===2");
    boolean("let a=[1,2];a.length=0;a.push('x');a.join('')==='x'");
    boolean("let a=[];a.push()===0&&a.pop()===undefined&&a.join('|')===''&&Array('3')[0]==='3'");
    boolean("let a=[1,2];a.join((a.push(3),'|'))==='1|2|3'");
    boolean("let a=Array(2);let n=0;for(let x of a){if(x===undefined)n++}n===2");
    boolean("function Array(n){return [n]}Array(3)[0]===3&&Array(3).length===1");
    error("new Array(-1)", "RangeError");
    error("new Array(1.5)", "RangeError");
    error("new Array(4294967296)", "RangeError");
    error("[{}].join('')", "capability");
}
TEST_F(JsMvpLmd, CharacterCodesAndAsciiReuse) {
    boolean("function code(s,i){return s.charCodeAt(i)}"
        "code('abc',1.9)===98&&code('abc',-0.5)===97&&code('abc',NaN)===97&&"
        "code('abc',undefined)===97&&code('abc','2')===99&&code('abc',-1)!==code('abc',-1)");
    boolean("let s='a\\u{1f600}\\ud800';s.length===4&&s.charCodeAt(1)===55357&&"
        "s.charCodeAt(2)===56832&&s.charCodeAt(3)===55296&&s.charAt(4)===''&&s[4]===undefined");
    boolean("String.fromCharCode(65,0,65536,0xd800)==='A\\0\\0\\ud800'&&"
        "'abc'.charAt(1)==='b'&&'abc'[1]==='b'&&'x'.repeat(0)===''");
    boolean("function read(s,i){return s.charCodeAt(i)}"
        "read('x',Infinity)!==read('x',Infinity)&&read('x',-Infinity)!==read('x',-Infinity)");
    boolean("const s='abc';function f(){return s[1]+s.length}f()==='b3'");
    boolean("const s='abc';function f(i){return s[i]}"
        "f(3)===undefined&&f('length')===3&&f(1)==='b'");
    boolean("const s='abc';let n=0,a=[];for(let i=0;i<4;i++){a.push(s[(n++,i)])}"
        "n===4&&a.join('')==='abc'&&a[3]===undefined");
    boolean("function f(s){let a=[];for(let i=0;i<s.length;i++){a.push(s[i])}return a.join('')}"
        "f('a\\0b\\u007f')==='a\\0b\\u007f'&&f('abc')==='abc'");
    boolean("let s='abc';function f(i){return s[i]}s='\\u{1f600}';"
        "f(0)==='\\ud83d'&&f(1)==='\\ude00'&&f(2)===undefined");
    boolean("const s='\\u{1f600}';function f(i){return s[i]}"
        "s.length===2&&f(1)==='\\ude00'");
    boolean("let s='ab',n=0;let c=s.charAt((s='xy',n++),(n++));c==='a'&&s==='xy'&&n===2");
    boolean("'ab'.repeat(2.9)==='abab'&&'x'.repeat(NaN)===''&&String.fromCharCode()===''");
    boolean("function f(){let String={fromCharCode:(x)=>x+1};return String.fromCharCode(4)===5}f()");
    error("function f(){return s.length} f();const s='abc'", "ReferenceError");
    error("function f(i){return s[i]} f(0);const s='abc'", "ReferenceError");
    error("'x'.repeat(-1)", "RangeError");
    error("''.repeat(Infinity)", "RangeError");
}
TEST_F(JsMvpLmd, ConcatenationOrderAndAliases) {
    boolean("let n=1;let s='x'+n+true+null+undefined+'z';s==='x1truenullundefinedz'");
    boolean("let n=1;let s=n+2+'x'+3+4;s==='3x34'");
    boolean("let calls=0;function f(){calls++;return calls}"
        "let s='x'+f()+f()+f()+f()+f()+f()+f();s==='x1234567'&&calls===7");
    boolean("let a='a'+'b'+'c',alias=a;let b=a+'d'+'e';"
        "let pieces=[a,b];alias==='abc'&&pieces.join('|')==='abc|abcde'");
    boolean("let a='x',b='y';let s=''+a+(a='z')+a+b;s==='xzzy'&&a==='z'");
    boolean("let s='a'+'\\ud800'+'\\udc00'+'b';s.length===4&&s.charCodeAt(1)===55296&&s.charCodeAt(2)===56320");
    boolean("let a=[];for(let i=0;i<200;i++){a.push('x'+i+':'+(i+1)+'!')}"
        "a[0]==='x0:1!'&&a[199]==='x199:200!'&&a.join('|').length>200");
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
    boolean("var a=[]; a[1]=2; a.length===2 && !(0 in a) && a[0]===undefined");
    boolean("var a=[]; a.length=1; a.length===1 && !(0 in a)");
    boolean("var a=[]; a['01']=1; a['01']===1 && a.length===0");
    boolean("var a=[]; a['-0']=1; a['-0']===1 && a.length===0");
    boolean("var a=[]; a[4294967295]=1; a[4294967295]===1 && a.length===0");
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
    boolean("function at(a,i){return a[i]} at([2],'-0')===undefined");
    boolean("function at(a,i){return a[i]} at([2],1.5)===undefined");
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
    boolean("function grow(){let a=[];a[1]=2;return a.length===2 && a[0]===undefined} grow()");
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
    boolean("function f(){return ()=>x; var x=1} f()()===undefined");
    numeric("{let x=1; function f(){return x}} f()", 1);
    error("function f(a=1){return a} 1", "scope");
    boolean("function f(){return this} f()===f()");
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
TEST_F(JsMvpLmd, PredictedDeletionShapesKeepOwnership) {
    Item shapes = run("let a={x:1,y:2};let b={y:2};delete a.x;[a,b]");
    ASSERT_EQ(get_type_id(shapes), LMD_TYPE_ARRAY);
    // deletion replay must share the same nominal transition graph as literal planning.
    EXPECT_EQ(shapes.array->items[0].map->type, shapes.array->items[1].map->type);
    boolean(R"JS(
        function check() {
            let object={left:5e-324,right:2}; let alias=object; let ok=true;
            const values=[3,5e-324,-0,'text',true,null,undefined,{value:9},[7],()=>8];
            for(let i=0;i<30;i++) {
                delete object.left;
                ok=ok && object.left===undefined && !Object.hasOwn(object,'left');
                const value=values[i%values.length]; object.left=value;
                const first=object.left; const held=object.left;
                object.left='replacement';
                ok=ok && first===value && held===value && object.left==='replacement';
                if(i%values.length===2) ok=ok && 1/held===-Infinity;
                object.left=value;
                ok=ok && object.right===2 && Object.keys(object).join(',')==='right,left';
            }
            return ok && object===alias;
        }
        check();
    )JS");
    char* mir = dump("temp/js_mvp_deleted_object_cache.mir");
    ASSERT_NE(mir, nullptr);
    EXPECT_EQ(strstr(mir, "mvp_lmd_class_property"), nullptr);
    EXPECT_NE(strstr(mir, "mvp_lmd_property_get"), nullptr);
    mem_free(mir);
    boolean(R"JS(
        function read(o) {
            let ok=true;
            for(let i=0;i<12;i++) {
                delete o.a; delete o.b;
                ok=ok && o.c===3 && o.a===undefined && o.b===undefined;
                o.b=i; o.a='value';
                ok=ok && o.a==='value' && o.b===i && o.c===3;
            }
            return ok && Object.keys(o).join(',')==='c,b,a';
        }
        const first={a:1,b:2,c:3}, second={a:4,b:5,c:3};
        read(first) && read(second) && first!==second;
    )JS");
}
TEST_F(JsMvpLmd, ObjectEvaluationOrderAndImmutableShapes) {
    boolean("function f(x){return {a:1,b:x,c:3}}let a=f(2),b=f(-0),c=f(5e-324);"
        "a.a===1 && a.b===2 && a.c===3 && b.a===1 && 1/b.b===-Infinity && b.c===3 &&"
        "c.a===1 && c.b===5e-324 && c.c===3");
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
    numeric("let m=new Map();m['set'](1,2);m.get(1)", 2);
    char* mir = dump("temp/js_mvp_map_builtin_calls.mir");
    ASSERT_NE(mir, nullptr);
    EXPECT_EQ(strstr(mir, "mvp_lmd_property_get"), nullptr);
    EXPECT_NE(strstr(mir, "mvp_lmd_map_call"), nullptr);
    mem_free(mir);
    boolean("let m=new Map();m.set('a',1).set('b',undefined);m.size===2 && m.get('a')===1 && m.has('b') && m.get('b')===undefined && !m.has('c') && m.get('c')===undefined");
    boolean("let m=new Map();m.set(1,'int');m.set(1/1,'float');m.set(-0,'zero');m.set(0,'same');m.set(NaN,3);m.set(0/0,4);m.size===3 && m.get(1)==='float' && m.get(0)==='same' && m.get(NaN)===4");
    boolean("let m=new Map();let a={},b={},c=[],f=()=>1;m.set(a,1).set(b,2).set(c,3).set(f,4).set(null,5).set(undefined,6).set(true,7);m.size===7 && m.get(a)===1 && m.get(b)===2 && m.get(c)===3 && m.get(f)===4 && m.get(null)===5 && m.get(undefined)===6 && m.get(true)===7");
    boolean("let m=new Map();m.set('\\uD83D'+'\\uDE00',3);m.set('😀',4);m.size===1 && m.get('😀')===4");
    boolean("let m=new Map();m.x=9;m.set('x',1);Object.keys(m)[0]==='x' && m.x===9 && m.get('x')===1 && m.delete('x') && !m.delete('x') && m.x===9 && m.size===0");
    boolean("let m=new Map();m.set('x',5e-324);let old=m.get('x');m.set('x',2);old===5e-324 && m.get('x')===2 && m.clear()===undefined && m.size===0");
    boolean("let m=new Map();m.set(1,5e-324);m.get(1);let old=m.get(1);m.set(1,1e-323);"
        "let ok=old===5e-324 && m.get(1)===1e-323 && m.has(1);m.delete(1);"
        "ok=ok && !m.has(1) && m.get(1)===undefined;m.set(1,3);m.get(1);m.clear();"
        "ok && m.get(1)===undefined && !m.has(1)");
    boolean("let m=new Map();m.set(undefined,2);m.get();let ok=m.get()===2 && m.has();"
        "m.set(-0,3);m.get(0);ok=ok && m.get(-0)===3;m.set(NaN,4);m.get(NaN);"
        "ok && m.get(0/0)===4 && m.has(NaN)");
    boolean("let m=new Map();m.set(1,2);m.get(1);function clear(){m.clear();return 1}"
        "m.get(clear())===undefined");
}
TEST_F(JsMvpLmd, LiveMapIteration) {
    numeric("function sum(values){let n=0;for(let [k,v] of values)n+=k+v;return n}let m=new Map();m.set(3,4);sum([[1,2]])+sum(m)", 10);
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
    error("let Map=()=>7;new Map()", "TypeError");
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
    numeric("let a=[1,2];let s=0;for(let x of a){s+=x;a=new Map()}s", 3);
    numeric("let m=new Map();m.set(1,2).set(3,4);let s=0;for(let [k,v] of m){s+=v;m=[]}s", 6);
    Item result = run("let m=new Map();m.set(1,1);function f(){for(let pair of m){return pair}}f();for(let k of m.keys()){break}for(let i=0;i<300;i++){m.set(i,i);m.delete(i)}m.set('live',5e-324);m");
    ASSERT_EQ(get_type_id(result), LMD_TYPE_MAP);
    OrderedMap* map = (OrderedMap*)result.map;
    EXPECT_EQ(map->cursors, 0);
    EXPECT_LT(map->entries->length, 140);
    boolean("let m=new Map();m.set(1,1);outer:for(let a of m){for(let b of m){break outer}}m.clear();m.set(2,2);let s=0;for(let [k,v] of m){s+=k+v}s===4");
    numeric("let m=new Map();m.set(1,1).set(2,2);let s=0;for(let [k,v] of m){m.delete(k);m.set(k+2,v+2);s+=v;if(k===4)break}s", 10);
}

TEST_F(JsMvpLmd, SharedEscapingClosures) {
    numeric("function f(){let x=1;return ()=>++x} let g=f();g()+g()", 5);
    numeric("function f(){let x=0;return [()=>++x,()=>x]}const a=f();a[0]()+a[1]()", 2);
    numeric("function f(x){return ()=>()=>++x}const g=f(3)();g()+g()", 9);
    numeric("function f(){let x=0;function g(n){if(n){x++;return g(n-1)}return x}return g}f()(4)", 4);
    numeric("function f(){let x=0;return ()=>++x}let a=f();let b=f();a()+a()+b()", 4);
    boolean("function f(){let x=1;return [()=>x,()=>{x='s'}]}let a=f();a[1]();a[0]()==='s'");
    numeric("function f(){const x={n:0};return ()=>++x.n}let g=f();g()+g()", 3);
    numeric("function f(x){const g=()=>x;x=7;return g}f(2)()", 7);
    numeric("function f(){let x=1;const g=()=>++x;g();return x}f()", 2);
    numeric("function f(){let x=1;{let x=7;return ()=>x}}f()()", 7);
    numeric("function f(){return g;function g(){return ++x}var x}let g=f();g()", NAN);
    numeric("function f(){let x=2;return function self(n){return n?x+self(n-1):x}}f()(3)", 8);
    boolean("function f(){let x=5e-324;return [()=>x,v=>{x=v}]}let a=f();let old=a[0]();"
        "a[1](1e-323);for(let i=0;i<100;i++)['x'+i];old===5e-324 && a[0]()===1e-323");
}
TEST_F(JsMvpLmd, ClosureInitializationAndConst) {
    error("function f(){let g=()=>x;g();let x=1}f()", "ReferenceError");
    error("function f(){let g=()=>{x=2};g();let x=1}f()", "ReferenceError");
    error("function f(){const x=1;return ()=>{x=2}}f()()", "TypeError");
    boolean("function f(){return ()=>x;var x=1}f()()===undefined");
    numeric("function f(){let g=()=>x;let x=3;return g}f()()", 3);
    numeric("function f(){let x=1;function g(){return x}x=3;return g}f()()", 3);
}
TEST_F(JsMvpLmd, ClosureIterationBindings) {
    numeric("let a=[];for(let i=0;i<3;i++)a.push(()=>i);a[0]()+10*a[1]()+100*a[2]()", 210);
    numeric("let a=[];for(let i=0;i<4;i++){if(i===1)continue;a.push(()=>i)}"
        "a[0]()+10*a[1]()+100*a[2]()", 320);
    numeric("let a=[];for(const i of [1,2,3])a.push(()=>i);a[0]()+10*a[1]()+100*a[2]()", 321);
    numeric("let a=[];let m=new Map();m.set(1,2);m.set(3,4);"
        "for(const [k,v] of m)a.push(()=>k+v);a[0]()+a[1]()", 10);
    numeric("let a=[];for(var i=0;i<3;i++)a.push(()=>i);a[0]()+a[1]()+a[2]()", 9);
    numeric("let a=[];for(let i=0;i<3;i++){let j=i*2;a.push(()=>j)}a[0]()+a[1]()+a[2]()", 6);
}
TEST_F(JsMvpLmd, ClassesAndLexicalReceivers) {
    error("class A{f(){return 1}}class B extends A{f(){return ()=>super.f()}}new B().f()()", "scope");
    boolean("class A{constructor(x){this.x=x}f(){return this.x}static g(){return 4}}"
        "class B extends A{f(){return super.f()+1}}let b=new B(2);"
        "b.f()===3 && B.g()===4 && b instanceof B && b instanceof A");
    boolean("class A{f(){return ()=>this}}let a=new A();a.f()()===a");
    numeric("class A{constructor(x){this.x=x}f(){return ()=>()=>++this.x}}let a=new A(1);"
        "let g=a.f()();g()+g()", 5);
    boolean("class A{}class B extends A{constructor(){let f=()=>this;super();this.f=f}}"
        "let b=new B();b.f()===b");
    boolean("class A{constructor(){this.f=()=>new.target}}class B extends A{}"
        "new B().f()===B && new A().f()===A");
    error("class A{}class B extends A{constructor(){let f=()=>this;f();super()}}new B()", "ReferenceError");
    error("class A{}A()", "TypeError");
    error("class A{}new A().missing()", "TypeError");
}
TEST_F(JsMvpLmd, SparseArrayGrowthAndSlice) {
    numeric("function f(){let a=[];for(let i=0;i<20;i++)a[i]=i;let s=0;"
        "for(let i=0;i<a.length;i++)s+=a[i];return s}f()", 190);
    boolean("let a=[];for(let i=0;i<5;i+=2)a[i]=i;a[1]===undefined && !(1 in a)");
    boolean("let a=[];for(let i=0;i<5;i++){if(i===1)continue;a[i]=i}a[1]===undefined && !(1 in a)");
    boolean("function f(){let a=[];let alias=a;function reset(i){alias.length=0;return i}"
        "for(let i=0;i<5;i++)a[i]=reset(i);return a[1]===undefined && !(1 in a)}f()");
    boolean("let a=[1,2,3];a.length=1;a.length=4;"
        "a[0]===1 && a[1]===undefined && !(1 in a) && !(3 in a) && Object.keys(a).length===1");
    boolean("let a=[];a[3]=undefined;a[5]=5;let b=a.slice(1,6);"
        "b.length===5 && !(0 in b) && (2 in b) && b[2]===undefined && b[4]===5");
    boolean("let a=[1,2,3,4];a.slice(-3,-1).join(',')==='2,3' && a.slice(3,1).length===0 &&"
        "a.slice(NaN,undefined).length===4 && a.slice(-Infinity,Infinity).length===4");
    boolean("let nested={x:1};let a=[5e-324,nested];let b=a.slice();a[0]=1e-323;"
        "b[1].x=2;b[0]===5e-324 && a[0]===1e-323 && a[1].x===2");
}
TEST_F(JsMvpLmd, ArrayForEachMutationAndArguments) {
    numeric("let sum=0;[1,2,3].forEach(x=>{sum+=x});sum", 6);
    numeric("function f(){let sum=0;[1,2,3].forEach(x=>{sum+=x});return sum}f()", 6);
    boolean("let a=[];a[1]=undefined;a[3]=4;let seen='';let result=a.forEach((v,i,owner)=>{"
        "seen+=i+':'+v+';';if(owner!==a)seen='bad'});result===undefined && seen==='1:undefined;3:4;'");
    numeric("let a=[1,2,3];let sum=0;a.forEach((v,i)=>{sum+=v;if(i===0){a[1]=8;a.push(9);delete a[2]}});sum", 9);
    numeric("let a=[1,2,3];let sum=0;a.forEach((v,i)=>{sum+=v;if(i===0)a.length=0});sum", 1);
    numeric("class A{f(x){this.sum+=x}}let a=new A();a.sum=0;[1,2,3].forEach(a.f,a);a.sum", 6);
    error("[].forEach(1)", "TypeError");
    error("[1].forEach(()=>{const x=1;x=2})", "TypeError");
}
TEST_F(JsMvpLmd, ClassAncestryAndReflection) {
    boolean("class A{f(){return this.x}}class B extends A{}let b=new B();b.x=2;"
        "'f' in b && !Object.hasOwn(b,'f') && Object.keys(b).join(',')==='x' &&"
        "Object.keys(A).length===0 && Object.keys(A.prototype).length===0 && b.f()===2");
    boolean("class A{}class B extends A{}let b=new B();b.x=1;b.x='s';delete b.x;b.y=5e-324;"
        "b instanceof A && b instanceof B && b.y===5e-324 && !Object.hasOwn(b,'x')");
    numeric("class A{f(){return this.x}}class B extends A{f(){return super.f()+1}}"
        "let b=new B();b.x=4;let f=b.f;b.f=()=>9;b.f()+[1].length", 10);
    boolean("class A{static f(){return ()=>this}}class B extends A{}B.f()()===B");
    boolean("class A{constructor(){return 3}}new A() instanceof A");
    boolean("class A{constructor(){return {x:3}}}class B extends A{}let b=new B();"
        "b.x===3 && !(b instanceof A) && !(b instanceof B)");
    error("class A{}class B extends A{constructor(){return 3}}new B()", "TypeError");
    error("class A{}class B extends A{constructor(){}}new B()", "ReferenceError");
}
TEST_F(JsMvpLmd, ClassCachedScalarLanesAndRetyping) {
    boolean("class A{constructor(x){this.x=x}get(){return this.x}set(x){this.x=x;return this.x}}"
        "class B extends A{}let a=new B(1);let ok=true;"
        "for(let v of [1,2,1.5,2.5,true,false,'s',null,undefined,5e-324,1e-323,-0,Infinity,-Infinity]){"
        "a.set(v);a.set(v);ok=ok && a.get()===v && a.get()===v}"
        "a.set(NaN);ok=ok && a.get()!==a.get();delete a.x;ok=ok && a.get()===undefined;"
        "a.set(4);ok && a.get()===4 && a instanceof A");
    numeric("class A{constructor(){this.x=5e-324}get(){return this.x}set(x){this.x=x}}"
        "let a=new A();a.get();let x=a.get();a.set(1e-323);a.set(5e-324);a.set(1e-323);x", 5e-324);
    boolean("class A{constructor(){this.x=0}get(){return this.x}set(x){this.x=x}}"
        "let a=new A();a.set(-0);a.set(-0);a.get();1/a.get()===-Infinity");
    numeric("class A{f(){return this.x}}let a=new A();a.x=1;let s=0;"
        "for(let i=0;i<4;i++){s+=a.f();if(i===1)a.f=()=>10}delete a.f;s+a.f()", 23);
    numeric("class A{constructor(){this.x=5e-324}get(){return this.x}}"
        "let a=new A();function mutate(){a.x=1e-323;return 0}a.x-mutate()-a.x", -5e-324);
    boolean("class A{constructor(){this.x=1}f(){return this.x}}class B extends A{f(){return 10}}"
        "function call(a){return a.f()}let a=new A();let b=new B();"
        "call(a)===1 && call(b)===10 && call(a)===1 && a.f(a.f=()=>7)===1 && call(a)===7");
    boolean("class A{f(){return this.x}target(){return new.target}}let a=new A();a.x=3;"
        "let b={x:8,f:a.f};b.f()===8 && a.target()===undefined");
    numeric("class A{constructor(x){this.x=x}}let a=new A('2');let s=0;"
        "for(let v of ['2',3,1.5,true,null]){a.x=v;s+=a.x*2} s", 15);
    boolean("class A{constructor(x){this.x=x}get(){return this.x}set(x){this.x=x}}"
        "let a=new A([]);let ok=true;for(let v of [[],{n:1},()=>3,'s']){"
        "a.set(v);a.set(v);ok=ok && a.get()===v && a.get()===v}ok");
}
TEST_F(JsMvpLmd, GuardedMethodParametersAndReturns) {
    boolean("class A{constructor(){this.x=1}set(v){this.x=v}sum(v){return this.x+v}}"
        "let a=new A();let n=0;a.set(4,++n);a.sum(2)===6 && a.set()===undefined && "
        "a.x===undefined && n===1");
    boolean("class A{f(x){x+=1;let y=x*2;if(y>8)return y;return x}"
        "g(x){return this.f(x)+this.f(1)}}let a=new A();a.g(4)===12 && a.f(0)===1");
    boolean("class A{constructor(x){this.x=x}f(v){return this.x+v}"
        "g(other){return other.f(2)+this.x}}let a=new A(1);let b=new A(10);"
        "a.g(b)===13 && b.g(a)===13 && ({x:4,f:a.f}).f(3)===7");
    boolean("class A{f(x){return x+1}}let a=new A();"
        "a.f((a.f=x=>x+10,3))===4 && a.f(3)===13");
    boolean("class A{f(x){if(x===0)return 1;return this.f(x-1)+1}}new A().f(8)===9");
    boolean("class A{f(x){if(x)this.x=2}target(v){return new.target}}let a=new A();"
        "a.f(false)===undefined && a.f(true)===undefined && a.x===2 && a.target(1)===undefined");
    boolean("class A{f(x){const a=x;if(a)return a;return 0}g(x){return this.f(x)}}"
        "let a=new A();let p=[1,2];a.g(p)===p && a.g(5e-324)===5e-324");
    boolean("class A{constructor(){this.x=0}f(v){this.x+=v;if(v>2)return this.x;return v}}"
        "let a=new A();let total=0;for(let i=0;i<5;i++)total+=a.f(i);total===19 && a.x===10");
    error("class A{f(x){const a=x;a=3;return a}}new A().f(1)", "TypeError");
    error("class A{f(x){let y=z;let z=x;return y}}new A().f(1)", "ReferenceError");
}
TEST_F(JsMvpLmd, ConstructorLayoutReuseAndObservation) {
    boolean("class A{constructor(x,y){this.x=x;this.y=y;this.z=null}}let ok=true;"
        "for(let v of [1,2,1.5,'s',true,null,5e-324,{n:1}]){let a=new A(v,[v]);"
        "ok=ok && a.x===v && a.y[0]===v && a.z===null && Object.keys(a).join(',')==='x,y,z'}ok");
    boolean("let seen='';function observe(a){seen+=Object.keys(a).join(',')+';';return 2}"
        "class A{constructor(){this.x=1;this.y=observe(this)}}new A();new A();seen==='x;x;'");
    boolean("class A{constructor(){this.x=Object.keys(this).length;this.y=1}}"
        "new A().x===0 && new A().x===0");
    boolean("class A{constructor(){this.x=1;this.y=2}}class B extends A{constructor(){super();this.z=3}}"
        "new A();let b=new B();Object.keys(b).join(',')==='x,y,z' && b instanceof A && b instanceof B");
    boolean("class A{constructor(x){this.x=x;if(x)this.y=2}}new A(true);"
        "Object.keys(new A(false)).join(',')==='x'");
    boolean("class A{constructor(){this.x='y' in this;this.y=1}}new A().x===false && new A().x===false");
}
TEST_F(JsMvpLmd, ScalarClassLocals) {
    numeric("class P{constructor(x,y){this.x=x;this.y=y}}function f(n){let total=0;"
        "for(let i=0;i<n;i++){let p=new P(i,2);p.x+=p.y;total+=p.x}return total}f(10)", 65);
    boolean("class P{constructor(x,y){this.x=x;this.y=y}}let n=0;"
        "let p=new P(++n,++n,++n);p.x===1 && p.y===2 && n===3");
    boolean("class P{constructor(x){this.x=x}}let p=new P(5e-324);let q=new P();"
        "p.x===5e-324 && q.x===undefined");
    numeric("class P{constructor(x){this.x=x*2+1}}let p=new P(3);p.x", 7);
    boolean("let seen;class P{constructor(x){this.x=x;seen=this}}let p=new P(4);p===seen");
    boolean("class P{constructor(x){this.x=x}}let p=new P(4);let q=p;p.x=5;q.x===5 && p instanceof P");
    boolean("class P{constructor(x){this.x=x}}class Q{constructor(x){this.x=x+10}}"
        "P=Q;let p=new P(1);p.x===11");
    error("function f(){let p=new P(1);return p.x}f();class P{constructor(x){this.x=x}}", "ReferenceError");
    boolean("class P{constructor(x){this.x=x}}let p=new P(-0);1/p.x===-Infinity");
    boolean("class P{constructor(x){this.x=x}f(){return this.x+1}}let p=new P(4);p.f()===5");
    numeric("class P{constructor(x){this.x=x}}function f(x){let p=new P(x);return p.x+1}f(2)", 3);
    char* mir = dump("temp/mvp_scalar_class.mir");
    ASSERT_NE(mir, nullptr);
    EXPECT_EQ(strstr(mir, "mvp_lmd_class_invoke"), nullptr);
    mem_free(mir);
}
TEST_F(JsMvpLmd, GuardedCallbackCellsAndReceiver) {
    numeric("function outer(){let x=1;let f=v=>{x+=v;return x};return f(2)+f(3)+x}outer()", 15);
    boolean("function outer(){let x=5e-324;let f=()=>x;let a=f();x=1e-323;"
        "return a===5e-324 && f()===1e-323}outer()");
    numeric("function outer(){let x=1;let f=v=>{x+=v;return x};"
        "let a=f((f=v=>v+10,2));return a+f(2)}outer()", 15);
    numeric("class A{constructor(x){this.x=x}f(v){let g=n=>this.x+n;return g(v)}}"
        "let a=new A(4);let b=new A(10);a.f(2)+b.f(3)", 19);
    boolean("class A{constructor(){const f=()=>new.target;this.same=f()===A}}new A().same");
    numeric("function allocate(n){return new Array(n).length}"
        "class A{constructor(){this.x=7}f(){const g=n=>allocate(n)+this.x;return g(3)}}new A().f()", 10);
    error("class A{}class B extends A{constructor(){const f=()=>this;f();super()}}new B()", "ReferenceError");
    numeric("function outer(){let x=1;const f=v=>{let y=x+v;if(v>0)return y;return x};"
        "x=4;return f(2)+f(0)}outer()", 10);
    numeric("function outer(){let x=0;const f=v=>{x+=v;if(v>2)return x;return v};"
        "let total=0;for(let i=0;i<5;i++)total+=f(i);return total+x}outer()", 29);
}
TEST_F(JsMvpLmd, StableNullableClassFields) {
    boolean("class A{constructor(){this.x=null;this.y=5e-324}get(){return this.x}set(v){this.x=v}}"
        "let a=new A();let b=new A();let p={n:3};let ok=true;"
        "for(let i=0;i<30;i++){a.set(p);ok=ok && a.get()===p;a.set(null);ok=ok && a.get()===null}"
        "for(let v of [5e-324,1e-323,-0,1.5,3,'s',undefined,true,[],()=>4,p,null]){"
        "a.set(v);ok=ok && a.get()===v}"
        "ok && a.y===5e-324 && b.x===null && b.y===5e-324 && "
        "Object.keys(a).join(',')==='x,y' && a instanceof A");
    boolean("class A{constructor(){this.x=null}set(v){this.x=v}get(){return this.x}}let a=new A();"
        "a.set({});a.set(()=>5);a.get()()===5 && a.set(-0)===undefined && 1/a.get()===-Infinity");
    Item result = run("class A{constructor(){this.x=null;this.y=1}}let a=new A();"
        "a.x={n:2};a.x=null;let b=new A();b.x={};b.x=null;b.x={n:3};[a,b]");
    ASSERT_EQ(get_type_id(result), LMD_TYPE_ARRAY);
    Map* a = result.array->items[0].map;
    Map* b = result.array->items[1].map;
    ASSERT_EQ(a->type, b->type);
    ShapeEntry* field = typemap_hash_lookup((TypeMap*)a->type, "x", 1);
    ASSERT_NE(field, nullptr);
    EXPECT_EQ(field->type, &TYPE_MAP);
    EXPECT_EQ(map_shape_field_to_item(a->data, field).item, ITEM_NULL);
    EXPECT_EQ(get_type_id(map_shape_field_to_item(b->data, field)), LMD_TYPE_MAP);
    result = run("class N{constructor(next){this.next=next}}let leaf=new N(null);"
        "let root=new N(leaf);let tail=new N(null);[root,tail]");
    ASSERT_EQ(get_type_id(result), LMD_TYPE_ARRAY);
    a = result.array->items[0].map; b = result.array->items[1].map;
    // constructor reuse must not turn immutable links into dynamic lanes across separate instances.
    EXPECT_EQ(typemap_hash_lookup((TypeMap*)a->type, "next", 4)->type->type_id, LMD_TYPE_MAP);
    EXPECT_EQ(typemap_hash_lookup((TypeMap*)b->type, "next", 4)->type->type_id, LMD_TYPE_NULL);
    result = run("class N{constructor(){this.next=null}}let leaf=new N();"
        "let root=new N();root.next=leaf;[root,leaf]");
    ASSERT_EQ(get_type_id(result), LMD_TYPE_ARRAY);
    a = result.array->items[0].map; b = result.array->items[1].map;
    // factory initialization after new must retain the same packed pointer lane as constructor writes.
    EXPECT_EQ(typemap_hash_lookup((TypeMap*)a->type, "next", 4)->type->type_id, LMD_TYPE_MAP);
    EXPECT_EQ(typemap_hash_lookup((TypeMap*)b->type, "next", 4)->type->type_id, LMD_TYPE_NULL);
}
TEST_F(JsMvpLmd, CompatibleNumericClassStoresKeepShape) {
    Item result = run(R"JS(
        class Point { constructor() { this.x=1.5; this.y=2.5; } }
        let first=new Point(); let second=new Point();
        first.x=9007199254740991; first.y=0;
        [first,second];
    )JS");
    ASSERT_EQ(get_type_id(result), LMD_TYPE_ARRAY);
    Map* first = result.array->items[0].map;
    Map* second = result.array->items[1].map;
    EXPECT_EQ(first->type, second->type);
    ShapeEntry* field = typemap_hash_lookup((TypeMap*)first->type, "x", 1);
    ASSERT_NE(field, nullptr);
    EXPECT_EQ(field->type, &TYPE_FLOAT);
    EXPECT_EQ(map_shape_field_to_item(first->data, field).get_double(), 9007199254740991.0);
    boolean(R"JS(
        class Point { constructor() { this.x=5e-324; } }
        let p=new Point(), ok=true;
        for(let i=0;i<20;i++) {
            p.x=5e-324; const tiny=p.x; p.x=i;
            ok=ok && tiny===5e-324 && p.x===i;
            p.x=-0; ok=ok && 1/p.x===-Infinity;
            p.x='s'; ok=ok && p.x==='s';
            delete p.x; ok=ok && !Object.hasOwn(p,'x');
            p.x=i; ok=ok && p.x===i;
        }
        ok;
    )JS");
}
TEST_F(JsMvpLmd, NullableConstructorAllocationShapes) {
    Item result = run(R"JS(
        class Link { constructor() { this.left=null; this.right=null; this.parent=null; } }
        function fresh() { return new Link(); }
        let seed=fresh(); fresh(); seed.extra=7;
        seed.left={value:1}; seed.right={value:2}; seed.parent=seed;
        let empty=fresh(); let full=fresh();
        full.left=seed.left; full.right=seed.right; full.parent=seed;
        [empty,full,seed];
    )JS");
    ASSERT_EQ(get_type_id(result), LMD_TYPE_ARRAY);
    Map* empty = result.array->items[0].map;
    Map* full = result.array->items[1].map;
    EXPECT_EQ(empty->type, full->type);
    EXPECT_EQ(((TypeMap*)empty->type)->length, 3);
    const char* keys[] = {"left", "right", "parent"};
    for (const char* key : keys) {
        ShapeEntry* field = typemap_hash_lookup((TypeMap*)empty->type, key, strlen(key));
        ASSERT_NE(field, nullptr);
        EXPECT_EQ(field->type, &TYPE_MAP);
        EXPECT_EQ(map_shape_field_to_item(empty->data, field).item, ITEM_NULL);
        EXPECT_EQ(get_type_id(map_shape_field_to_item(full->data, field)), LMD_TYPE_MAP);
    }
    boolean(R"JS(
        class Link { constructor() { this.next=null; } }
        let seed=new Link(); seed.next={value:3};
        let n=new Link(); let ok=n.next===null && Object.keys(n).join(',')==='next';
        n.next=seed; n.next=null; ok=ok && n.next===null;
        n.next='s'; ok=ok && n.next==='s'; delete n.next;
        ok=ok && !Object.hasOwn(n,'next'); n.next=5e-324;
        let later=new Link();
        ok && n.next===5e-324 && later.next===null && later instanceof Link;
    )JS");
    boolean(R"JS(
        class Link { constructor(value) { this.next=null; this.next=value; } }
        let seed=new Link(null); seed.next={}; let n=new Link('s');
        n.next==='s' && new Link(null).next===null;
    )JS");
    boolean(R"JS(
        class Link { constructor(value) { this.value=value; this.next=null; } }
        function create(value) { return new Link(value); }
        let seed=create(false); seed.next={value:9}; let ok=true;
        for(let i=0;i<20;i++) {
            const value=i%2 ? {value:i} : true;
            const link=create(value);
            ok=ok && link.value===value && link.next===null;
            link.next=seed; link.next=null;
            ok=ok && link.next===null && +link.next===0;
        }
        const a=create(undefined), b=create(null), c=create(5e-324), d=create('s');
        ok && a.value===undefined && +a.value!==+a.value && b.value===null &&
            c.value===5e-324 && d.value==='s';
    )JS");
}
TEST_F(JsMvpLmd, ClosureEnvironmentUsesSharedTracing) {
    Item result = run("function f(){let value=[5e-324,'alive'];return ()=>value}f()");
    ASSERT_EQ(get_type_id(result), LMD_TYPE_FUNC);
    ASSERT_EQ(result.function->closure_field_count, 1);
    ASSERT_NE(result.function->closure_env, nullptr);
    heap_gc_collect();
    result = mvp_lmd_result(execution);
    Item cell = ((Item*)result.function->closure_env)[0];
    ASSERT_EQ(get_type_id(cell), LMD_TYPE_ARRAY);
    ASSERT_EQ(cell.array->length, 1);
    Item values = cell.array->items[0];
    ASSERT_EQ(get_type_id(values), LMD_TYPE_ARRAY);
    EXPECT_EQ(it2d(values.array->items[0]), 5e-324);
    EXPECT_STREQ(values.array->items[1].get_string()->chars, "alive");
}
