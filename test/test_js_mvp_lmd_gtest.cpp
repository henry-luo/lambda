#include <gtest/gtest.h>
#include "lambda/js/mvp-lmd/mvp_lmd.h"
#include "lambda/js/mvp-lmd/mvp_lmd_runtime.h"
#include "lambda/js/js_object_meta.h"
#include "lambda/runtime/runtime-state.h"
#include "lambda/runtime/context_capsule.h"
#include "lambda/runtime/heap_api.h"
#include "lambda/runtime/transpiler.hpp"
#include "lambda/runtime/gc/gc_heap.h"
#include "lib/file.h"
#include "lib/mem.h"
#include "lib/hashmap.h"
#include "lib/strbuf.h"
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
TEST_F(JsMvpLmd, BigIntSharedArithmeticAndOwnership) {
    boolean(R"JS(
        let large=123456789012345678901234567890n;
        let values=[large, 2n]; let object={value:large};
        function show(value){return value.toString();}
        function counter(){let n=0n;return ()=>++n;}
        let next=counter();next();
        typeof values[0]==='bigint' && show(values[0]*values[1])==='246913578024691357802469135780' &&
          object.value===large && next()===2n && large/10n===12345678901234567890123456789n &&
          -10n%3n===-1n && (1n<<70n).toString(16)==='400000000000000000' &&
          0xdeadbeefn.toString(16)==='deadbeef' && ~1n===-2n && 2n**10n===1024n &&
          1n==1 && 1n!==1 && 1n<'2' && -2n< -1.5 && 0n=='' && 1n==' 1 ' &&
          1n!=null && 1n!=undefined && 1n==true && 1n+'x'==='1x' &&
          Number(9007199254740993n)===9007199254740992 && !0n && !!large;
    )JS");
    error("1n+1", "TypeError");
    error("+1n", "TypeError");
    error("1n/0n", "RangeError");
    error("2n**-1n", "RangeError");
    error("1n>>>0n", "TypeError");
    error("1n.toString(1)", "RangeError");
}
TEST_F(JsMvpLmd, DefaultAndRestParameters) {
    boolean(R"JS(
        let calls=0;
        function f(a=++calls,b=a+1,...rest){return [a,b,rest];}
        let a=f(), b=f(undefined,8,1,2), c=f(null);
        function closure(a=3,b=()=>a){a++;return b;}
        calls===2 && a[0]===1 && a[1]===2 && a[2].length===0 &&
          b[0]===2 && b[1]===8 && b[2].join(',')==='1,2' &&
          c[0]===null && c[1]===1 && closure()()===4;
    )JS");
    error("function f(a=b,b=2){return a} f()", "ReferenceError");
    error("function f(a=a){return a} f()", "ReferenceError");
}
TEST_F(JsMvpLmd, ArrayFlattenReduceSomeIncludes) {
    boolean(R"JS(
        let holes=new Array(3); holes[1]=[1,[2],new Array(2)];
        let flat=holes.flat(); let deep=holes.flat(Infinity);
        let values=new Array(4); values[1]=3;values[3]=5;
        let calls=0;
        let sum=values.reduce((sum,value,index,array)=>{
          calls++;if(index===1){array[2]=4;array.push(9);}return sum+value;
        },0);
        let seen=0; let found=values.some(value=>{seen++;return value===4;});
        flat.length===3 && flat[0]===1 && flat[1][0]===2 && flat[2].length===2 &&
          deep.join(',')==='1,2' && holes.flat(0).length===1 &&
          sum===12 && calls===3 && found && seen===2 &&
          new Array(2).includes(undefined) && [NaN].includes(NaN) &&
          [0].includes(-0) && ![1,2].includes(1,1) && [1,2].includes(2,-1) &&
          [].reduce((a,b)=>a+b,undefined)===undefined &&
          [1,2,3].reduce((a,b)=>a+b)===6 && ![].some(()=>true);
    )JS");
    error("new Array(3).reduce((a,b)=>a+b)", "TypeError");
    error("[].some(1)", "TypeError");
}
TEST_F(JsMvpLmd, JsonUsesSharedParserAndMutableStorage) {
    boolean(R"JS(
        let data=JSON.parse('{"name":"a\\u0000b","values":[1,2,5e-324],"flag":true,"empty":null}');
        data.values.push(3);data.values[0]=4;data.extra='kept';
        let text=JSON.stringify(data), copy=JSON.parse(text);
        copy.name.length===3 && copy.name.charCodeAt(1)===0 &&
          copy.values.length===4 && copy.values[0]===4 && copy.values[2]===5e-324 &&
          copy.extra==='kept' && copy.flag && copy.empty===null &&
          JSON.stringify({b:undefined,a:1,'2':2,'1':1})==='{"1":1,"2":2,"a":1}' &&
          JSON.stringify([undefined,NaN,Infinity,null])==='[null,null,null,null]' &&
          JSON.stringify(undefined)===undefined && JSON.stringify(-0)==='0' &&
          JSON.stringify('\ud800')==='"\\ud800"' && JSON.parse('"\\ud800"').charCodeAt(0)===55296 &&
          1/JSON.parse('-0')===-Infinity && JSON.parse('9007199254740993')===9007199254740992 &&
          JSON.parse('1e400')===Infinity && JSON.parse('{"x":1,"x":2}').x===2;
    )JS");
    error("JSON.parse('[1,]')", "SyntaxError");
    error("JSON.parse('01')", "SyntaxError");
    error("JSON.parse('\\v1')", "SyntaxError");
    error("JSON.parse('1\\u0000')", "SyntaxError");
    error("let a={};a.self=a;JSON.stringify(a)", "TypeError");
    error("JSON.stringify(1n)", "TypeError");
}
TEST_F(JsMvpLmd, DateClockAndArrayStringCoercion) {
    boolean(R"JS(
        let date=new Date(123.9);date.extra='kept';
        let invalid=new Date(NaN);let cycle=[1];cycle.push(cycle);cycle.push([2,3]);
        let now=Date.now(), after=new Date().getTime();
        date.getTime()===123 && date.valueOf()===123 && date.extra==='kept' &&
          date instanceof Date && date.constructor.name==='Date' &&
          invalid.getTime()!==invalid.getTime() && 1/new Date(-0).getTime()===Infinity &&
          now>1700000000000 && after>=now &&
          [1,[2,3],null,undefined].toString()==='1,2,3,,' &&
          String(cycle)==='1,,2,3' && 'pixels:'+[[1,2],[3,4]]==='pixels:1,2,3,4' &&
          Object.keys(date).join(',')==='extra' && Object.keys(JSON).length===0;
    )JS");
}
TEST_F(JsMvpLmd, RegExpStateCapturesAndReplacement) {
    boolean(R"JS(
        let re=/(a)(b)?/g, first=re.exec('a ab'), next=re.exec('a ab');
        let good=first[0]==='a' && first[1]==='a' && first[2]===undefined && first.index===0 &&
          next[0]==='ab' && next.index===2 && re.lastIndex===4 && re.exec('a ab')===null && re.lastIndex===0;
        let sticky=/a/y;sticky.lastIndex=1;
        good=good && sticky.test('ba') && sticky.lastIndex===2 && !sticky.test('ba') && sticky.lastIndex===0;
        let empty=/(?:)/g;
        good && 'aaa'.match(/a/g).length===3 && 'xyz'.match(/a/)===null &&
          'a\nb\nc'.replace(/\n/g,'')==='abc' &&
          'ab a'.replace(/(a)(b)?/g,'$2-$1-$$')==='b-a-$ -a-$' &&
          'aba'.replace(/a/g,function(m,i,s){return i+':'+s.length;})==='0:3b2:3' &&
          'ab'.replace(empty,'-')==='-a-b-' && empty.lastIndex===0 &&
          'x\ud83d\ude00y'.match(/./g).length===4 && 'x\ud83d\ude00y'.match(/./gu).length===3 &&
          /(?<letter>a)/.exec('ba').groups.letter==='a' &&
          'aba'.replace(/(?<letter>a)/g,'$<letter>!')==='a!ba!' &&
          /a(?!b)/.test('ac') && !/a(?!b)/.test('ab') &&
          new RegExp('a','gi').flags==='gi' && new RegExp('a') instanceof RegExp;
    )JS");
    error("new RegExp('[','')", "SyntaxError");
    error("new RegExp('a','gg')", "SyntaxError");
    boolean(R"JS(
        let text='x\ud83d\ude00\u00e9y', offsets=[];
        let replaced=text.replace(/./gu,function(s,i){
            offsets.push(i);let trash=[];for(let j=0;j<40;j++)trash.push({s:s});return s;
        });
        replaced===text && offsets.join(',')==='0,1,3,4' &&
          text.replace(/(?:)/gu,'-')==='-x-\ud83d\ude00-\u00e9-y-' &&
          'a\ud800b'.match(/./g).length===3 &&
          /\u00e9/u.exec(text).index===3;
    )JS");
}
TEST_F(JsMvpLmd, FunctionCallApplyBindOwnership) {
    boolean(R"JS(
        function sum(a,b){return this.base+a+b;}
        let object={base:7}, bound=sum.bind(object,2), nested=bound.bind({base:90},3);
        let args=[1.5,2.25];
        function snapshot(a,b){args[0]=99;args[1]=100;return a+b;}
        let test=/a/.test.bind(/a/);
        sum.call(object,1,2)===10 && sum.apply(object,[3,4])===14 && bound(4)===13 && nested()===12 &&
          bound.length===1 && snapshot.apply(null,args)===3.75 && test('cat') && !test('dog') &&
          Object.prototype.toString.call([])==='[object Array]' &&
          Object.prototype.toString.call(1)==='[object Number]' &&
          Object.prototype.hasOwnProperty.call({a:undefined},'a') &&
          !Object.prototype.hasOwnProperty.call({},'a');
    )JS");
}
TEST_F(JsMvpLmd, ArrayEditsAndInheritedEnumeration) {
    boolean(R"JS(
        let a=new Array(4);a[1]=1.25;a[3]=3.5;
        let b=a.concat([5],6), cut=a.splice(1,2,7,8,9);
        let ok=b.length===6 && !(0 in b) && !(2 in b) && b[1]===1.25 && b[5]===6 &&
          cut.length===2 && cut[0]===1.25 && !(1 in cut) && a.join(',')===',7,8,9,3.5';
        ok=ok && a.unshift(0)===6 && a[0]===0 && !(1 in a) && a[5]===3.5;
        function Base(){this.own=1;} Base.prototype.inherited=2;
        let x=new Base(), keys='';for(let key in x){keys+=key+',';}
        let deleted={a:1,b:2}, names='';for(let key in deleted){names+=key;delete deleted.b;}
        let skipped=0;for(let key in null){skipped++;}
        ok && keys==='own,inherited,' && names==='a' && skipped===0;
    )JS");
    boolean(R"JS(
        var exposed=3;let hidden=4;globalThis.exposed=5;
        let ok=exposed===5 && globalThis.hidden===undefined;
        exposed=7;globalThis.self=globalThis;
        ok && globalThis.exposed===7 && globalThis.self===this;
    )JS");
}
TEST_F(JsMvpLmd, OptionalChainsAndPrimitiveConversion) {
    boolean(R"JS(
        let calls=0, absent=null, object={base:4, fn:function(x){return this.base+x;}};
        let missing=absent?.[calls++].x;
        let empty=object.missing?.(calls++);
        let good=missing===undefined && empty===undefined && calls===0 &&
          object?.fn?.(3)===7 && delete absent?.x &&
          ({x:7})?.[absent?.x]===undefined;
        let order='', converted={valueOf:function(){order+='v';return 6;},toString:function(){order+='s';return 'text';}};
        good=good && converted+''==='6' && String(converted)==='text' && +converted===6 && order==='vsv';
        let values=[1]; values.push.apply(values,[2,5e-324]);
        let tiny={valueOf:()=>5e-324}, zero={valueOf:()=>-0};
        let mixed=[undefined,null,true,'3',1], numbers=mixed.map(x=>+x);
        good && values.length===3 && values[2]===5e-324 &&
          +tiny===5e-324 && 1/+zero===-Infinity &&
          Number.isNaN(numbers[0]) && numbers.slice(1).join(',')==='0,1,3,1' &&
          Number.isNaN(NaN) && !Number.isNaN('x') && !Number.isNaN({}) &&
          isNaN('x') && !isNaN(new Date(1)) && isNaN(/a/g) &&
          /a/g+''==='/a/g' && String(/a/i)==='/a/i' &&
          Array.isArray([]) && !Array.isArray({}) && !Array.isArray(new Int32Array(1));
    )JS");
    error("let x=null;(x?.y).z", "TypeError");
    error("let x={y:1};x.y?.()", "TypeError");
    boolean("let order='',slot={value:5e-324},left={valueOf(){order+='l';slot.value=7;return 0;}};let result=left+slot?.value;result===5e-324&&slot.value===7&&order==='l'");
    boolean("let order='',a={valueOf(){order+='a';return 1;}},b={valueOf(){order+='b';return 2;}};let observed=a?.value; a<b&&order==='ab'");
}
TEST_F(JsMvpLmd, DataDescriptorTransitionsAndPrototypeReflection) {
    boolean(R"JS(
        let a={},b={},c={};
        Object.defineProperty(a,'x',{value:1});
        Object.defineProperty(b,'x',{value:2,enumerable:true,writable:true,configurable:true});
        Object.defineProperty(c,'x',{value:3});
        a.x=9;b.x=8;c.x=7;
        let names='';for(let key in a){names+=key;}
        let ok=a.x===1 && b.x===8 && c.x===3 && names==='' &&
          Object.keys(a).length===0 && Object.keys(b)[0]==='x' && !delete a.x;
        Object.defineProperty(b,'x',{writable:false}); b.x=10;
        Object.defineProperty(a,'x',{value:1});
        let values=[]; Object.defineProperty(values,'named',{value:4});values.named=9;
        ok && b.x===8 && delete b.x && values.named===4 && !delete values.named &&
          Object.getPrototypeOf(a)===Object.prototype && Object.getPrototypeOf([])===Array.prototype &&
          Object.getPrototypeOf(Array.prototype)===Object.prototype && Object.getPrototypeOf(Object.prototype)===null &&
          Object.getPrototypeOf(new Date(1)).constructor.name==='Date' &&
          Object.getPrototypeOf(/a/).constructor.name==='RegExp';
    )JS");
    error("let a={};Object.defineProperty(a,'x',{value:1});Object.defineProperty(a,'x',{value:2})", "TypeError");
    error("'use strict';let a={};Object.defineProperty(a,'x',{value:1});a.x=2", "TypeError");
    error("'use strict';let a=[];Object.defineProperty(a,'x',{value:1});a.x=2", "TypeError");
    error("'use strict';let a={};Object.defineProperty(a,'x',{value:1});delete a.x", "TypeError");
}
TEST_F(JsMvpLmd, ExplicitCatchFinallyCompletions) {
    boolean(R"JS(
        let trace='';
        function pass(value){throw value;}
        function choose(){try{return 1;}finally{return 2;}}
        function recover(){try{pass(5e-324);}catch(e){return e;}finally{trace+='f';}}
        let object={tag:'kept'}, caught;
        try {try{pass(object);}finally{trace+='i';}} catch(e) {caught=e;trace+='c';}
        let total=0;
        for(let i=0;i<4;i++) {
            try {if(i===1)continue;if(i===3)break;total+=i;}
            finally {trace+=i;}
        }
        let name='';try {null.x;} catch(e) {name=e.name;}
        let thrown;try {try {throw 1;}finally{throw 2;}}catch(e){thrown=e;}
        let x=recover();
        caught===object && trace==='ic0123f' && total===2 && name==='TypeError' && thrown===2 && choose()===2 && x===5e-324;
    )JS");
    boolean("let n=0;try{for(let i=0;i<2;i++){break;}n++;}finally{n+=2;}n===3");
    boolean("let n=0;try{throw undefined;}catch(e){n=e===undefined?1:0;}n===1");
    error("try {new Date('unsupported');}catch(e){1;}", "capability");
}
TEST_F(JsMvpLmd, GeneratorsUseLambdaActivations) {
    boolean("function* g(){yield 1;}let it=g();let a=it.next();a.value===1&&!a.done&&it.next().done");
    boolean(R"JS(
        let trace='';
        function* values(a=(trace+='p',5e-324)) {
            trace+='b';
            try {let received=yield a;yield received+1;}
            catch(e) {yield e;}
            finally {trace+='f';yield 'cleanup';}
            return 9;
        }
        let g=values(), before=trace==='p', first=g.next(), second=g.next(4);
        let stop=g.return(7), done=g.next();
        let h=values(3);h.next();let thrown=h.throw('caught'), clean=h.next(), end=h.next();
        let unused=values(2), closed=unused.return(6);
        class Holder {constructor(v){this.value=v;} *read(){yield this.value;}}
        let instance=new Holder(11), method=instance.read();
        before && first.value===5e-324 && !first.done && second.value===5 && !second.done &&
          stop.value==='cleanup' && !stop.done && done.value===7 && done.done &&
          thrown.value==='caught' && clean.value==='cleanup' && end.value===9 && end.done &&
          closed.done && closed.value===6 && unused.next().done && method.next().value===11 &&
          trace==='pbfbf';
    )JS");
    boolean("function* g(a=missing){yield a;}let ok=false;try{g();}catch(e){ok=e.name==='ReferenceError';}ok");
    boolean("function* g(){yield 1;}let it=g();let ok=false;try{it.throw(7);}catch(e){ok=e===7;}ok&&it.next().done");
    boolean("let it;function* g(){try{it.next();}catch(e){yield e.name;}}it=g();it.next().value==='TypeError'");
    boolean("function* g(){return 5e-324;}let result=g().next();result.done&&result.value===5e-324");
    boolean(R"JS(
        function* item(n){let saved={n:n,value:5e-324};yield saved;return saved.value;}
        let ok=true;
        for(let round=0;round<3;round++) {
            let active=[];
            for(let i=0;i<48;i++){let g=item(i);ok=ok&&g.next().value.n===i;active.push(g);}
            for(let i=0;i<48;i++){let result=active[i].next();ok=ok&&result.done&&result.value===5e-324;}
        }
        ok;
    )JS");
}
TEST_F(JsMvpLmd, SymbolPropertyIdentity) {
    boolean(R"JS(
        let a=Symbol('x'), b=Symbol('x'), o={x:3};o[a]=1;o[b]=2;
        o[Symbol.iterator]=4;
        typeof a==='symbol' && a!==b && !!a && o[a]===1 && o[b]===2 && o.x===3 &&
          o[Symbol.iterator]===4 && Object.keys(o).join(',')==='x' &&
          Object.hasOwn(o,a) && delete o[a] && !Object.hasOwn(o,a) && o[b]===2
    )JS");
    boolean("let a=Symbol(),b=Symbol();a!==b&&typeof a==='symbol'");
    error("new Symbol('x')", "TypeError");
}
TEST_F(JsMvpLmd, FixedPrototypeCreationAndCopy) {
    boolean(R"JS(
        let base={a:1}, child=Object.create(base), leaf=Object.create(child);
        base.a=2;child.b=3;leaf.c=4;
        let flat=Object.assign({},leaf), spread={a:9,...child,c:5};
        let symbol=Symbol('s');base[symbol]=7;let copied={...base};
        let nil=Object.create(null);nil.x=1;
        Object.getPrototypeOf(child)===base&&Object.getPrototypeOf(leaf)===child&&
          leaf.a===2&&leaf.b===3&&Object.keys(flat).join(',')==='c'&&
          spread.a===9&&spread.b===3&&spread.c===5&&copied[symbol]===7&&
          Object.getPrototypeOf(nil)===null&&nil.toString===undefined&&nil.x===1
    )JS");
    boolean("function* g(){yield 1;}let f=Object.assign(g,{params:[2]});f===g&&g.params[0]===2&&g().next().value===1");
    boolean("let a=Object.assign({},'a😀');a[0]==='a'&&a[1]==='\\ud83d'&&a[2]==='\\ude00'");
    error("let p={};Object.defineProperty(p,'x',{value:1});Object.assign(Object.create(p),{x:2})", "TypeError");
}
TEST_F(JsMvpLmd, ObjectBindingPatterns) {
    boolean("function f({a,b=3}={a:2}){return a+b;}f()===5&&f({a:1,b:4})===5&&f.length===0");
    boolean("let f=({key,value})=>key+value;f({key:1,value:2})===3");
    boolean("function f({a},b=()=>a){a++;return b;}f({a:4})()===5");
    boolean("let {a:x,b:y=4,n:{z}}={a:1,n:{z:3}};let a=0;({a}= {a:7});x===1&&y===4&&z===3&&a===7");
    boolean("let sum=0;for(let {key,value=2} of [{key:1},{key:3,value:4}])sum+=key+value;sum===10");
    boolean("let key='x', count=0;let {[key]:v=(count++,2)}={x:1};v===1&&count===0");
    boolean("let value=0;try{throw {a:7};}catch({a}){value=a;}value===7");
    error("let {}=null", "TypeError");
}
TEST_F(JsMvpLmd, NonConstructorFunctionProperties) {
    boolean("let o={x:2,method(y){return this.x+y;},*iterate(){yield this.x;}};o.method(3)===5&&o.iterate().next().value===2&&o.method.prototype===undefined");
    error("let o={method(){}};new o.method()", "TypeError");
    boolean("let f=x=>x+1;f.ast={value:2};f.trace=x=>x;f(2)===3&&f.ast.value===2&&f.trace(4)===4&&f.prototype===undefined&&Object.keys(f).join(',')==='ast,trace'");
    boolean("class A{method(x){return x;}}let f=A.prototype.method;f.extra=2;f(3)===3&&f.extra===2&&f.prototype===undefined");
    error("let f=x=>x;f.extra=1;new f()", "TypeError");
    error("let f=x=>x;({}) instanceof f", "TypeError");
}
TEST_F(JsMvpLmd, CollectionIteratorAndSet) {
    boolean("let m=new Map([[1,2]]);Object.defineProperty(m,'get',{value:()=>9});m.get(1)===9&&[...m][0][1]===2");
    boolean("let m=new Map([[1,2]]);const O=Object;O.assign(m,{has:()=>false});!m.has(1)&&m.get(1)===2");
    boolean("let a=[1,2];const O=Object;O.defineProperty(a,'map',{value:()=>9});a.map(x=>x+1)===9");
    boolean("let m=new Map([[1,2]]),a=[1];({f:m.get,g:a.map}={f:()=>7,g:()=>8});m.get(1)===7&&a.map(x=>x)===8");
    boolean("let m=new Map([[1,2]]);let before=[...m.entries()];function key(){m.get=function(){return 9;};return 1;}before[0][1]===2&&m.get(key())===2&&m.get(1)===9");
    boolean("let m=new Map([[1,2]]);m.has=function(){return 'own';};let f=x=>x;f.get=function(){return 3;};[...m].length===1&&m.has(1)==='own'&&f.get()===3");
    boolean("let s=new Set([1,2,2,NaN,NaN,-0]);s.size===4&&s.has(0)&&s.has(NaN)&&s.add(3)===s&&s.delete(2)&&!s.has(2)&&s.get===undefined&&s.set===undefined&&s.keys===s.values&&s.values===s[Symbol.iterator]");
    boolean("let m=new Map([[1,2],[3,4]]), it=m.entries();m.set(5,6);it.next().value.join(',')==='1,2'&&it.next().value.join(',')==='3,4'&&it.next().value.join(',')==='5,6'&&it.next().done");
    boolean("let s=new Set([1,2]),it=s.values();let first=it.next().value;s.delete(2);s.add(3);first===1&&it.next().value===3&&it.next().done");
    boolean("let m=new Map([[1,2]]),it=m.keys();m.clear();m.set(3,4);it.next().value===3&&it.next().done");
    boolean("let s=new Set([1,2]),a=[];for(let n of s)a.push(n);Array.from(s).join(',')==='1,2'&&a.join(',')==='1,2'&&s.entries().next().value.join(',')==='1,1'");
    boolean("let m=new Map([[1,2]]);m[Symbol.iterator]=function*(){yield 7;};Array.from(m)[0]===7");
    error("new Set().add.call(new Map(),1)", "TypeError");
    error("new Set().values.call(new Map())", "TypeError");
    error("let s=new Set();new Map().keys.call(s)", "TypeError");
}
TEST_F(JsMvpLmd, PublicInstanceFields) {
    boolean("class A{static a=new A();x=3;}A.a.x===3");
    boolean("let count=0;class A{x=++count;y=this.x+1;z;}let a=new A(),b=new A();a.x===1&&b.x===2&&a.y===2&&b.y===3&&Object.hasOwn(a,'z')&&a.z===undefined");
    boolean("let order='';class A{x=(order+='a',1);constructor(){order+='b';}}class B extends A{x=(order+='c',2);constructor(){super();order+='d';}}let b=new B();order==='abcd'&&b.x===2&&b instanceof A&&b instanceof B");
    boolean("class A{x=1;}class B extends A{y=2;}class C extends B{}let c=new C();c.x===1&&c.y===2");
    boolean("let value=1;class A{x=value;fn=()=>this.x;}value=2;let a=new A();a.x===2&&a.fn()===2");
}
TEST_F(JsMvpLmd, LibraryVisitorsAndStringPrimitives) {
    boolean("let map=Array.prototype.map;map.call('abc',(v,i)=>v+i).join(',')==='a0,b1,c2'&&map.call({0:2,2:4,length:3},x=>x*2).join(',')==='4,,8'");
    boolean("let a=[1,,2,3];a.filter(x=>x%2).join(',')==='1,3'&&a.indexOf(undefined)===-1&&a.indexOf(2)===2&&[NaN].indexOf(NaN)===-1");
    boolean("let a=[1,2,3],visit=Array.prototype.filter;visit.call(a,(x,i)=>{if(i===0)a.length=1;return true;}).join(',')==='1'");
    boolean("let reduce=Array.prototype.reduce;reduce.call([5e-324,5e-324],(a,b)=>a+b)===1e-323");
    boolean("Number.parseInt('ff',16)===255&&Number.parseFloat('  -1.25e2rest')===-125&&Number.parseFloat('1e+')===1&&Number.parseFloat('0x10')===0&&Number.isNaN(Number.parseFloat(''))");
    boolean("Number.isFinite(1)&&!Number.isFinite('1')&&!Number.isFinite(Infinity)&&!Number.isFinite(NaN)&&Number.POSITIVE_INFINITY===Infinity");
    boolean("(255).toString(16)==='ff'&&(1.25).toString()==='1.25'&&(-16).toString(2)==='-10000'&&'abc'.endsWith('bc')&&'abcd'.endsWith('bc',3)&&'abc'.endsWith('bc',undefined)");
    boolean("'x'.padStart(4,'ab')==='abax'&&'x'.padStart(2,'😀')==='\\ud83dx'&&'x'.padStart(3)==='  x'&&'x'.padStart(5,'')==='x'");
    error("(1).toString(1)", "RangeError");
}
TEST_F(JsMvpLmd, IteratorProtocolAndDelegation) {
    boolean("function* g(){yield 9;}let a=[1,,3],seen=[];for(let x of a){seen.push(x);if(seen.length===1)a.push(4);}seen.length===4&&seen[1]===undefined&&seen[3]===4&&g().next().value===9");
    boolean("let a=[1,2];a[Symbol.iterator]=function*(){yield 7;};let sum=0;for(let x of a)sum+=x;sum===7");
    boolean("function* g(){yield 1;yield 2;}Array.from(g(),(v,i)=>v+i).join(',')==='1,3'&&[0,...g(),3].join(',')==='0,1,2,3'");
    boolean("function* g(){yield 2;yield 3;}function add(a,b,c){return a+b+c;}let a=[1];a.push(...g());add(...a)===6");
    boolean("class A{get ordinary(){return 1;}}'ordinary' in new A()&&Object.keys(A.prototype).length===0");
    numeric("class A{get ordinary(){return 1;}}new A().ordinary", 1);
    boolean("let a=[1,,3],it=a[Symbol.iterator]();a.push(4);it[Symbol.iterator]()===it&&it.next().value===1&&it.next().value===undefined&&it.next().value===3&&it.next().value===4&&it.next().done");
    boolean("let it='a😀b'[Symbol.iterator]();it.next().value==='a'&&it.next().value==='😀'&&it.next().value==='b'&&it.next().done");
    boolean("function* g(){yield* [1,2];yield 3;return 4;}let it=g();it.next().value===1&&it.next().value===2&&it.next().value===3&&it.next().value===4&&it.next().done");
    boolean("function* inner(){let n=yield 1;return n;}function* outer(){return yield* inner();}let it=outer();it.next().value===1&&it.next(5).value===5&&it.next().done");
    boolean("function* inner(){try{yield 1;}finally{yield 2;}}function* outer(){yield* inner();return 3;}let it=outer();it.next().value===1&&it.return(7).value===2&&it.next().value===3&&it.next().done");
    boolean("let trace='';function* g(){try{yield 1;yield 2;}finally{trace+='c';}}let total=0;for(let v of g()){total+=v;break;}total===1&&trace==='c'");
    boolean("function* g(){yield 1;yield 2;}let total=0;for(let v of g())total+=v;total===3");
    boolean("let total='';for(let ch of 'a😀b')total+=ch;function* g(){} total==='a😀b'");
    boolean(R"JS(
        let closed=0, object={}, result={value:3,done:false,extra:4};
        object[Symbol.iterator]=function(){return this;};
        object.next=function(){return result;};
        object.return=function(){closed++;throw 2;};
        let caught=0;try {for(let v of object)throw 1;}catch(e){caught=e;}
        let failed=0;try {for(let v of object)break;}catch(e){failed=e;}
        function* g(){yield* object;}let it=g();
        caught===1&&failed===2&&closed===2&&it.next()===result
    )JS");
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
    boolean("function F(){}let p={};F.prototype=p;Object.getPrototypeOf(new F())===p");
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
    char* mir = dump("temp/mvp_lmd_cube_construct.mir");
    ASSERT_NE(mir, nullptr);
    EXPECT_EQ(strstr(mir, "\timport\tmvp_lmd_array_index_property\n"), nullptr);
    EXPECT_EQ(strstr(mir, "\timport\tmvp_lmd_to_primitive\n"), nullptr);
    mem_free(mir);
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
    numeric(R"JS(
        class Probe {
            order(a,b) { if(a===b)return 0; if(a<b)return -1; return 1; }
            read(a) { const first=this.order(a,2); return first+this.order.x+this.order.y; }
        }
        let p=new Probe(); p.read(1);
    )JS", NAN);
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
TEST_F(JsMvpLmd, PolymorphicMethodExpansionHasCallerBudget) {
    StrBuf* source = strbuf_new();
    ASSERT_NE(source, nullptr);
    for (int i = 0; i < 12; i++) strbuf_append_format(source,
        "class C%d {constructor(child){this.child=child;}"
        "toString(){return '['+this.child.toString()+']';}}", i);
    strbuf_append_str(source,
        "function show(value){return value.toString();}"
        "let value=new C11(new C7(new C2(31)));"
        "show(value)==='[[[31]]]' && show(31)==='31';");
    boolean(source->str); strbuf_free(source);
    char* mir = dump("temp/mvp_polymorphic_budget.mir");
    ASSERT_NE(mir, nullptr);
    // A wide method graph must not grow exponentially at each nested call.
    EXPECT_LT(strlen(mir), 20000000u);
    mem_free(mir);
}
TEST_F(JsMvpLmd, MathNamespaceCallsAndSpread) {
    boolean("let m=Math;m.abs=function(x){return 'changed'};Math.abs(-3)==='changed'&&Object.keys(Math).length===0");
    boolean("Math.PI=0;Math.PI>3&&delete Math.PI===false");
    error("'use strict';Math.PI=0", "TypeError");
    boolean(R"JS(
        let methods=['sqrt','abs','floor','min','max']; let sum=0;
        for(let method of methods){if(method in Math)sum+=Math[method](4,9);}
        let round=Math.round, minimum=Math.min;
        sum===23 && Math.max(...[1,7,3])===7 && minimum()===Infinity &&
          minimum(3,NaN)!==minimum(3,NaN) && 1/minimum(0,-0)===-Infinity &&
          round(-1.5)===-1 && 1/round(-0.1)===-Infinity &&
          typeof Math.sqrt==='function';
    )JS");
}
TEST_F(JsMvpLmd, ShiftPreservesHolesAndScalarOwnership) {
    boolean("[] instanceof Array && new Array(2) instanceof Array && [] instanceof Object && !(Array.prototype instanceof Array) && Array.prototype instanceof Object");
    boolean("let key=Symbol('own'),a=[];a[key]=1;a.hasOwnProperty(key)&&Map.assign===undefined&&Set.keys===undefined&&RegExp.create===undefined");
    boolean("let caught=0;try{({}).hasOwnProperty({toString(){throw 7;}});}catch(e){caught=e;}caught===7");
    boolean("!({}).hasOwnProperty('__proto__')");
    boolean(R"JS(
        let values=[,5e-324,3]; let absent=values.shift();
        let shift=Array.prototype.shift, tiny=shift.call(values,99);
        let later=[1,2,3]; later.shift();
        absent===undefined && tiny===5e-324 && values.length===1 && values[0]===3 &&
          shift.call([])===undefined && later.join(',')==='2,3';
    )JS");
    boolean("let a=[1,,3],b=a.toReversed();b.join(',')==='3,,1'&&b.hasOwnProperty(1)&&!a.hasOwnProperty(1)&&a.at(-1)===3&&a.at(-4)===undefined&&a.at(Infinity)===undefined");
    boolean("String.fromCodePoint(65,0x1f600,0xd800)==='A😀\\ud800'&&String.fromCodePoint()===''&&String.fromCharCode(0x10041)==='A'");
    error("String.fromCodePoint(0x110000)", "RangeError");
    error("String.fromCodePoint(1.5)", "RangeError");
}
TEST_F(JsMvpLmd, NumericConstructorRegionsKeepIdentityAndLanes) {
    boolean(R"JS(
        class Holder {
            constructor(keys=Object.keys(this).join(',')) {
                this.items=new Array(2);this.keys=keys;
            }
        }
        let a=new Holder(),b=new Holder();a.keys===''&&b.keys==='';
    )JS");
    boolean(R"JS(
        class Storage {constructor(n){this.items=n===undefined||n===0?null:new Array(n);this.size=0;}}
        class Holder {constructor(n){this.storage=new Storage(n);this.ready=true;}}
        let ok=true, saved=[];
        for(let i=0;i<30;i++){
            let h=new Holder(i%3);saved.push(h);
            ok=ok && h.ready && h.storage.size===0 &&
                Object.keys(h).join(',')==='storage,ready' &&
                Object.keys(h.storage).join(',')==='items,size';
            if(i%3===0)ok=ok&&h.storage.items===null;
            else {ok=ok&&h.storage.items.length===i%3&&!(0 in h.storage.items);h.storage.items[0]=i;}
        }
        ok && saved[1].storage.items[0]===1 && saved[2].storage.items[0]===2;
    )JS");
    boolean(R"JS(
        let escaped=[];
        class Observe {constructor(owner){escaped.push(Object.keys(owner).join(','));}}
        class Holder {constructor(){this.first=1;this.child=new Observe(this);this.last=2;}}
        let a=new Holder(),b=new Holder();escaped.join('|')==='first|first'&&a.child!==b.child;
    )JS");
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
    boolean("function f(){const keys=[0,1];const a=[7,9];let i=Math.sqrt(9);"
        "return a[keys[i]]===undefined} f()");
    char* mir = dump("temp/mvp_optional_index.mir");
    ASSERT_NE(mir, nullptr);
    // native optional indices must not round-trip through a boxed property-key join.
    EXPECT_EQ(strstr(mir, "index_key"), nullptr);
    mem_free(mir);
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
    boolean(R"JS(
        function equal(a,b){return a[0]===b[0];}
        function loose(a,b){return a[0]==b[0];}
        equal([-7],[-7]) && !equal([-7],[7]) && equal([0],[-0]) && !equal([NaN],[NaN]) &&
          equal([9007199254740991],[9007199254740991]) && equal([5e-324],[5e-324]) &&
          !equal([1],['1']) && !equal([0],[false]) && loose([1],['1']) && loose([0],[false]);
    )JS");
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
    error("[[{}]].join('')", "capability");
    boolean("let a=[{}];a.toString=()=>7;String(a)==='7'");
    boolean("let a=[{}];a.join=()=>8;String(a)==='8'");
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
    boolean(R"JS(
        let trace='', a=[3,4], symbol=Symbol('key');a[symbol]=9;a['undefined']=7;a['1.5']=8;
        function key(){return {toString(){trace+='k';return '1';}};}
        function store(k){a[k]=(trace+='v',5);return a[k];}
        function read(k){return a[k];}
        let assigned=store(key());
        assigned===5 && trace==='kvk' && read(-0)===3 && read(undefined)===7 &&
          read(1.5)===8 && read(symbol)===9 && read(NaN)===undefined;
    )JS");
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
    boolean("if(false){({get a(){return 1}})}true");
    boolean("function f(){return ()=>x; var x=1} f()()===undefined");
    numeric("{let x=1; function f(){return x}} f()", 1);
    numeric("function f(a=1){return a} f()", 1);
    boolean("function f(){return this} f()===f()");
    boolean("let a=[1,,2];a.length===3&&!Object.hasOwn(a,1)&&a[1]===undefined");
    error("var x=1; with([]){x=2}", "scope");
    numeric("try{1}catch(e){2}", 1);
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
    numeric("({get x(){return 1}}).x", 1);
    boolean("Object.getPrototypeOf({__proto__:null})===null");
    boolean("let o={};o.__proto__===Object.prototype");
    boolean("let o={};o['__'+'proto__']=1;Object.getPrototypeOf(o)===Object.prototype");
    boolean("new Map([]).size===0");
    boolean("let m=new Map();m.set(1,2);let f=m.get;f.call(m,1)===2");
    boolean("let m=new Map();m.set=()=>1;m.set()===1&&m.size===0");
    boolean("let o={};typeof o.toString==='function'");
    boolean("let o={};o[{}]=1;o['[object Object]']===1");
    numeric("let count=0;for(let k in {}){count++;}count", 0);
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
    boolean("class A{f(){return 1}}class B extends A{f(){return ()=>super.f()}}new B().f()()===1");
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

TEST_F(JsMvpLmd, PropertyExpansionAccessors) {
    boolean(R"JS(
        let calls=0; let o={value:2,get x(){calls++;return this.value},set x(v){this.value=v}};
        let first=o.x; o.x=4; let second=o.x++;
        first===2 && second===4 && o.value===5 && calls===2 && Object.hasOwn(o,'x');
    )JS");
    boolean(R"JS(
        class A { get x(){return this.value} set x(v){this.value=v} }
        class B extends A { constructor(){super();this.value=3} }
        let b=new B(); let first=b.x; b.x=8;
        first===3 && b.value===8 && !Object.hasOwn(b,'x') && Object.keys(A.prototype).length===0;
    )JS");
    boolean(R"JS(
        let o={value:1}; let get=function(){return this.value};
        Object.defineProperty(o,'x',{get:get,configurable:true,enumerable:true});
        Object.defineProperty(o,'x',{set:function(v){this.value=v}});
        o.x=9; let before=o.x;
        o.sibling='a';o.sibling=3;delete o.sibling;
        let after=o.x;
        Object.defineProperty(o,'x',{value:4,writable:true}); o.x=5;
        before===9 && after===9 && o.x===5 && Object.keys(o).join(',')==='value,x';
    )JS");
    boolean("let o={get x(){return 1},x:2};o.x===2");
    boolean("let o={x:2,get x(){return 3}};o.x===3");
    boolean("let o={get x(){return 2}};o.x=4;o.x===2");
    error("'use strict';let o={get x(){return 2}};o.x=4", "TypeError");
    error("Object.defineProperty({},'x',{get:1})", "TypeError");
    error("Object.defineProperty({},'x',{get:()=>1,value:2})", "TypeError");
    boolean("let o={};let f=()=>2;Object.defineProperty(o,'x',{get:f});Object.defineProperty(o,'x',{get:f});o.x===2");
    error("let o={};Object.defineProperty(o,'x',{get:()=>2});Object.defineProperty(o,'x',{get:()=>2})", "TypeError");
    boolean("let o={get x(){throw 7}};let seen=0;try{o.x}catch(e){seen=e}seen===7");
    boolean("let o={x:5e-324,get y(){return this.x}};Object.preventExtensions(o);o.x='wide';let a=o.y;o.x={v:7};a==='wide'&&o.y.v===7");
    boolean("let o={x:1};Object.setPrototypeOf(o,{set x(v){throw 9}});Object.seal(o);o.x=5e-324;Reflect.set(o,'x',2)&&o.x===2");
    boolean("let o={x:1};Object.freeze(o);!Reflect.set(o,'x',2)&&o.x===1");
    boolean("let a={x:1},b={set x(v){throw 9}};!Reflect.set(a,'x',2,b)&&a.x===1");
    boolean("let o={x:1};function put(v){o.x=v}put(2);Object.defineProperty(o,'x',{set(v){this.seen=v},configurable:true});put(7);o.seen===7");
    boolean("let o={x:1};function put(v){o.x=v}put(2);Object.defineProperty(o,'x',{writable:false});put(7);o.x===2");
    boolean("let o={x:1};function put(v){o.x=v}put(2);delete o.x;Object.setPrototypeOf(o,{set x(v){this.seen=v}});put(7);o.seen===7&&!Object.hasOwn(o,'x')");
    boolean("let o={x:5e-324};function put(v){o.x=v}put(1e-323);Object.preventExtensions(o);put({n:7});o.x.n===7");
}

TEST_F(JsMvpLmd, PropertyExpansionPrototypes) {
    boolean(R"JS(
        let p={x:1};let o=Object.create(p);let first=o.x;
        Object.setPrototypeOf(o,{x:2});
        first===1 && o.x===2 && Object.getPrototypeOf(p)===Object.prototype;
    )JS");
    boolean(R"JS(
        let p={set x(v){this.y=v},get x(){return this.y+1}};let o=Object.create(p);
        o.x=4;o.x===5 && o.y===4 && !Object.hasOwn(o,'x') && !Object.hasOwn(p,'y');
    )JS");
    boolean(R"JS(
        function C(){this.x=1}let old=new C();let initial=C.prototype;
        C.prototype={get y(){return this.x+1}};let current=new C();
        old instanceof C===false && current instanceof C && current.y===2 &&
            Object.getPrototypeOf(old)===initial && Object.getPrototypeOf(current)===C.prototype;
    )JS");
    boolean("let p={x:4};let o={__proto__:p,y:1};o.x===4 && Object.keys(o).join(',')==='y'");
    boolean("let o={['__proto__']:7};o.__proto__===7&&Object.getPrototypeOf(o)===Object.prototype");
    boolean("let o={};o.__proto__=null;Object.getPrototypeOf(o)===null && o.__proto__===undefined");
    error("let o={};Object.setPrototypeOf(o,o)", "TypeError");
    error("let a={};let b=Object.create(a);Object.setPrototypeOf(a,b)", "TypeError");
    boolean("class A{f(){return 1}}let a=new A();A.prototype.f=function(){return 3};a.f()===3");
}

TEST_F(JsMvpLmd, PropertyExpansionDescriptorsAndIntegrity) {
    boolean(R"JS(
        let calls=0;let o={get x(){calls++;return 2},set x(v){calls+=v}};
        let d=Object.getOwnPropertyDescriptor(o,'x');
        calls===0 && typeof d.get==='function' && typeof d.set==='function' &&
          d.enumerable && d.configurable && !Object.hasOwn(d,'value') &&
          Object.getOwnPropertyDescriptor(o,'absent')===undefined;
    )JS");
    boolean("let o={x:1};Object.preventExtensions(o);o.y=2;o.x=3;!Object.isExtensible(o)&&o.x===3&&!Object.hasOwn(o,'y')");
    boolean("let o={x:1};Object.seal(o);o.x=2;!delete o.x && Object.isSealed(o) && !Object.isFrozen(o) && o.x===2");
    boolean("let o={x:1};Object.freeze(o);o.x=2;Object.isFrozen(o)&&Object.isSealed(o)&&o.x===1");
    boolean("let n=0;let o={set x(v){n=v}};Object.freeze(o);o.x=7;n===7&&Object.isFrozen(o)");
    error("'use strict';let o={};Object.preventExtensions(o);o.x=1", "TypeError");
    error("let o={};Object.preventExtensions(o);Object.defineProperty(o,'x',{value:1})", "TypeError");
    error("let o={};Object.preventExtensions(o);Object.setPrototypeOf(o,null)", "TypeError");
    boolean("let p={};let o=Object.create(p);Object.preventExtensions(o);Object.setPrototypeOf(o,p)===o");
    boolean("let o={get x(){delete this.y;return 1},y:2};Object.values(o).join(',')==='1'");
    boolean("let o={get x(){return 1},y:2};Object.entries(o)[0][1]===1");
    boolean("let p={x:1};let o=Object.create(p);o.y=2;let s='';for(let k in o)s+=k;s==='yx'");
}

TEST_F(JsMvpLmd, PropertyExpansionSuperReceivers) {
    boolean(R"JS(
        class A { get x(){return this.value} set x(v){this.value=v} }
        class B extends A { read(){return super.x} write(v){super['x']=v} }
        let b=new B();b.value=2;let first=b.read();b.write(4);
        first===2 && b.value===4 && !Object.hasOwn(A.prototype,'value');
    )JS");
    boolean(R"JS(
        class A { f(){return 1} }
        class B extends A { f(){return super.f()+1} }
        let b=new B();let first=b.f();Object.setPrototypeOf(B.prototype,{f(){return 4}});
        first===2 && b.f()===5;
    )JS");
    boolean(R"JS(
        class A {} class B extends A { write(){super.x=5} }
        let b=new B();b.write();b.x===5 && !Object.hasOwn(A.prototype,'x');
    )JS");
}

TEST_F(JsMvpLmd, PropertyExpansionBulkDescriptors) {
    boolean("class C{get x(){return 1}x(){return 2}}let c=new C();c.x()===2");
    boolean("class C{x(){return 1}get x(){return 2}}let c=new C();c.x===2");
    boolean("class C{get x(){return this.y}set x(v){this.y=v}}let c=new C();c.x=4;c.x===4");
    boolean("class C{__proto__(){return 7}}let c=new C();c.__proto__()===7");
    boolean("class C{__proto__=8}let c=new C();c.__proto__===8");
    boolean("let o={get x(){return 2},y:3};Object.defineProperty(o,'x',{writable:true});o.x===undefined&&o.y===3");
    boolean(R"JS(
        let o={};let trace='';let d={
          get enumerable(){trace+='e';return true},
          get configurable(){trace+='c';return true},
          get value(){trace+='v';return 5e-324},
          get writable(){trace+='w';return true}};
        Object.defineProperties(o,{x:d,y:{get(){return this.x+1},enumerable:true}});
        trace==='ecvw' && o.x===5e-324 && o.y===1 && Object.keys(o).join(',')==='x,y';
    )JS");
    boolean(R"JS(
        let o={};let threw=false;
        try{Object.defineProperties(o,{x:{value:1},y:{get:2}})}catch(e){threw=true}
        threw && !Object.hasOwn(o,'x');
    )JS");
    boolean("let p={x:2};let o=Object.create(p,{y:{value:3,enumerable:true}});o.x===2&&o.y===3&&Object.keys(o).join(',')==='y'");
    boolean("function f(){};Object.defineProperty(f,'x',{get:()=>3})===f&&f.x===3");
    boolean("function f(){};let p=Object.getPrototypeOf(f);typeof p==='function'&&p()===undefined&&Object.getPrototypeOf(p)===Object.prototype");
    boolean("function f(){return this.x};let p=Object.getPrototypeOf(f);let original=p.call;p.call=function(){return 7};f.call({x:3})===7");
}

TEST_F(JsMvpLmd, PropertyExpansionSparseArrays) {
    Item promoted = run("let a=[5e-324];a.length=20000;for(let i=1;i<=5000;i++)a[i]=5e-324;a");
    ASSERT_EQ(get_type_id(promoted), LMD_TYPE_ARRAY);
    EXPECT_NE(container_js_elements_kind(promoted.array), JS_ELEMENTS_SPARSE_TAGGED);
    EXPECT_GE(container_dense_capacity(promoted.array), promoted.array->length);
    {
        RootFrame roots(1);
        Rooted<Item> held(roots, promoted);
        heap_gc_collect();
        promoted = held.get();
    }
    EXPECT_EQ(it2d(promoted.array->items[0]), 5e-324);
    EXPECT_EQ(it2d(promoted.array->items[5000]), 5e-324);
    EXPECT_EQ(promoted.array->items[5001].item, ITEM_JS_DELETED_SENTINEL);
    boolean("let a=new Array(20000);Object.defineProperty(a,'19999',{get(){return 7}});for(let i=0;i<6000;i++)a[i]=5e-324;let old=a[0];a[0]=1e-323;old===5e-324&&a[0]===1e-323&&a[5999]===5e-324&&!(6000 in a)&&a[19999]===7");
    boolean("let a=new Array(20000);a.fill(3);a[19999]===3&&a.length===20000&&Object.keys(a).length===20000");
    boolean("let a=new Array(20000);a[19999]=3;let b=a.slice(19998);b.length===2&&!(0 in b)&&b[1]===3");
    boolean("let a=new Array(20000);a[19999]=3;a.pop()===3&&a.length===19999");
    boolean("let a=new Array(20000);a[19999]=3;let found=a.indexOf(3,19998);found===19999");
    boolean("let a=new Array(20000);a.fill(3,19998);a[19998]===3&&a[19999]===3&&!(0 in a)");
    boolean("let a=new Array(4294967295);a[4294967294]=7;a.length===4294967295&&a[4294967294]===7&&a[0]===undefined&&!(0 in a)");
    boolean("let a=[1];a.note='ok';a[1000000000]=5e-324;Object.keys(a).join(',')==='0,1000000000,note'&&a.note==='ok'&&a[1000000000]===5e-324");
    boolean("let a=[];a[1000000000]=3;a.length=2;a.length=1000000001;!(1000000000 in a)&&a[1000000000]===undefined");
    boolean("let a=[];a[4294967295]=7;a.length===0&&a['4294967295']===7");
    boolean("let a=[1,2];Object.defineProperty(a,'1',{get(){return this[0]+4},configurable:true});a[1]===5&&Object.values(a).join(',')==='1,5'");
    boolean("let a=[];Object.defineProperty(a,'1000000000',{value:8,writable:true,enumerable:true,configurable:true});a.length===1000000001&&a[1000000000]===8&&Object.keys(a).join(',')==='1000000000'");
    boolean("let a=[1];Object.defineProperty(a,'length',{writable:false});a[1]=2;a.length===1&&!(1 in a)");
    error("'use strict';let a=[1];Object.defineProperty(a,'length',{writable:false});a[1]=2", "TypeError");
    boolean("let a=[0,1,2,3];Object.defineProperty(a,'2',{configurable:false});let caught=false;try{Object.defineProperty(a,'length',{value:1,writable:false})}catch(e){caught=true}caught&&a.length===3&&!(3 in a)&&!Object.getOwnPropertyDescriptor(a,'length').writable");
    boolean("let a=new Array(3);let p=Object.create(Array.prototype);Object.defineProperty(p,'1',{get(){return 4},set(v){this.x=v}});Object.setPrototypeOf(a,p);a[1]=8;a[1]===4&&a.x===8&&!(Object.hasOwn(a,'1'))&&a.map(x=>x+1).join(',')===',5,'");
    boolean("let a=[1,2];Object.defineProperty(a,'0',{get(){delete this[1];return 3}});a.slice().join(',')==='3,'");
    boolean("let a=[1,,3];Object.freeze(a);a[0]=9;a[1]=2;Object.isFrozen(a)&&a[0]===1&&!Object.hasOwn(a,'1')");
    error("let a=[1];Object.freeze(a);a.push(2)", "TypeError");
    error("let a=[];a.length=-1", "RangeError");
    boolean("let a=[];let p=Object.create(Array.prototype);Object.defineProperty(p,'0',{get(){return 9}});Object.setPrototypeOf(a,p);a.length=1;[...a][0]===9&&a.includes(9)&&a.indexOf(9)===0");
}

TEST_F(JsMvpLmd, PropertyExpansionReflection) {
    boolean("let a=[1,,3];a.note=4;Object.getOwnPropertyNames(a).join(',')==='0,2,length,note'");
    boolean("let s=Symbol();let t=Symbol('t');let o={[s]:1,[t]:2,x:3};let k=Object.getOwnPropertySymbols(o);k.length===2&&k[0]===s&&k[1]===t&&Object.keys(o).join(',')==='x'");
    boolean("let s=Symbol('x');let o={[s]:1,get x(){throw 2}};let d=Object.getOwnPropertyDescriptors(o);d[s].value===1&&typeof d.x.get==='function'");
    boolean("let O=Object;O.keys=function(){return ['override']};Object.keys({x:1}).join(',')==='override'");
    boolean("Array.prototype.join=function(){return 'override'};[1,2].join()==='override'");
}

TEST_F(JsMvpLmd, PropertyExpansionReflect) {
    boolean("let o={x:1};Reflect.set(o,'x',2)&&o.x===2&&Reflect.get(o,'x')===2&&Reflect.has(o,'x')&&Reflect.deleteProperty(o,'x')&&!Reflect.has(o,'x')");
    boolean("let o={};Object.preventExtensions(o);!Reflect.set(o,'x',1)&&!Reflect.defineProperty(o,'x',{value:1})&&!Reflect.setPrototypeOf(o,null)");
    boolean("let o={};Object.defineProperty(o,'x',{value:1});!Reflect.set(o,'x',2)&&!Reflect.deleteProperty(o,'x')&&!Reflect.defineProperty(o,'x',{value:2})&&Reflect.defineProperty(o,'x',{value:1})");
    boolean("let a={x:1};let b={};Reflect.set(a,'x',4,b)&&a.x===1&&b.x===4&&Reflect.get(a,'x',b)===1");
    boolean("let p={get x(){return this.y},set x(v){this.y=v}};let o={y:2};let first=Reflect.get(p,'x',o);Reflect.set(p,'x',5,o)&&first===2&&o.y===5&&!Object.hasOwn(p,'y')");
    boolean("let p={x:1};let n=0;let o={set x(v){n++}};!Reflect.set(p,'x',5,o)&&n===0");
    boolean("let o={get x(){return 1}};!Reflect.set(o,'x',2)");
    boolean("let n=0;try{Reflect.set({set x(v){throw 7}},'x',2)}catch(e){n=e}n===7");
    boolean("let n=0;try{Reflect.defineProperty({},'x',{get value(){throw 7}})}catch(e){n=e}n===7");
    boolean("let s=Symbol('s');let o={x:1,[s]:2};let keys=Reflect.ownKeys(o);keys.length===2&&keys[0]==='x'&&keys[1]===s&&Reflect.getOwnPropertyDescriptor(o,s).value===2");
    boolean("let a=[1,2,3];Object.defineProperty(a,'1',{configurable:false});!Reflect.defineProperty(a,'length',{value:0})&&a.length===2&&!(2 in a)");
    boolean("function f(a,b){return this.x+a+b}Reflect.apply(f,{x:2},{length:2,0:3,get 1(){return 4}})===9");
    boolean("function F(x){this.x=x}let o=Reflect.construct(F,[3]);o instanceof F&&o.x===3");
    error("Reflect.get(1,'x')", "TypeError");
    error("Reflect.defineProperty({},'x',{get:1})", "TypeError");
    error("Reflect.construct(()=>1,[])", "TypeError");
}

TEST_F(JsMvpLmd, PropertyExpansionEnumeration) {
    boolean("let p={};Object.defineProperty(p,'hidden',{get(){throw 1}});let o=Object.create(p);o.z=1;o[2]=2;o[1]=3;let keys=[];for(let k in o)keys.push(k);keys.join(',')==='1,2,z'");
    boolean("let p={a:1,b:2};let o=Object.create(p);Object.defineProperty(o,'a',{value:3});o.c=4;let keys=[];for(let k in o)keys.push(k);keys.join(',')==='c,b'");
    boolean("let p={x:1};let q=Object.create(p);Object.defineProperty(q,'x',{value:2});let o=Object.create(q);o.y=3;let keys=[];for(let k in o)keys.push(k);keys.join(',')==='y'");
    boolean("let o={x:1};let before=[];for(let k in o)before.push(k);Object.prototype.extra=2;let after=[];for(let k in o)after.push(k);delete Object.prototype.extra;before.join(',')==='x'&&after.join(',')==='x,extra'");
    boolean("let a=[3,,5];let before=[];for(let k in a)before.push(k);Array.prototype[1]=4;let after=[];for(let k in a)after.push(k);delete Array.prototype[1];before.join(',')==='0,2'&&after.join(',')==='0,2,1'");
    // ES 14.7.5.9 permits different trap order for exotic prototype chains.
    boolean("let own=0,desc=0,proto=0;let p=new Proxy({z:2},{ownKeys(t){own++;return Reflect.ownKeys(t)},getOwnPropertyDescriptor(t,k){desc++;return Reflect.getOwnPropertyDescriptor(t,k)},getPrototypeOf(t){proto++;return null}});let o=Object.create(p);o.x=1;let keys=[];for(let k in o)keys.push(k);keys.join(',')==='x,z'&&own===1&&desc===1&&proto===1");
    boolean("let o=Object.create(null);o['a\\u0000b']=1;o['😀']=2;o['e\\u0301']=3;o['é']=4;let keys=[];for(let k in o)keys.push(k);keys.length===4&&keys[0]==='a\\u0000b'&&keys[1]==='😀'&&keys[2]==='e\\u0301'&&keys[3]==='é'");
    boolean("let s=Symbol('x');let o={x:1,[s]:2};Object.defineProperty(o,'quiet',{value:3});let keys=[];for(let k in o)keys.push(k);keys.join(',')==='x'&&Reflect.ownKeys(o)[2]===s");
}

TEST_F(JsMvpLmd, PropertyExpansionProxyProperties) {
    boolean("let t={x:1};let p=new Proxy(t,{});p.x=2;p.y=3;p.x===2&&t.x===2&&t.y===3&&('x' in p)&&delete p.y&&!('y' in t)");
    boolean("let seen=false;let t={x:1};let p=new Proxy(t,{get(a,k,r){seen=a===t&&r===p;return k==='x'?4:Reflect.get(a,k,r)}});p.x===4&&seen");
    boolean("let seen=false;let t={};let p=new Proxy(t,{set(a,k,v,r){seen=a===t&&r===p&&k==='x'&&v===3;return false}});p.x=3;seen&&!Object.hasOwn(t,'x')&&!Reflect.set(p,'x',3)");
    boolean("let r={value:4};let p=new Proxy({get x(){return this.value}},{});Reflect.get(p,'x',r)===4");
    boolean("let s=Symbol();let ok=false;let p=new Proxy({[s]:3},{get(t,k){ok=k===s;return Reflect.get(t,k)}});p[s]===3&&ok");
    boolean("let n=0;let p=new Proxy({x:1},{get get(){n++;return null}});p.x===1&&n===1");
    boolean("let p=new Proxy({x:1},{get(t,k){return 5}});let o=Object.create(p);o.x===5");
    boolean("let seen=false;let p=new Proxy({},{set(t,k,v,r){seen=r===o;return Reflect.set(t,k,v,r)}});let o=Object.create(p);o.x=3;seen&&o.x===3");
    error("let p=new Proxy({},{get:1});p.x", "TypeError");
    error("'use strict';let p=new Proxy({},{set(){return false}});p.x=1", "TypeError");
    error("'use strict';let p=new Proxy({},{deleteProperty(){return false}});delete p.x", "TypeError");
    boolean("let p=new Proxy({x:1},{has(){return false},deleteProperty(){return true}});!('x' in p)&&delete p.x");
}

TEST_F(JsMvpLmd, PropertyExpansionProxyInvariants) {
    error("let t={};Object.defineProperty(t,'x',{value:1});new Proxy(t,{get(){return 2}}).x", "TypeError");
    error("let t={};Object.defineProperty(t,'x',{value:0});new Proxy(t,{get(){return -0}}).x", "TypeError");
    boolean("let t={};Object.defineProperty(t,'x',{value:NaN});let p=new Proxy(t,{get(){return NaN}});isNaN(p.x)");
    error("let t={};Object.defineProperty(t,'x',{get:undefined});new Proxy(t,{get(){return 2}}).x", "TypeError");
    error("let t={};Object.defineProperty(t,'x',{value:1});let p=new Proxy(t,{set(){return true}});p.x=2", "TypeError");
    error("let t={};Object.defineProperty(t,'x',{get(){return 1}});let p=new Proxy(t,{set(){return true}});p.x=2", "TypeError");
    error("let t={};Object.defineProperty(t,'x',{value:1});'x' in new Proxy(t,{has(){return false}})", "TypeError");
    error("let t={x:1};Object.preventExtensions(t);delete new Proxy(t,{deleteProperty(){return true}}).x", "TypeError");
    error("let p=new Proxy({},{getOwnPropertyDescriptor(){return null}});Object.getOwnPropertyDescriptor(p,'x')", "TypeError");
    error("let p=new Proxy({},{getOwnPropertyDescriptor(){return {value:1}}});Object.getOwnPropertyDescriptor(p,'x')", "TypeError");
    boolean("let p=new Proxy({},{getOwnPropertyDescriptor(){return {value:5e-324,configurable:true}}});let d=Object.getOwnPropertyDescriptor(p,'x');d.value===5e-324&&!d.writable&&!d.enumerable&&d.configurable");
    error("let t={};Object.preventExtensions(t);Reflect.defineProperty(new Proxy(t,{defineProperty(){return true}}),'x',{value:1})", "TypeError");
    boolean("let t={};let p=new Proxy(t,{defineProperty(t,k,d){return Reflect.defineProperty(t,k,d)}});Object.defineProperty(p,'x',{value:2});t.x===2");
    boolean("let p=new Proxy({},{defineProperty(){return false}});!Reflect.defineProperty(p,'x',{})");
}

TEST_F(JsMvpLmd, PropertyExpansionProxyKeysAndLinks) {
    boolean("let s=Symbol('s');let t={x:1,y:2,[s]:3};let p=new Proxy(t,{ownKeys(){return [s,'y','x']}});let k=Reflect.ownKeys(p);k[0]===s&&k[1]==='y'&&Object.keys(p).join(',')==='y,x'&&Object.values(p).join(',')==='2,1'");
    error("Reflect.ownKeys(new Proxy({},{ownKeys(){return ['x','x']}}))", "TypeError");
    error("Reflect.ownKeys(new Proxy({},{ownKeys(){return [1]}}))", "TypeError");
    error("let t={};Object.defineProperty(t,'x',{value:1});Reflect.ownKeys(new Proxy(t,{ownKeys(){return []}}))", "TypeError");
    error("let t={x:1};Object.preventExtensions(t);Reflect.ownKeys(new Proxy(t,{ownKeys(){return ['x','y']}}))", "TypeError");
    boolean("let t={};let q={};let p=new Proxy(t,{getPrototypeOf(){return q},setPrototypeOf(t,x){return false}});Object.getPrototypeOf(p)===q&&!Reflect.setPrototypeOf(p,null)");
    error("let t={};Object.preventExtensions(t);Object.getPrototypeOf(new Proxy(t,{getPrototypeOf(){return null}}))", "TypeError");
    error("Reflect.isExtensible(new Proxy({},{isExtensible(){return false}}))", "TypeError");
    error("Reflect.preventExtensions(new Proxy({},{preventExtensions(){return true}}))", "TypeError");
    boolean("let t={};let p=new Proxy(t,{preventExtensions(t){return Reflect.preventExtensions(t)}});Reflect.preventExtensions(p)&&!Reflect.isExtensible(p)");
    boolean("let t={x:1};let p=new Proxy(t,{});Object.freeze(p);Object.isFrozen(p)&&Object.isFrozen(t)");
    boolean("let p=new Proxy([1,2],{});Array.isArray(p)&&p.length===2&&p[1]===2");
}

TEST_F(JsMvpLmd, PropertyExpansionProxyCallsAndRevocation) {
    boolean("let p=new Proxy(function(x){return this.y+x},{});typeof p==='function'&&p.call({y:2},3)===5");
    boolean("let target=function(x){return x+1};let p=new Proxy(target,{apply(t,r,a){return t===target?a[0]*2:0}});p(3)===6");
    boolean("function F(x){this.x=x}let p=new Proxy(F,{construct(t,a,n){return {x:a[0]+1}}});new p(2).x===3");
    error("new (new Proxy(()=>1,{}))()", "TypeError");
    error("function F(){};new (new Proxy(F,{construct(){return 2}}))()", "TypeError");
    boolean("let r=Proxy.revocable(function(){return 1},{});let p=r.proxy;let first=p();r.revoke();r.revoke();let n=0;try{p()}catch(e){n++}first===1&&n===1&&typeof p==='function'");
    error("let r=Proxy.revocable({},{});r.revoke();r.proxy.x", "TypeError");
    error("let r=Proxy.revocable([],{});r.revoke();Array.isArray(r.proxy)", "TypeError");
    boolean("let p=new Proxy(new Proxy({x:3},{}),{});p.x===3");
    boolean("let p=new Proxy(()=>7,{});let o={};Object.defineProperty(o,'x',{get:p});o.x===7");
    error("let p=new Proxy(new Map(),{});p.set('x',1)", "TypeError");
    boolean("function F(x){this.x=x}let p=new Proxy(F,{});let o=new p(3);o.x===3&&o instanceof F");
    boolean("function F(x){this.x=x}let p=new Proxy(F,{});let b=p.bind(null,4);new b().x===4");
    boolean("function F(x){this.x=x}function G(){};let o=Reflect.construct(F,[3],G);o.x===3&&Object.getPrototypeOf(o)===G.prototype");
    boolean("let f=new Proxy(x=>x+1,{});[1,2].map(f).join(',')==='2,3'");
}

TEST_F(JsMvpLmd, PropertyExpansionProxyArrays) {
    boolean("let a=[1,,3];let p=new Proxy(a,{});p.push(4);let first=p.pop();p.reverse();first===4&&p.join(',')==='3,,1'&&p.slice().join(',')==='3,,1'");
    boolean("let p=new Proxy([1,,3],{});let f=new Proxy(x=>x+1,{});p.map(f).join(',')==='2,,4'");
    boolean("let p=new Proxy([3,1,2],{});p.sort(new Proxy((a,b)=>a-b,{}));p.join(',')==='1,2,3'");
    boolean("let p=new Proxy([1,2],{});[0].concat(p).join(',')==='0,1,2'&&[p].flat().join(',')==='1,2'");
    boolean("let a=[1,2];a.join=()=>7;Array.prototype.join.call(a)==='1,2'&&a.toString()===7");
    boolean("let a=[1,2];let separator={toString(){a.length=1;return '-'}};a.join(separator)==='1-'");
    boolean("let trace='';let p=new Proxy([1,,3],{get(t,k,r){trace+='g'+k+',';return Reflect.get(t,k,r)},has(t,k){trace+='h'+k+',';return Reflect.has(t,k)}});let a=Array.prototype.slice.call(p);a.join(',')==='1,,3'&&trace==='glength,gconstructor,h0,g0,h1,h2,g2,'");
    boolean("let p=new Proxy([1,2],{});[...p].join(',')==='1,2'");
}

TEST_F(JsMvpLmd, PropertyExpansionProxyProtocols) {
    boolean("function F(){};let p=new Proxy(F,{});let o=new p();o instanceof p&&o instanceof F");
    boolean("function F(){};let b=new Proxy(F,{}).bind(null);new b() instanceof b");
    boolean("let o={[Symbol.hasInstance](v){return v===4}};4 instanceof o&&!(5 instanceof o)");
    boolean("let o={[Symbol.toPrimitive](hint){return hint==='number'?3:4}};+o===3&&o+1===5");
    boolean("let seen=false;let o=new Proxy({},{get(t,k){if(k===Symbol.toPrimitive)return function(h){seen=this===o;return 5}}});+o===5&&seen");
    boolean("Object.prototype.toString.call(new Proxy([],{}))==='[object Array]'&&Object.prototype.toString.call({[Symbol.toStringTag]:'Example'})==='[object Example]'");
    error("let r=Proxy.revocable(function(){},{});r.revoke();1 instanceof r.proxy", "TypeError");
    error("let o={[Symbol.toPrimitive](){return {}}};+o", "TypeError");
    boolean("let a=[1,2];a[Symbol.isConcatSpreadable]=false;let b=[].concat(a);b.length===1&&b[0]===a");
    boolean("let o={0:7,length:2,[Symbol.isConcatSpreadable]:true};let a=[].concat(new Proxy(o,{}));a.length===2&&a[0]===7&&!(1 in a)");
    boolean("let log='';let a=new Proxy([1,2],{get(t,k,r){if(k==='length')log+='L';return Reflect.get(t,k,r)}});a.toReversed().join(',')==='2,1'&&log==='L'");
    boolean("let log='';let a=[1,2];let p=new Proxy(a,{get(t,k,r){if(k==='length')log+='L';if(k==='constructor')log+='C';return Reflect.get(t,k,r)}});let depth={valueOf(){log+='D';a.push(3);return 1}};p.flat(depth).join(',')==='1,2'&&log==='LDC'");
    boolean("let log='';let p=new Proxy([],{get(t,k,r){if(k==='length')log+='L';return Reflect.get(t,k,r)}});try{p.map(null)}catch(e){log+='E'}log==='LE'");
    boolean("let d=Object.getOwnPropertyDescriptor(Symbol,'iterator');!d.writable&&!d.enumerable&&!d.configurable&&Array[Symbol.species]===Array");
}

TEST_F(JsMvpLmd, PropertyExpansionProxyJson) {
    boolean("let p=new Proxy({x:2},{get(t,k,r){return k==='x'?3:Reflect.get(t,k,r)}});JSON.stringify(p)==='{\"x\":3}'");
    boolean("JSON.stringify(new Proxy([1,,3],{}))==='[1,null,3]'");
    boolean("let keys='';let value={toJSON(k){keys+=k;return 7}};JSON.stringify({x:value})==='{\"x\":7}'&&JSON.stringify([value])==='[7]'&&keys==='x0'");
    boolean("let value=new Proxy(function(){},{});JSON.stringify({x:value,y:2})==='{\"y\":2}'&&JSON.stringify([value,Symbol()])==='[null,null]'&&JSON.stringify(value)===undefined");
    boolean("let log='';let p=new Proxy({x:2},{get(t,k,r){log+='g'+k+',';return Reflect.get(t,k,r)},ownKeys(t){log+='k,';return ['x']},getOwnPropertyDescriptor(t,k){log+='d'+k+',';return Reflect.getOwnPropertyDescriptor(t,k)}});JSON.stringify(p)==='{\"x\":2}'&&log==='gtoJSON,k,dx,gx,'");
    boolean("let log='';let p=new Proxy([1],{get(t,k,r){log+='g'+k+',';return Reflect.get(t,k,r)}});JSON.stringify(p)==='[1]'&&log==='gtoJSON,glength,g0,'");
    error("let r=Proxy.revocable({},{});r.revoke();JSON.stringify(r.proxy)", "TypeError");
}

TEST_F(JsMvpLmd, PropertyExpansionLambdaBoundary) {
    Item result = run("class A{x=3}class B extends A{y=4}let before=new B(),after=new B();"
        "Object.setPrototypeOf(after,{z:5});Object.defineProperty(after,'observed',{get(){return 9}});[before,after]");
    ASSERT_EQ(get_type_id(result), LMD_TYPE_ARRAY);
    Map* before = result.array->items[0].map;
    Map* after = result.array->items[1].map;
    TypeMap* shape = (TypeMap*)after->type;
    ASSERT_EQ(shape->nominal, ((TypeMap*)before->type)->nominal);
    ASSERT_NE(shape->nominal->base, nullptr);
    ShapeEntry* original = typemap_hash_lookup((TypeMap*)before->type, "x", 1);
    ShapeEntry* retained = typemap_hash_lookup(shape, "x", 1);
    ASSERT_NE(original, nullptr);
    ASSERT_NE(retained, nullptr);
    EXPECT_EQ(original->byte_offset, retained->byte_offset);
    EXPECT_EQ(it2i(map_shape_field_to_item(after->data, retained)), 3);
    EXPECT_TRUE(item_is_error(map_shape_field_to_item(after->data, typemap_hash_lookup(shape, "observed", 8))));
    Item key = {.item = s2it(heap_create_name("observed", 8))};
    EXPECT_TRUE(item_is_error(fn_member(Item{.map = after}, key)));

    // foreign declared layouts stay read-only at the guest property adapter.
    TypeMap foreign_shape = *shape;
    TypeNominal foreign_nominal = *shape->nominal;
    foreign_nominal.extension = NULL;
    foreign_shape.nominal = &foreign_nominal;
    for (int trusted = 0; trusted < 2; trusted++) {
        foreign_shape.is_trusted_contract = trusted;
        after->type = &foreign_shape;
        EXPECT_EQ(mvp_lmd_property_map(Item{.map = after}, true), nullptr);
        EXPECT_EQ(retained->byte_offset, original->byte_offset);
    }
    after->type = shape;

    result = run("new Proxy({x:1},{get(){throw 17}})");
    ASSERT_EQ(get_type_id(result), LMD_TYPE_MAP);
    ASSERT_EQ(result.map->data, nullptr);
    key.item = s2it(heap_create_name("x", 1));
    EXPECT_TRUE(item_is_error(fn_member(result, key)));
    bool found = false;
    NameId name_id = property_key_id(key.get_string());
    ASSERT_NE(name_id, NAME_ID_NONE);
    EXPECT_TRUE(item_is_error(map_get_by_name_id(result.map, (TypeMap*)result.map->type,
        result.map->data, name_id, &found)));
    result = run("let p=new Proxy({x:1},{});p.y=2;Object.defineProperty(p,'z',{value:3});p");
    ASSERT_EQ(get_type_id(result), LMD_TYPE_MAP);
    EXPECT_EQ(result.map->data, nullptr);

    result = run("let key=Symbol('x');let p=new Proxy({[key]:7},{});[p,key]");
    ASSERT_EQ(get_type_id(result), LMD_TYPE_ARRAY);
    Item proxy = result.array->items[0], symbol = result.array->items[1];
    const JsPropertyOps* ops = ((TypeMap*)proxy.map->type)->js_meta->ops;
    Item fetched = ops->get(proxy, 0, symbol, ItemNull, proxy, ItemNull).completion;
    EXPECT_EQ(it2i(fetched), 7);
    Item keys = ops->own_keys(proxy, 0, ItemNull, ItemNull, proxy, ItemNull).completion;
    ASSERT_EQ(get_type_id(keys), LMD_TYPE_ARRAY);
    ASSERT_EQ(keys.array->length, 1);
    EXPECT_EQ(keys.array->items[0].item, symbol.item);

    result = run("let a=new Array(4294967295);a[1000000000]=5e-324;a");
    ASSERT_EQ(get_type_id(result), LMD_TYPE_ARRAY);
    EXPECT_EQ(result.array->length, UINT32_MAX);
    EXPECT_LT(container_dense_capacity(result.array), 1024);
    EXPECT_EQ(it2d(mvp_lmd_array_own_item(result.array, 1000000000)), 5e-324);
    EXPECT_TRUE(item_is_error(array_get(result.array, 1000000000)));
    EXPECT_TRUE(item_is_error(array_item_read(result.array, 1000000000)));
}

TEST_F(JsMvpLmd, PropertyExpansionComputedHooks) {
    boolean("let k='__'+'proto__';let o={};o[k]={x:7};o.x===7");
    boolean("let k='to'+'String';let o={[k](){return 'ok'}};String(o)==='ok'");
    boolean("let k='to'+'String';let o={[k]:()=> 'ok'};String(o)==='ok'");
    boolean("let k='__'+'proto__';let o={};o[k]!==undefined");
    boolean("let k='to'+'String';let o={};o[k]=()=> 'ok';String(o)==='ok'");
    boolean("let O=Object;let k='set'+'PrototypeOf';let o={};O[k](o,{x:4});o.x===4");
    boolean("let S=Symbol;let o={[S.toPrimitive](){return 4}};+o===4");
    boolean("class A{constructor(){this.x=1}}class B extends A{}function F(){this.x=7}Object.setPrototypeOf(B,F);new B().x===7");
}

TEST_F(JsMvpLmd, PropertyExpansionClassFields) {
    boolean("let n=0;class A{constructor(){Object.defineProperty(this,'x',{set(v){n++},configurable:true})}}class B extends A{x=7}let b=new B();b.x===7&&n===0");
    boolean("class A{static get x(){return 1}static x=3}A.x===3&&Object.getOwnPropertyDescriptor(A,'x').enumerable");
    error("class A{constructor(){Object.defineProperty(this,'x',{value:1})}}class B extends A{x=7}new B()", "TypeError");
    boolean("let n=0;class A{constructor(){return new Proxy({},{defineProperty(t,k,d){n++;return Reflect.defineProperty(t,k,d)}})}}class B extends A{x=7}new B().x===7&&n===1");
    Item result = run("class A{}class B extends A{}function F(){this.x=7}let before=new B();Object.setPrototypeOf(B,F);[before,new B()]");
    ASSERT_EQ(get_type_id(result), LMD_TYPE_ARRAY);
    EXPECT_EQ(((TypeMap*)result.array->items[0].map->type)->nominal, ((TypeMap*)result.array->items[1].map->type)->nominal);
}

TEST_F(JsMvpLmd, PropertyExpansionComputedKeyProof) {
    boolean("class A{m(){return 1}}let {prototype:p}=A;let a=new A();let old=a.m();p.m=()=>2;old===1&&a.m()===2");
    boolean("function A(){}A.prototype.m=()=>1;let {prototype:p}=A;let a=new A();let old=a.m();p.m=()=>2;old===1&&a.m()===2");
    boolean("function A(){this.axis=0;this.a=[7]}A.prototype.at=function(){return this.a[this.axis]};new A().at()===7");
    char* ordinary_field = dump("temp/mvp_lmd_ordinary_field_key_proof.mir");
    ASSERT_NE(ordinary_field, nullptr);
    EXPECT_EQ(strstr(ordinary_field, "\timport\tmvp_lmd_array_index_property\n"), nullptr);
    mem_free(ordinary_field);
    boolean("let o={};function A(){this.name=0}A.prototype.put=function(){o[this.name]={x:7}};let a=new A();let __proto__=function __proto__(){};a.put.call(__proto__);o.x===7");
    boolean("let o={};function A(){this.axis=0}A.prototype.put=function(){o[this.axis]={x:7}};let a=new A();a.put.call({axis:'__'+'proto__'});o.x===7");
    boolean("var random=(function(){var seed=0;return function(){return ++seed}})();var zeros=new Array(0,0,0);function A(){this.a=[7]}A.prototype.at=function(i){return this.a[i]};A.Node=function(){this.x=1};A.Node.prototype.get=function(){return this.x};new A().at(0)+new A.Node().get()+random()===9&&zeros.length===3");
    char* setup = dump("temp/mvp_lmd_fixed_prototype_setup.mir");
    ASSERT_NE(setup, nullptr);
    EXPECT_EQ(strstr(setup, "\timport\tmvp_lmd_array_index_property\n"), nullptr);
    mem_free(setup);
    boolean("function A(){}A.prototype.m=()=>1;let a=new A();let old=a.m();A.prototype.m=()=>2;old===1&&a.m()===2");
    boolean("function A(){}A.prototype.m=()=>1;let alias=A.prototype;let a=new A();let old=a.m();alias.m=()=>2;old===1&&a.m()===2");
    boolean("function A(){}A['prototype'].m=function(){return 7};new A().m()===7");
    boolean("function A(){}A.prototype.m=()=>1;let a=new A();function replace(){let old=a.m();A.prototype.m=()=>2;return old}replace()===1&&a.m()===2");
    boolean("function A(){}A.Node=function(){};A.Node.prototype.m=()=>1;let a=new A.Node();let old=a.m();A.Node=function(){};A.Node.prototype.m=()=>2;old===1&&a.m()===1&&new A.Node().m()===2");
    boolean("var index=0;globalThis.extra=1;for(index of ['__'+'proto__']){}let o={};o[index]={x:7};o.x===7");
    boolean("var index=0;globalThis.extra=1;[index]=['__'+'proto__'];let o={};o[index]={x:7};o.x===7");
    boolean("let index=0;[index]=['__'+'proto__'];let o={};o[index]={x:7};o.x===7");
    boolean("let o={};function put(k){for(k of ['__'+'proto__']){}o[k]={x:7}}put(0);o.x===7");
    boolean("let table={A:7};let text='A';let i=0;table[text[i]]===7");
    char* character = dump("temp/mvp_lmd_character_key_proof.mir");
    ASSERT_NE(character, nullptr);
    EXPECT_EQ(strstr(character, "\timport\tmvp_lmd_array_index_property\n"), nullptr);
    mem_free(character);
    boolean("var k=0;let g=globalThis;let text='k';g[text[0]]='__'+'proto__';let o={};o[k]={x:7};o.x===7");
    boolean("globalThis.extra=1;var index=0;let out=[];for(index=0;index<4;index++)out[index]=index;out[3]===3");
    char* global_index = dump("temp/mvp_lmd_global_scalar_key_proof.mir");
    ASSERT_NE(global_index, nullptr);
    EXPECT_EQ(strstr(global_index, "\timport\tmvp_lmd_array_index_property\n"), nullptr);
    mem_free(global_index);
    boolean("var index=0;globalThis.index='__'+'proto__';let o={};o[index]={x:7};o.x===7");
    boolean("var index=0;let g=globalThis;g.index='__'+'proto__';let o={};o[index]={x:7};o.x===7");
    boolean("var index=0;Object.assign(globalThis,{index:'__'+'proto__'});let o={};o[index]={x:7};o.x===7");
    boolean("let stack=[];for(let i=0;i<3;i++)stack.push(i);let out=[];out[stack.pop()]=7;out[2]===7");
    char* popped = dump("temp/mvp_lmd_popped_key_proof.mir");
    ASSERT_NE(popped, nullptr);
    EXPECT_EQ(strstr(popped, "\timport\tmvp_lmd_array_index_property\n"), nullptr);
    mem_free(popped);
    boolean("let stack=[];stack.push('__'+'proto__');let o={};o[stack.pop()]={x:7};o.x===7");
    boolean("let stack=[0];let alias=stack;alias.push('__'+'proto__');let o={};o[stack.pop()]={x:7};o.x===7");
    boolean("let stack=[0];stack.pop=()=> '__'+'proto__';let o={};o[stack.pop()]={x:7};o.x===7");
    boolean("class V{constructor(){this.storage=[1]}at(i){return this.storage[i]}}class A{constructor(){this.v=new V()}get(i){return this.v.at(i)}asArray(){return this}}class O{get(k){return new A()}asObject(){return this}}function parse(){return new O()}parse().asObject().get('x').asArray().get(0)===1");
    char* receivers = dump("temp/mvp_lmd_returned_receiver_proof.mir");
    ASSERT_NE(receivers, nullptr);
    EXPECT_EQ(strstr(receivers, "\timport\tmvp_lmd_array_index_property\n"), nullptr);
    mem_free(receivers);
    boolean("let o={};class A{asArray(){return this}put(k){o[k]={x:7}}}class O{put(k){}}let a=new A();let f=a.asArray;let foreign={put(k){o[k]={x:7}}};f.call(foreign).put('__proto__');o.x===7");
    boolean("globalThis.extra=1;let keys=[];let out=[];for(let i=0;i<4;i++){keys[i]=i;out[keys[i]]=i}out[3]===3");
    char* globals = dump("temp/mvp_lmd_global_local_key_proof.mir");
    ASSERT_NE(globals, nullptr);
    EXPECT_EQ(strstr(globals, "\timport\tmvp_lmd_array_index_property\n"), nullptr);
    mem_free(globals);
    boolean("let keys=[];let n=0;let out=[];for(let i=0;i<4;i++){keys[n++]=i;out[keys[n-1]]=i}out[3]===3");
    char* local_keys = dump("temp/mvp_lmd_local_array_key_proof.mir");
    ASSERT_NE(local_keys, nullptr);
    EXPECT_EQ(strstr(local_keys, "\timport\tmvp_lmd_array_index_property\n"), nullptr);
    mem_free(local_keys);
    boolean("let keys=[0];let alias=keys;alias[0]='__'+'proto__';let o={};o[keys[0]]={x:7};o.x===7");
    boolean("let keys=[[0]];let alias=keys[0];alias[0]='__'+'proto__';let o={};o[keys[0][0]]={x:7};o.x===7");
    boolean("let keys=[];keys=['__'+'proto__'];let o={};o[keys[0]]={x:7};o.x===7");
    boolean("let o={};for(let i=0;i<4;i++)o['key'+i]=i;Object.keys(o).length===4");
    char* keys = dump("temp/mvp_lmd_affixed_key_proof.mir");
    ASSERT_NE(keys, nullptr);
    EXPECT_EQ(strstr(keys, "\timport\tmvp_lmd_array_index_property\n"), nullptr);
    mem_free(keys);
    boolean("let n='g';let o={};o['toStrin'+n]=()=> 'ok';String(o)==='ok'");
    boolean("function A(){this.a=[];this.put=function(i){this.a[i]=7}}let a=new A();a.put(0);a.a[0]===7");
    boolean("let o={};function A(){this.put=function(k){o[k]={x:7}}}let a=new A();let f=a.put;f('__proto__');o.x===7");
    boolean("let o={};function A(){this.put=function(k){o[k]={x:7}}}let a=new A();Object.values(a)[0]('__proto__');o.x===7");
    boolean("let J=JSON;let value=J.parse('{\"key\":\"__proto__\"}');let o={};o[value.key]={x:7};o.x===7");
    boolean("class V{constructor(){this.a=[1]}at(i){return this.a[i]}}class D{at(k){return 1}}function make(){return new D()}class S{static a=make();static b=S.make();static make(){return new D()}}let v=new V();v.at(0)+S.a.at({})+S.b.at({})===3");
    char* factories = dump("temp/mvp_lmd_static_factory_proof.mir");
    ASSERT_NE(factories, nullptr);
    EXPECT_EQ(strstr(factories, "\timport\tmvp_lmd_array_index_property\n"), nullptr);
    mem_free(factories);
    boolean("let o={};class V{put(k){o[k]={x:7}}}class D{put(k){}}class S{static d=new D()}let alias=S;alias.d=new V();S.d.put('__proto__');o.x===7");
    boolean("let o={};class V{put(k){o[k]={x:7}}}class D{put(k){}}class S{static make(){return new D()}}let alias=S;alias.make=()=>new V();let v=S.make();v.put('__proto__');o.x===7");
    boolean("let o={};class V{put(k){o[k]={x:7}}}class D{put(k){}}class S{static make(){return new D()}}Object.assign(S,{make(){return new V()}});let v=S.make();v.put('__proto__');o.x===7");
    boolean("let o={};class V{put(k){o[k]={x:7}}}class D{put(k){}}class B{constructor(){return new V()}}class C extends B{put(k){}}let v=new C();v.put('__proto__');o.x===7");
    boolean("let table={indices:[[0,1],[1,0]]};let output=[];for(let i=0;i<table.indices.length;i++)output[table.indices[i][1]]=7;output[1]===7");
    char* mir = dump("temp/mvp_lmd_nested_key_proof.mir");
    ASSERT_NE(mir, nullptr);
    EXPECT_EQ(strstr(mir, "\timport\tmvp_lmd_array_index_property\n"), nullptr);
    mem_free(mir);
    boolean("let table={indices:[[0]]};table.indices[0][0]='__'+'proto__';let o={};o[table.indices[0][0]]={x:7};o.x===7");
    boolean("let table={indices:[[0]]};let row=table.indices[0];row[0]='__'+'proto__';let o={};o[table.indices[0][0]]={x:7};o.x===7");
    boolean("let table={indices:[[0]]};table.indices[0]=['__'+'proto__'];let o={};o[table.indices[0][0]]={x:7};o.x===7");
    boolean("let row=[0];let table={indices:[row]};row[0]='__'+'proto__';let o={};o[table.indices[0][0]]={x:7};o.x===7");
    boolean("let table={indices:[[0]]};table.indices[0]=[1];let output=[];output[table.indices[0][0]]=7;output[1]===7");
    boolean("class A{constructor(){this.datum=0;this.data=new Array(1).fill(0)}}let a=new A();a.data[0]='__'+'proto__';a.datum=a.data[0];let o={};o[a.datum]={x:7};o.x===7");
    boolean("class A{constructor(){this.datum=0;this.data=new Array(1).fill(0)}}let a=new A();let items=a.data;items[0]='__'+'proto__';a.datum=a.data[0];let o={};o[a.datum]={x:7};o.x===7");
    boolean("class A{constructor(){this.message=0}}let a=new A();let e;try{throw new Error('__'+'proto__')}catch(x){e=x}let o={};o[e.message]={x:7};o.x===7");
    boolean("class C{constructor(k){this[k]={x:7}}static ctor(){return C}}let F=C.ctor();let c=new F('__'+'proto__');c.x===7");
    boolean("class __proto__{constructor(){this.name=0}}let a=new __proto__();let o={};o[__proto__.name]={x:7};o.x===7");
    error("class X{getValue(){return 7}}let a=[new X()];let k='__'+'proto__';let x=a[k];x.getValue()", "TypeError");
    error("let k='to'+'String';let o={[k]:1};String(o)", "TypeError");
    boolean("class A{constructor(){this.name=0}static key(){return this.name}}let o={};o[A.key()]={x:7};Object.keys(o)[0]==='A'");
    boolean("class A{key(){return 0}put(){this[this.key()]={x:7}}}let a=new A();let f=a.put;let o={key(){return '__'+'proto__'}};f.call(o);o.x===7");
    boolean("class A{put(k,v){this[k]=v}}let a=new A();let {put}=a;put.call(a,'__'+'proto__',{x:7});a.x===7");
    boolean("class C{m(){return 1}}let c=new C();function f(){return c.m()}let n=f();let k='proto'+'type';C[k].m=()=>2;n===1&&f()===2");
    boolean("class A{put(k,v){this[k]=v}}let a=new A();let f=a['p'+'ut'];f.call(a,'__'+'proto__',{x:7});a.x===7");
    boolean("class A{key(){return 0}}let o={key(){return '__'+'proto__'}};let a={};a[o.key()]={x:7};a.x===7");
    boolean("class A{key(){return 0}}let a=new A();a.key=()=> '__'+'proto__';let o={};o[a.key()]={x:7};o.x===7");
    boolean("class A{put(k,v){this[k]=v}}let a=new A();let f=a.put;f.call(a,'__'+'proto__',{x:7});a.x===7");
    boolean("class A{constructor(){this.key=0}put(){this[this.key]={x:7}}}let a=new A();a.key='__'+'proto__';a.put();a.x===7");
    boolean("let {key}=JSON.parse('{\"key\":\"__proto__\"}');let a={};a[key]={x:7};a.x===7");
    boolean("class A{constructor(){this.i=0;this.a=[]}append(v){this.a[this.i]=v;this.i+=1}at(i){return this.a[i]}}let a=new A();a.append(7);a.at(0)===7");
    boolean("let d=Object.getOwnPropertyDescriptor(Set.prototype,'size');let s=new Set([1,2]);typeof d.get==='function'&&!d.enumerable&&d.configurable&&d.get.call(s)===2");
}

TEST_F(JsMvpLmd, PropertyExpansionPresentDenseElements) {
    boolean("class A{constructor(){this.x=1}}let a=new A();let unrelated={get y(){return 3}};function read(){return a.x}let before=read();Object.defineProperty(a,'x',{get(){return 7},configurable:true});before===1&&read()===7");
    boolean("class A{m(){return 1}}let a=new A();function read(){return a.m()}let before=read();A.prototype.m=()=>7;before===1&&read()===7");
    boolean("let a=[1,2];Object.defineProperty(a,'note',{get(){return 9}});function get(i){return a[i]}function put(i,v){a[i]=v}put(0,3);let before=get(0);Object.defineProperty(a,'0',{get(){return 7},set(v){this.seen=v},configurable:true});put(0,4);before===3&&get(0)===7&&a.seen===4&&get(1)===2");
    char* mir = dump("temp/mvp_lmd_present_dense.mir");
    ASSERT_NE(mir, nullptr);
    EXPECT_NE(strstr(mir, "i8:("), nullptr); // the existing sparse state is the sign bit of array_flags.
    EXPECT_NE(strstr(mir, "\tblt\t"), nullptr);
    EXPECT_NE(strstr(mir, "\timport\tmvp_lmd_array_index_property"), nullptr);
    mem_free(mir);
    boolean("let a=[1,2];Object.defineProperty(a,'length',{writable:false});Object.preventExtensions(a);a[0]=5e-324;a[0]===5e-324&&a[1]===2");
    boolean("let a=[1,2];function rhs(){Object.defineProperty(a,'0',{value:7,writable:false});return 9}a[0]=rhs();a[0]===7");
    boolean("let a=[1,2];function rhs(){delete a[0];Object.setPrototypeOf(a,{set 0(v){this.seen=v}});return 9}a[0]=rhs();!Object.hasOwn(a,'0')&&a.seen===9");
    boolean("let a=[1];let p=Object.create(Array.prototype);p['-1']=7;p.NaN=8;Object.setPrototypeOf(a,p);let k=-1;a[k]===7&&a[NaN]===8");
    boolean("let a=[1];let n=0;let k={toString(){n++;return '0'}};a[k]=7;a[k]===7&&n===2");
    boolean("let a=[];a.x=5e-324;Object.defineProperty(a,'y',{get(){return this.x}});a.y===(a.x=1e-323,5e-324)");
    boolean("let a=[];a.x=5e-324;Object.defineProperty(a,'note',{value:1});a.x===(a.x=1e-323,5e-324)");
    boolean("let a=[];a.x=5e-324;a.x===(a.x=1e-323,5e-324)");
    boolean("let a=[];a[1000000000]=5e-324;a[1000000000]===(a[1000000000]=1e-323,5e-324)");
    boolean("let a=[1,2],n=0,sum=0;for(let v of a){sum+=v;n++;if(n===1){a[1000000000]=3;a[1]=4;a.length=2}}sum===5&&n===2");
    boolean("let a=[1,2,3],sum=0;for(let v of a){sum+=v;a.length=1}sum===1");
    boolean("function f(){let a=[5e-324];for(let v of a){a[0]=1e-323;return v===5e-324}}f()");
    boolean("function f(){let a=[5e-324,1e-323],out=[];for(let v of a){out.push(()=>v);a[0]=2e-323}return out[0]()===5e-324&&out[1]()===1e-323}f()");
    boolean("function f(){let o={x:5e-324};for(let v of Object.values(o)){o.x=1e-323;return v===5e-324}}f()");
}

TEST_F(JsMvpLmd, PropertyExpansionArrayCoercion) {
    boolean("let n=0;let k={valueOf(){n++;throw 9}};[].indexOf(1,k)===-1&&!([].includes(1,k))&&n===0");
    boolean("[].indexOf(1,1n)===-1&&![].includes(1,1n)");
    boolean("let a=['a','b','c'];a.join({toString(){a.length=1;return ','}})==='a,,'");
    boolean("let a=['a','b','c'];a.join({toString(){a[1000000000]='x';a[0]='z';a.length=1;return ','}})==='z,,'");
    boolean("let a=[1,2,3];let b=a.slice({valueOf(){a.length=1;return 0}});b.length===3&&b[0]===1&&!(1 in b)&&!(2 in b)");
    boolean("let a=[[1]];let b=a.flat({valueOf(){a.push([2]);return 1}});b.length===1&&b[0]===1");
    boolean("let a=[1,2,3];a.indexOf(7,{valueOf(){a[1000000000]=9;a[1]=7;a.length=3;return 0}})===1");
    boolean("let a=[1,2,3];a.includes(7,{valueOf(){a[1000000000]=9;a[1]=7;a.length=3;return 0}})");
    boolean("let a=[1,2,3];a.fill(7,{valueOf(){a[1000000000]=9;a.length=0;return 1}});a.length===3&&a[0]===undefined&&a[1]===7&&a[2]===7");
    boolean("let a=[{toString(){a.length=1;return 'a'}},'b','c'];a.join(',')==='a,,'");
    boolean("let a=[1];a.push(a);a.join(',')==='1,'");
    boolean("let seen=0;let a=[1];try{a.join({toString(){throw 9}})}catch(e){seen=e}seen===9");
    boolean("let trace='';let p=new Proxy([1,2],{get(t,k,r){trace+='g'+k+',';return Reflect.get(t,k,r)},has(t,k){trace+='h'+k+',';return Reflect.has(t,k)}});Array.prototype.reverse.call(p);trace==='glength,h0,g0,h1,g1,'");
    boolean("let trace='';let p=new Proxy([1,2],{get(t,k,r){trace+='g'+k+',';return Reflect.get(t,k,r)},has(t,k){trace+='h'+k+',';return Reflect.has(t,k)}});let first=Array.prototype.shift.call(p);first===1&&trace==='glength,g0,h1,g1,'");
}

TEST_F(JsMvpLmd, PropertyExpansionBoundedStorage) {
    boolean("function f(){let a=new Array(32);for(let i=0;i<32;i++)a[i]=i;let sum=0;for(let v of a)sum+=v;return sum===496}f()");
    char* mir = dump("temp/mvp_lmd_bounded_storage.mir");
    ASSERT_NE(mir, nullptr);
    EXPECT_EQ(strstr(mir, "\timport\tmvp_lmd_array_own_item"), nullptr);
    mem_free(mir);
    boolean("function f(){let a=new Array(4);for(let i=0;i<4;i++)a[Math.floor(i/2)]=i;return a[0]===1&&a[1]===3}f()");
    mir = dump("temp/mvp_lmd_floor_storage.mir");
    ASSERT_NE(mir, nullptr);
    EXPECT_EQ(strstr(mir, "\timport\tmvp_lmd_array_own_item"), nullptr);
    mem_free(mir);
    boolean("Math.floor=()=>1000000000;let a=[];a[Math.floor(1/2)]=7;a[1000000000]===7&&a[0]===undefined");
    boolean("function f(x){return Math.floor(x/2)}1/f(-0)===-Infinity&&f(-1)===-1&&f(1)===0&&Number.isNaN(f(NaN))");
    boolean("let A=Array;let a=new A(1000000000);a[999999999]=7;a[0]===undefined&&a[999999999]===7");
    boolean("let a=[];let b=a;b[1000000000]=5e-324;a[0]===undefined&&a[1000000000]===5e-324");
    boolean("let a=new Array(20000);let b=a.map(x=>x);b.length===20000&&b[19999]===undefined");
    boolean("let a=[];Object.assign(a,{'1000000000':7});a.length===1000000001&&a[1000000000]===7");
    boolean("let a=[];({x:a[1000000000]}={x:7});a[1000000000]===7&&a[0]===undefined");
    boolean("let a=[];for(a[1000000000] of [7]){}a[1000000000]===7&&a[0]===undefined");
    boolean("let a=[];a['1000000000']=7;a[1000000000]===7&&a[0]===undefined");
    boolean("let a=[];a.length=1000000000;a.length=2;let n=0;for(let v of a)n+=v===undefined;n===2");
    boolean("function f(){let a=[];function shrink(){a.length=0;return 1}for(let i=0;i<15002;i++)a[i]=shrink();return a[0]===undefined&&a[15001]===1}f()");
}

TEST_F(JsMvpLmd, PropertyExpansionNativeStackBudget) {
    Item proxy = run("new Proxy(function(){return 1},{})");
    ASSERT_EQ(get_type_id(proxy), LMD_TYPE_MAP);
    Item key = {.item = s2it(heap_create_name("x", 1))};
    uintptr_t limit = context->stack_limit;
    context->stack_limit = UINTPTR_MAX;
    Item result = mvp_lmd_proxy_property(proxy, key, ItemNull, proxy, LMD_PROXY_GET);
    context->stack_limit = limit;
    EXPECT_TRUE(item_is_error(result));
    context->stack_limit = UINTPTR_MAX;
    result = mvp_lmd_proxy_invoke(proxy, ItemNull, NULL, 0, Item{.item = ITEM_JS_UNDEFINED});
    context->stack_limit = limit;
    EXPECT_TRUE(item_is_error(result));
    context->stack_limit = UINTPTR_MAX;
    result = mvp_lmd_set_completion(proxy, key, ItemNull, proxy);
    context->stack_limit = limit;
    EXPECT_TRUE(item_is_error(result));
}

TEST_F(JsMvpLmd, PropertyExpansionDenseLoopGuards) {
    boolean("function f(a,b,n){let i=0,total=0;while(i<n){total++;if(a[i]===b[i])total+=10;i++}return total}let a=[1,2],b=[1,4];a[20000]=3;b[20000]=3;f(a,b,3)===23&&f([1,2],[1,2],2)===22");
    char* guarded = dump("temp/mvp_dense_loop.mir");
    ASSERT_NE(guarded, nullptr);
    const char* function = strstr(guarded, "mvp_lmd_f1:\tfunc");
    ASSERT_NE(function, nullptr);
    int storage_guards = 0;
    // two entry guards protect the dense body; two per-read guards remain in its generic replay.
    for (const char* at = function; (at = strstr(at, "i8:(")); at += 4) storage_guards++;
    EXPECT_EQ(storage_guards, 4);
    mem_free(guarded);
    boolean("let hits=0;let a=[1,2];Object.defineProperty(a,'1',{get(){hits++;a[20000]=7;return 3}});function f(a,b){let i=0,n=0;while(i<2){n++;if(a[i]===b[i])n+=10;i++}return n}f(a,[1,3])===22&&hits===1&&a[20000]===7");
    boolean("let hits=0;let a=[1,2];Object.defineProperty(a,'1',{get(){hits++;return 3}});function f(a,b){let i=-1,n=0;while(++i<2&&a[i]===b[i]){n++}return n+i}f(a,[1,3])===4&&hits===1");
    boolean("let sparse=[];sparse[20000]=1;function f(a,b){let i=0,n=0;while(i<3){n++;if(a[i]===b[i])n+=10;i++}return n}f([1],[1])===33");
    boolean("let hits=0;let a=[1];Object.defineProperty(a,'-1',{get(){hits++;a[20000]=7;return 9}});function f(a,b){let i=-1,n=0;while(i<1){n++;if(a[i]===b[i])n+=10;i++}return n}f(a,[1])===12&&hits===1");
    boolean("let hits=0;let a=[];Object.defineProperty(a,'NaN',{get(){hits++;return 9}});function f(a,b){let i=0,n=0;while(i<2){n++;if(a[NaN]===b[NaN])n+=10;i++}return n}f(a,[])===2&&hits===2");
    boolean("let hits=0;let o={0:1,1:2,get length(){hits++;return 2}};function f(a,b){let i=0,n=0;while(i<a.length){if(a[i]===b[i])n++;i++}return n}f([1,2],[1,2])===2&&f(o,[1,2])===2&&hits===3");
    boolean("let hits=0;let p=new Proxy([1,2],{get(t,k,r){hits++;return Reflect.get(t,k,r)}});function f(a,b){let i=0,n=0;while(i<2){if(a[i]===b[i])n++;i++}return n}f([1,2],[1,2])===2&&f(p,[1,2])===2&&hits===2");
    boolean("function match(a,b,n){let total=0;for(let i=0;i<n;i++){if(a[i]===b[i])total++}return total}let a=[1,2],b=[1,4];a[20000]=3;b[20000]=3;match(a,b,20001)===20000&&match([1,2],[1,2],2)===2");
    boolean("let sparse=[];sparse[20000]=1;function f(){for(let i=0;i<0;i++){if(a[i]===a[i])return 1}let a=[];return 7}f()===7");
    boolean("let sparse=[];sparse[20000]=1;function f(a){let n=0;for(let i=0;i<2;i++){let b=a;if(b[i]===a[i])n++}return n}f([1,2])===2");
    boolean("let sparse=[];sparse[20000]=1;function f(a,b){let n=0;for(let i=0;i<2;i++){if(a[i]===b[i])n++;a=b}return n}f([9,8],[1,2])===1");
    boolean("let a=[1,2];function change(){a.length=0;a[20000]=7}function f(a){let n=0;for(let i=0;i<2;i++){if(a[i]===a[i])n++;change()}return n}f(a)===2&&a[20000]===7");
    boolean("let a=[{valueOf(){a.length=0;a[20000]=7;return 3}},2];function f(a){let n=0;for(let i=0;i<1;i++){n+=+a[i];if(a[i]===undefined)n+=10}return n}f(a)===13&&a[20000]===7");
    boolean("let sparse=[];sparse[20000]=1;function f(a,b){let n=0;outer:for(let i=0;i<3;i++){for(let j=0;j<3;j++){if(a[i]===b[j]){n++;continue outer}if(j===2)break outer}}return n}f([2,1,9],[1,2,3])===2");
    boolean("let sparse=[];sparse[20000]=1;function f(a,b){let n=0;for(let i=0;i<2;i++){if(a[i]===b[i])n++}return n}let x={};f([x,undefined],[x,undefined])===2&&f([x],[{}])===1");
}

TEST_F(JsMvpLmd, PropertyExpansionStaticKeys) {
    boolean("function read(a){return a.length}let a=[1,2];Object.defineProperty(a,'x',{value:1});read(a)===2");
    char* mir = dump("temp/mvp_static_keys.mir");
    ASSERT_NE(mir, nullptr);
    char* body = strstr(mir, "mvp_lmd_f1:\tfunc");
    ASSERT_NE(body, nullptr);
    char* end = strstr(body, "\tendfunc");
    ASSERT_NE(end, nullptr);
    *end = 0;
    EXPECT_EQ(strstr(body, "mvp_lmd_property_key"), nullptr);
    mem_free(mir);
    boolean("let a=[];Object.defineProperty(a,'é',{value:3});a.é=4;a.é===3&&a.é===4&&Object.keys(a).join(',')==='é'");
    boolean("class A{get size(){return this.length}}class B extends A{read(){return super.size}}let b=new B();b.length=7;b.read()===7");
}

TEST_F(JsMvpLmd, PropertyExpansionInheritedTraversal) {
    boolean("let p={x:7};let a=Object.create(Object.create(p));a.x===7&&('x' in a)&&!('y' in a)&&a.y===undefined");
    boolean("let p={get x(){Object.setPrototypeOf(this,null);return this.n}};let a=Object.create(Object.create(p));a.n=9;a.x===9&&a.x===undefined");
    boolean("let hits=0;let p=new Proxy({x:7},{get(t,k,r){hits++;return r.n},has(t,k){hits++;return k==='x'}});let a=Object.create(Object.create(p));a.n=9;a.x===9&&('x' in a)&&!('y' in a)&&hits===3");
    boolean("let p=[7];Object.defineProperty(p,'1',{get(){return this.n}});let a=Object.create(Object.create(p));a.n=9;a[0]===7&&a[1]===9&&('0' in a)&&!('2' in a)");
    boolean("function f(){}f.x=7;let a=Object.create(Object.create(f));a.x===7&&a.name==='f'&&('x' in a)&&!('z' in a)");
    boolean("class A{}class B extends A{}Object.defineProperty(A.prototype,'x',{get(){return this.n}});let b=new B();b.n=7;b.x===7&&('x' in b)&&!Object.hasOwn(b,'x')");
}

TEST_F(JsMvpLmd, PropertyExpansionGuardedIndexExtent) {
    boolean("function f(){let a=[];for(var i=0;i<3;i++)a[i]=i;for(var i=0;i<2;i++)a[i]=7;return a[0]===7&&a[2]===2}f()");
    char* mir = dump("temp/mvp_guarded_extent.mir");
    ASSERT_NE(mir, nullptr);
    EXPECT_EQ(strstr(mir, "\timport\tmvp_lmd_array_own_item"), nullptr);
    mem_free(mir);
    boolean("function f(){let a=[];for(var i=0;i<3;i++){i=20000;a[i]=7}return a[0]===undefined&&a[20000]===7}f()");
    boolean("function f(){let a=[];function change(){i=20000}for(var i=0;i<3;i++){change();a[i]=7}return a[0]===undefined&&a[20000]===7}f()");
    boolean("function f(){let a=[];for(var i=0;i<3;i++){for(i of [20000])a[i]=7}return a[0]===undefined&&a[20000]===7}f()");
    boolean("function f(){let a=[];for(var i=0;i<3;i++){({i}={i:20000});a[i]=7}return a[0]===undefined&&a[20000]===7}f()");
    boolean("function f(){let a=[];for(var i=-1;i<2;i++)a[i]=7;for(var i=0.5;i<1;i++)a[i]=9;return a[-1]===7&&a[0.5]===9&&a[0]===7&&a.length===2}f()");
    boolean("function f(){let a=[];for(var i=0;i<(i=20000,20001);i++){a[i]=7;break}return a[0]===undefined&&a[20000]===7}f()");
    boolean("function f(){let a=[];var i=20000;do{a[i]=7}while(i++<2);return a[0]===undefined&&a[20000]===7}f()");
    boolean("function f(){let a=[],i=1;while(i<=3){a[i-1]=7;i+=1}return a.length===3&&a[2]===7}f()");
    boolean("function f(){let a=[],i=0;while(i<3){for(let j=0;j<2;j++){a[i]=7;i=20000}}return a[0]===7&&a[20000]===7}f()");
}

TEST_F(JsMvpLmd, PropertyExpansionCoercionEffects) {
    boolean("function f(x){return +x}function C(){}C.prototype.x=1;f('7')===7&&Number.isNaN(f({}))");
    char* mir = dump("temp/mvp_fixed_coercion.mir");
    ASSERT_NE(mir, nullptr);
    EXPECT_NE(strstr(mir, "\timport\tmvp_lmd_primitive_to_number_fixed"), nullptr);
    mem_free(mir);
    boolean("let n=0;let o={valueOf(){let a=new Array(100);n++;return 7}};function f(x){return +x}f(o)===7&&n===1");
    mir = dump("temp/mvp_reentrant_coercion.mir");
    ASSERT_NE(mir, nullptr);
    EXPECT_EQ(strstr(mir, "\timport\tmvp_lmd_primitive_to_number_fixed"), nullptr);
    mem_free(mir);
    boolean("let n=0;class C{valueOf(){n++;return 8}}function f(x){return +x}f(new C())===8&&n===1");
    boolean("let n=0;class C{valueOf=()=>{n++;return 9}}function f(x){return +x}f(new C())===9&&n===1");
    boolean("let n=0;let o={get valueOf(){n++;return ()=>7}};function f(x){return +x}f(o)===7&&n===1");
    boolean("let n=0;let o={[Symbol.toPrimitive](){n++;return 7}};function f(x){return +x}f(o)===7&&n===1");
    boolean("let n=0;let a=[];a.join=()=>{n++;return '7'};function f(x){return +x}f(a)===7&&n===1");
    boolean("let n=0;let o=new Proxy({valueOf(){n++;return 7}},{get(t,k,r){return Reflect.get(t,k,r)}});function f(x){return +x}f(o)===7&&n===1");
}

TEST_F(JsMvpLmd, PropertyExpansionSetTraversal) {
    boolean("let p={x:1};let a=Object.create(Object.create(p));a.x=7;a.y=9;a.x===7&&a.y===9&&p.x===1&&Object.hasOwn(a,'x')");
    boolean("let n=0;let p={set x(v){n++;Object.setPrototypeOf(this,null);this.y=v}};let a=Object.create(Object.create(p));a.x=7;a.x=9;n===1&&a.y===7&&a.x===9");
    boolean("let seen;let p=new Proxy({},{set(t,k,v,r){seen=r;return Reflect.defineProperty(r,k,{value:v,writable:true,enumerable:true,configurable:true})}});let a=Object.create(Object.create(p));a.x=7;seen===a&&a.x===7");
    boolean("let p={};Object.defineProperty(p,'x',{value:1});let a=Object.create(Object.create(p));!Reflect.set(a,'x',7)&&a.x===1&&!Object.hasOwn(a,'x')");
    boolean("let p={x:1};let a=Object.create(Object.create(p));Object.preventExtensions(a);!Reflect.set(a,'x',7)&&!Reflect.set(a,'y',9)&&a.x===1");
    boolean("let n=0;let p=Object.create(Array.prototype);Object.defineProperty(p,'0',{set(v){n+=v}});let a=[];Object.setPrototypeOf(a,p);a.push(7);a.length===1&&!Object.hasOwn(a,'0')&&n===7");
    boolean("let a=[];Object.setPrototypeOf(a,null);for(let i=0;i<11;i++)a[i]=i+0.25;a[-0]===0.25&&a[9]===9.25&&a[10]===10.25&&String(-0)==='0'&&String(9)==='9'&&String(9.5)==='9.5'");
    boolean("let a=[0.25];Object.preventExtensions(a);Reflect.defineProperty(a,'0',{value:1.25})&&Reflect.defineProperty(a,'0',{})&&!Reflect.defineProperty(a,'1',{value:2,writable:true,enumerable:true,configurable:true})&&a[0]===1.25&&a.length===1");
    boolean("let a=[];Object.defineProperty(a,'length',{writable:false});!Reflect.defineProperty(a,'0',{value:0.25,writable:true,enumerable:true,configurable:true})&&a.length===0");
    boolean("let a=[];a[20000]=0.25;Reflect.defineProperty(a,'20000',{value:1.25})&&Reflect.defineProperty(a,'20001',{writable:true,enumerable:true,configurable:true})&&a[20000]===1.25&&Object.hasOwn(a,'20001')&&a[20001]===undefined&&a.length===20002");
    boolean("function f(){}let a=Object.create(f);a.x={n:7};a.x.n===7&&!Object.hasOwn(f,'x')");
    boolean("function f(){}let p={x:1};Reflect.set(p,'x',{n:7},f)&&f.x.n===7&&p.x===1");
    boolean("let hits=0;let t={};let r=new Proxy(t,{getOwnPropertyDescriptor(t,k){hits++;let a=new Array(100);return Reflect.getOwnPropertyDescriptor(t,k)},defineProperty(t,k,d){hits++;return Reflect.defineProperty(t,k,d)}});Reflect.set({x:1},'x',{n:7},r)&&t.x.n===7&&hits===2");
    boolean("let observed;let p={set x(v){let a=new Array(100);observed=v}};let o=Object.create(p);o.x={n:7};observed.n===7&&!Object.hasOwn(o,'x')");
}

TEST_F(JsMvpLmd, PropertyExpansionArrayStorageProof) {
    boolean("let a=[0.25,,{n:7}];Object.defineProperty(a,'note',{get(){throw 9}});a.unshift(1.25);a.length===4&&a[0]===1.25&&a[1]===0.25&&!Object.hasOwn(a,'2')&&a[3].n===7");
    boolean("let a=[0.25,1.25,2.25];Object.setPrototypeOf(a,null);let r=Array.prototype.splice.call(a,1,1,{n:7},3.25);r[0]===1.25&&a[1].n===7&&a[2]===3.25&&a[3]===2.25");
    boolean("let a=[,1.25,,3.25];Object.defineProperty(a,'x',{value:1});let r=a.shift();r===undefined&&a.length===3&&a[0]===1.25&&!Object.hasOwn(a,'1')&&a[2]===3.25");
    boolean("let a=[,0.25,1.25];Object.defineProperty(a,'x',{value:1});a.reverse();a[0]===1.25&&a[1]===0.25&&!Object.hasOwn(a,'2')&&a.pop()===undefined&&a.length===2");
    boolean("let a=[1,2];Object.preventExtensions(a);let threw=false;try{a.unshift(9)}catch(e){threw=true}threw&&a.length===2&&a[0]===1&&a[1]===2");
    boolean("let a=[1,2];Object.defineProperty(a,'length',{writable:false});let threw=false;try{a.push(9)}catch(e){threw=true}threw&&a.length===2&&!Object.hasOwn(a,'2')");
    boolean("let a=[1,2];Object.defineProperty(a,'0',{writable:false});let threw=false;try{a.reverse()}catch(e){threw=true}threw&&a[0]===1&&a[1]===2");
    boolean("let hits=0;let p=Object.create(Array.prototype);Object.defineProperty(p,'0',{get(){hits++;return 7},configurable:true});let a=[,2];Object.setPrototypeOf(a,p);a.pop()===2&&a.pop()===7&&hits===1&&a.length===0");
    boolean("let seen=0;let p=Object.create(Array.prototype);Object.defineProperty(p,'1',{set(v){seen=v},configurable:true});let a=[7];Object.setPrototypeOf(a,p);a.unshift(9);seen===7&&a[0]===9&&a.length===2&&!Object.hasOwn(a,'1')");
    boolean("let a=[1,2,3];let start={valueOf(){Object.defineProperty(a,'1',{get(){return 7},set(v){Object.defineProperty(a,'1',{value:v,writable:true,configurable:true})},configurable:true});return 1}};let r=a.splice(start,1);r[0]===7&&a.length===2&&a[1]===3");
    boolean("let p=Object.create(Array.prototype);p[0]=7;let a=new Array(2);let start={valueOf(){Object.setPrototypeOf(a,p);return 0}};let r=a.slice(start);r[0]===7&&!Object.hasOwn(r,'1')");
    boolean("let p=Object.create(Array.prototype);p[0]=7;let a=new Array(1);Object.defineProperty(a,'constructor',{get(){Object.setPrototypeOf(a,p);return Array}});let r=a.slice();r.length===1&&r[0]===7");
    boolean("let hits=0;let p=new Proxy(Array.prototype,{get(t,k,r){if(k==='0'){hits++;return 7}return Reflect.get(t,k,r)},has(t,k){return k==='0'||Reflect.has(t,k)}});let a=new Array(1);Object.setPrototypeOf(a,p);let r=a.concat([]);r[0]===7&&hits===1");
    boolean("let p=[7];Object.setPrototypeOf(p,Array.prototype);let a=new Array(1);Object.setPrototypeOf(a,p);let r=a.concat([]);r[0]===7&&Object.hasOwn(r,'0')");
    boolean("let a=[0.25];a[20000]=1.25;let v=a.shift();v===0.25&&a.length===20000&&a[19999]===1.25&&!Object.hasOwn(a,'0')");
    boolean("let a=[1,2,3];let start={valueOf(){a.length=1;return 0}};let r=a.splice(start,2,7);r.length===2&&r[0]===1&&!Object.hasOwn(r,'1')&&a.length===2&&a[0]===7&&!Object.hasOwn(a,'1')");
}

TEST_F(JsMvpLmd, PropertyExpansionFixedPropertyDispatch) {
    boolean("class A{constructor(n){this.n=n}read(){return this.n}}class B extends A{}function f(a){a.n+=1;return a.read()}let a=new B(6);f(a)===7&&f(a)===8");
    char* mir = dump("temp/mvp_fixed_property.mir");
    ASSERT_NE(mir, nullptr);
    EXPECT_NE(strstr(mir, "\timport\tmvp_lmd_class_property_fixed"), nullptr);
    mem_free(mir);
    boolean("class A{constructor(){this.m=new Map()}read(){return this.m.size}}let a=new A();a.m.set('x',0.25);a.read()===1&&a.m.get('x')===0.25");
    boolean("function C(){this.x=7}C.prototype.read=function(){return this.x};let c=new C();c.read()===7&&c.x===7&&C.length===0");
    boolean("class A{constructor(){this.n=1}read(){return this.n}}let a=new A();Object.defineProperty(A.prototype,'read',{get(){return function(){return this.n+6}}});a.read()===7");
    mir = dump("temp/mvp_mutable_property.mir");
    ASSERT_NE(mir, nullptr);
    EXPECT_EQ(strstr(mir, "\timport\tmvp_lmd_class_property_fixed"), nullptr);
    mem_free(mir);
}

TEST_F(JsMvpLmd, PropertyExpansionLocalDenseStorage) {
    boolean("let sparse=[];sparse[20000]=7;function f(){var a=Array(80);for(var i=0;i<80;i++)a[i]=i;let total=0;for(var j=0;j<80;j++)total+=a[j];return total}f()===3160&&sparse[20000]===7");
    char* mir = dump("temp/mvp_local_dense_storage.mir");
    ASSERT_NE(mir, nullptr);
    const char* body = strstr(mir, "mvp_lmd_f1:\tfunc");
    ASSERT_NE(body, nullptr);
    const char* end = strstr(body, "\tendfunc");
    ASSERT_NE(end, nullptr);
    const char* sparse_guard = strstr(body, "i8:(");
    EXPECT_TRUE(!sparse_guard || sparse_guard >= end);
    mem_free(mir);
    boolean("let sparse=[];sparse[20000]=7;function f(){let a=new Array(3);a[0]=0.25;a[2]={n:7};delete a[0];return a[0]===undefined&&a[1]===undefined&&a[2].n===7}f()");
    boolean("let sparse=[];sparse[20000]=7;function grow(a){a[20000]=9}function f(){let a=[1,2];grow(a);return a[0]===1&&a[20000]===9}f()");
    boolean("let sparse=[];sparse[20000]=7;function f(){let a=[1,2];let b=a;b.length=20001;b[20000]=9;return a[0]===1&&a[20000]===9}f()");
    boolean("let sparse=[];sparse[20000]=7;function f(){let a=[1,2];function grow(){a[20000]=9}grow();return a[0]===1&&a[20000]===9}f()");
    boolean("let sparse=[];sparse[20000]=7;function f(){let a=[1,2];({x:a[20000]}={x:9});return a[0]===1&&a[20000]===9}f()");
    boolean("let sparse=[];sparse[20000]=7;function f(){let a=[1,2];for(a[20000] of [9]){}return a[0]===1&&a[20000]===9}f()");
    boolean("let sparse=[];sparse[20000]=7;function f(){let a=[1,2];a.length=20001;a[20000]=9;return a[0]===1&&a[20000]===9}f()");
    boolean("let sparse=[];sparse[20000]=7;function f(n){let a=Array(n);a[n-1]=9;return a[n-1]===9&&a[0]===undefined}f(20001)");
    boolean("let sparse=[];sparse[20000]=7;function f(){let a=[1,2];for(var i=0;i<3;i++){i=20000;a[i]=9}return a[0]===1&&a[20000]===9}f()");
    boolean("let sparse=[];sparse[20000]=7;function f(){let a=[1,2];a[-1]=7;a[0.5]=8;return a[-1]===7&&a[0.5]===8&&a.length===2}f()");
}

TEST_F(JsMvpLmd, PropertyExpansionArrayCreation) {
    boolean("let hits=0;Object.defineProperty(Array.prototype,'0',{set(v){hits++},configurable:true});let a=Array(7,8);hits===0&&Object.hasOwn(a,'0')&&a[0]===7&&a[1]===8");
    boolean("let hits=0;Object.defineProperty(Array.prototype,'0',{set(v){hits++},configurable:true});let a=new Array({n:7});hits===0&&Object.hasOwn(a,'0')&&a[0].n===7");
    boolean("let hits=0;Object.defineProperty(Array.prototype,'0',{set(v){hits++},configurable:true});function f(...a){return a}let a=f(0.25,{n:7});hits===0&&Object.hasOwn(a,'0')&&a[0]===0.25&&a[1].n===7");
    boolean("Object.defineProperty(Array.prototype,'0',{value:9,configurable:true});let a=Array(7,8);let b=[0.25];a[0]===7&&b[0]===0.25&&Object.hasOwn(a,'0')&&Object.hasOwn(b,'0')");
    boolean("let hits=0;Object.defineProperty(Array.prototype,'0',{set(v){hits++},configurable:true});let a=[7,8].map(x=>x+1);let b=[0.25,1.25].filter(x=>x>0);hits===0&&a[0]===8&&b[0]===0.25&&Object.hasOwn(a,'0')&&Object.hasOwn(b,'0')");
}

TEST_F(JsMvpLmd, PropertyExpansionLocalArrayMethods) {
    boolean("let sparse=[];sparse[20000]=7;function f(){let a=['a'];a.push('b','c');let tail=a.pop();return a.join('-')+tail}f()==='a-bc'");
    char* mir = dump("temp/mvp_local_array_methods.mir");
    ASSERT_NE(mir, nullptr);
    const char* body = strstr(mir, "mvp_lmd_f1:\tfunc");
    ASSERT_NE(body, nullptr);
    const char* end = strstr(body, "\tendfunc");
    ASSERT_NE(end, nullptr);
    const char* sparse_guard = strstr(body, "i8:(");
    EXPECT_TRUE(!sparse_guard || sparse_guard >= end);
    mem_free(mir);
    boolean("let sparse=[];sparse[20000]=7;function f(){let a=[0.25];a.push(1.7976931348623157e308);let v=a.pop();a.push(9);return v===1.7976931348623157e308&&a[0]===0.25&&a.join(',')==='0.25,9'}f()");
    boolean("function f(){let a=[1,2];let b=a;b[20000]=7;return a.pop()===7&&a.length===20000&&a[0]===1}f()");
    boolean("function f(){let a=['a','b'];let sep={toString(){a[20000]='c';return '-'}};let s=a.join(sep);return a.length===20001&&s==='a-b'}f()");
    boolean("function f(){let a=[{toString(){a.length=20001;a[20000]='c';return 'a'}},'b'];return a.join('-')==='a-b'&&a[20000]==='c'}f()");
    boolean("function f(){let a=[1];a.push=function(x){this[20000]=x;return 7};return a.push(9)===7&&a[20000]===9}f()");
    boolean("let hit=0;Object.defineProperty(Array.prototype,'1',{set(v){hit+=v},configurable:true});function f(){let a=[1];return a.push(7)===2&&a[1]===undefined}f()&&hit===7");
    boolean("function f(){let a=Array(20001);a[20000]='x';a.push('y');return a.pop()==='y'&&a[20000]==='x'&&a.length===20001}f()");
    boolean("function f(){let a=[1,2];delete a[0];a.push(3);return a.join('-')==='-2-3'&&a.pop()===3}f()");
}

TEST_F(JsMvpLmd, PropertyExpansionNativeMapIteration) {
    boolean("function f(){let m=new Map();m.set(1,2);m.set(3,4);let sum=0;for(let [k,v] of m)sum+=k+v;return sum}f()===10");
    boolean("function f(){let m=new Map();m.set('a',0.25);m.set('b',1.7976931348623157e308);let n=0;for(let [k,v] of m){m.set(k,9);if(k==='a'&&v===0.25)n++;if(k==='b'&&v===1.7976931348623157e308)n++}return n}f()===2");
    boolean("function f(){let m=new Map();m.set(1,2);m.set(3,4);let sum=0;for(let [k,v] of m){sum+=k+v;if(k===1){m.delete(3);m.set(5,6)}}return sum}f()===14");
    boolean("function f(){let m=new Map();m.set(1,2);let n=0;for(let [k,v] of m){for(let pair of m){n+=pair[0]+pair[1];break}break}m.clear();m.set(3,4);for(let [k,v] of m)n+=k+v;return n}f()===10");
    boolean("function f(m){let n=0;for(let [k,v] of m)n+=k+v;return n}let m=new Map();m.set(1,2);f(m)===3&&f([[3,4]])===7");
    boolean("let m=new Map();m.set(1,2);m[Symbol.iterator]=function*(){yield [3,4]};let n=0;for(let [k,v] of m)n+=k+v;n===7");
}

TEST_F(JsMvpLmd, PropertyExpansionPassiveOwnQuery) {
    boolean("let own=Object.prototype.hasOwnProperty;let a={x:undefined};own.call(a,'x')&&!own.call(a,'toString')&&!own.call(a,'z')");
    boolean("let hits=0;let a={get x(){hits++;return 7}};Object.prototype.hasOwnProperty.call(a,'x')&&hits===0");
    boolean("class A{get x(){return 7}}let a=new A();a.y=9;Object.prototype.hasOwnProperty.call(a,'y')&&!Object.prototype.hasOwnProperty.call(a,'x')");
    boolean("let a={};let key={toString(){a.x=7;return 'x'}};Object.prototype.hasOwnProperty.call(a,key)&&a.x===7");
    boolean("let hits=0;let p=new Proxy({x:7},{getOwnPropertyDescriptor(t,k){hits++;return Reflect.getOwnPropertyDescriptor(t,k)}});Object.prototype.hasOwnProperty.call(p,'x')&&hits===1");
    boolean("let key='x'+String(7);function f(){}f[key]=9;Object.prototype.hasOwnProperty.call(f,key)&&Object.prototype.hasOwnProperty.call(f,'prototype')");
    boolean("let a={};a['😀']=7;a['é']=9;Object.prototype.hasOwnProperty.call(a,'\\ud83d'+'\\ude00')&&Object.prototype.hasOwnProperty.call(a,'é')&&!Object.prototype.hasOwnProperty.call(a,'é')");
    boolean("let m=new Map();m.set('x',7);m.y=9;Object.prototype.hasOwnProperty.call(m,'y')&&!Object.prototype.hasOwnProperty.call(m,'x')&&!Object.prototype.hasOwnProperty.call(m,'size')");
}

TEST_F(JsMvpLmd, PropertyExpansionForwardedArgumentRoots) {
    boolean("function f(a,b){let garbage=[];for(let i=0;i<2000;i++)garbage.push({i});return a===5e-324&&b===1.7976931348623157e308}let a=[5e-324];let bound=f.bind(null,a[0]);a[0]=7;bound(1.7976931348623157e308)");
    boolean("function f(a,b){return this.n+a+b}let bound=f.bind({n:1},2);bound(4)===7&&bound.call({n:99},5)===8");
    boolean("function f(a,b){let garbage=[];for(let i=0;i<2000;i++)garbage.push({i});return a.n===7&&b===5e-324}f.call(null,{n:7},5e-324)");
    boolean("let log='',a=[0,0];Object.defineProperty(a,'0',{get(){log+='a';a[1]=7;return 5e-324}});function f(x,y){log+='f';return x===5e-324&&y===7}f.apply(null,a)&&log==='af'");
    boolean("let a=[0,0];Object.defineProperty(a,'0',{get(){delete a[1];return 7}});Object.defineProperty(Array.prototype,'1',{get(){return 9},configurable:true});function f(x,y){return x+y}let n=f.apply(null,a);delete Array.prototype[1];n===16");
    boolean("function f(x,y){return x===5e-324&&y===1.7976931348623157e308}let a=[5e-324,1.7976931348623157e308];Object.defineProperty(a,'0',{get(){let garbage=[];for(let i=0;i<2000;i++)garbage.push({i});return 5e-324}});f.apply(null,a)");
    boolean("let a=[1,2];Object.defineProperty(a,'0',{get(){throw 7}});function f(x){return x}let caught=false;try{f.apply(null,a)}catch(e){caught=e===7}caught&&f.call(null,5e-324)===5e-324");
    boolean("function C(x,y){this.x=x;this.y=y}let B=C.bind(null,5e-324);let b=new B(7);b instanceof C&&b.x===5e-324&&b.y===7");
    boolean("let hits=0;let f=new Proxy(function(a,b){return a+b},{apply(t,r,a){hits++;return Reflect.apply(t,r,a)}});let bound=f.bind(null,3);bound(4)===7&&hits===1");
    boolean("function f(a){return a}let bound=f.bind(null);let garbage=[];for(let i=0;i<2000;i++)garbage.push({i});bound(5e-324)===5e-324&&f.apply(null,[])===undefined");
}

TEST_F(JsMvpLmd, PropertyExpansionNonnegativeArrayLanes) {
    boolean("function f(i){const a=[0,63,9007199254740991];return a[i]}f(0)===0&&f(1)===63&&f(2)===9007199254740991&&f(3)===undefined");
    char* mir = dump("temp/mvp_nonnegative_array_lane.mir");
    ASSERT_NE(mir, nullptr);
    const char* body = strstr(mir, "mvp_lmd_f1:\tfunc");
    ASSERT_NE(body, nullptr);
    const char* end = strstr(body, "\tendfunc");
    ASSERT_NE(end, nullptr);
    const char* sign_extend = strstr(body, "\trsh\t");
    EXPECT_TRUE(!sign_extend || sign_extend >= end);
    mem_free(mir);
    boolean("function f(i){const a=[-1,-9007199254740991,63];return a[i]}f(0)===-1&&f(1)===-9007199254740991&&f(2)===63&&f(3)===undefined");
    boolean("function f(i){const a=[0,-0,5e-324];return a[i]}1/f(0)===Infinity&&1/f(1)===-Infinity&&f(2)===5e-324");
    boolean("function f(){let a=[7,9];a[0]=-1;return a[0]}f()===-1");
    boolean("let a=[7,9];function f(i){return a[i]}Object.defineProperty(a,'0',{get(){return -1}});f(0)===-1&&f(1)===9");
    boolean("function f(i){const a=[7,9];return a[i]}f(-1)===undefined&&f(0.5)===undefined&&f(1)===9");
}

TEST_F(JsMvpLmd, PropertyExpansionDenseForReplay) {
    boolean("let sparse=[];sparse[20000]=7;function f(a,b){let total=0;for(let i=0;i<a.length;i++){let j=0;while(j<b.length){if(a[i]===b[j])total++;j++}}return total}f([1,2],[2,1])===2");
    char* mir = dump("temp/mvp_dense_for_replay.mir");
    ASSERT_NE(mir, nullptr);
    const char* body = strstr(mir, "mvp_lmd_f1:\tfunc");
    ASSERT_NE(body, nullptr);
    const char* end = strstr(body, "\tendfunc");
    ASSERT_NE(end, nullptr);
    int guards = 0;
    for (const char* at = body; (at = strstr(at, "i8:(")) && at < end; at += 4) guards++;
    // two outer entry guards; the fallback retains two inner entry and two generic read guards.
    EXPECT_EQ(guards, 6);
    mem_free(mir);
    boolean("let hits=0;let a=[1,2,3];Object.defineProperty(a,'1',{get(){hits++;return 2}});function f(a,b){let n=0;for(let i=0;i<3;i++){n++;if(a[i]===b[i])n+=10}return n}f(a,[1,2,3])===33&&hits===1");
    boolean("let hits=0;let a=[1,2,3];Object.defineProperty(a,'2',{get(){hits++;return 3}});function f(a,b){let n=0;for(let i=1;i<3;i++){n++;if(a[i]===b[i])n+=10}return n}f(a,[1,2,3])===22&&hits===1");
    boolean("let hits=0;let a=[1,,3];Object.defineProperty(a,'1',{get(){hits++;return 2}});function f(a,b){let n=0;for(let i=0;i<3;i++){let j=0;while(j<3){if(a[i]===b[j])n++;j++}}return n}f(a,[1,2,3])===3&&hits===3");
    boolean("let hits=0;let a=[1,2];Object.defineProperty(a,'-1',{get(){hits++;return 7}});function f(a,b){let n=0;for(let i=-1;i<2;i++){n++;if(a[i]===b[i])n+=10}return n}f(a,[1,2])===23&&hits===1");
    boolean("let sparse=[];sparse[20000]=7;function f(a,b){let n=0;for(let i=0;i<3;i++){let local=1;if(a[i]===b[i])local+=10;n+=local}return n}f([1],[1])===33");
    boolean("let sparse=[];sparse[20000]=7;function f(a,b){let n=0;outer:for(let i=0;i<3;i++){for(let j=0;j<3;j++){if(a[i]===b[j]){n++;continue outer}if(j===2)break outer}}return n}f([2,1,9],[1,2,3])===2");
    boolean("let hits=0;let a=[1,2,3];Object.defineProperty(a,'1',{get(){hits++;return 2}});function f(a,b){let n=0;for(let i=0;i<3;i++){if(a[i]===b[i]){n++;continue}n+=10}return n}f(a,[1,2,3])===3&&hits===1");
    boolean("let hits=0;let a=[1,2];Object.defineProperty(a,'1',{get(){hits++;return 2}});function f(a,b){let n=0;for(let i=0;i<2&&a[i]===b[i];i++)n++;return n}f(a,[1,2])===2&&hits===1");
    boolean("let sparse=[];sparse[20000]=7;function f(a,b){let i=0,n=0;for(;i<2;i=a[i]){if(a[i]===b[i])n++;if(n>3)break}return n}f([1,2],[1,2])===2");
    boolean("let sparse=[];sparse[20000]=7;function f(){for(let i=0;i<0;i++){if(a[i]===a[i])return 1}let a=[];return 7}f()===7");
    boolean("let hits=0;let p=new Proxy([1,2],{get(t,k,r){hits++;return Reflect.get(t,k,r)}});function f(a,b){let n=0;for(let i=0;i<2;i++){if(a[i]===b[i])n++}return n}f([1,2],[1,2])===2&&f(p,[1,2])===2&&hits===2");
    boolean("let hits=0;let a=[1,2];Object.defineProperty(a,'1',{get(){hits++;a.length=1;return 2}});function f(a,b){let n=0;for(let i=0;i<a.length;i++){n++;if(a[i]===b[i])n+=10}return n}f(a,[1,2])===22&&hits===1&&a.length===1");
    boolean("let sparse=[];sparse[20000]=7;function f(a,b){let n=0;for(let i=0;i<2;i++){if(a[i]===b[i])n++;a=b}return n}f([9,8],[1,2])===1");
}

TEST_F(JsMvpLmd, PropertyExpansionLiteralStorage) {
    boolean("let hits=0;Object.defineProperty(Object.prototype,'x',{set(v){hits++},configurable:true});function f(v){return {x:v,y:3}}let a=f(0.25),b=f({n:7});hits===0&&a.x===0.25&&b.x.n===7&&a.y===3&&Object.hasOwn(a,'x')");
    boolean("Object.defineProperty(Object.prototype,'x',{value:9,configurable:true});function f(v){return {x:v}}let a=f(0.25);a.x===0.25&&Object.getOwnPropertyDescriptor(a,'x').writable");
    boolean("let hits=0;function f(v){return {get x(){hits++;return 9},x:v}}let a=f(0.25);let d=Object.getOwnPropertyDescriptor(a,'x');a.x===0.25&&hits===0&&d.writable&&d.enumerable&&d.configurable&&d.get===undefined");
    boolean("let hits=0;function f(v){return {set x(n){hits++},x:v}}let a=f({n:7});a.x.n===7&&hits===0&&Object.getOwnPropertyDescriptor(a,'x').set===undefined");
    boolean("function f(v){return {x:1,get x(){return 2},x:v}}let a=f(0.25);a.x===0.25&&Object.keys(a).join(',')==='x'");
    boolean("let hits=0;let proto={set x(v){hits++}};function f(v){return {__proto__:proto,x:v}}let a=f(0.25);hits===0&&a.x===0.25&&Object.getPrototypeOf(a)===proto");
    boolean("let key='__proto__';function f(v){return {[key]:v}}let a=f(0.25);Object.hasOwn(a,key)&&a[key]===0.25&&Object.getPrototypeOf(a)===Object.prototype");
    boolean("let s=Symbol('x');function f(v){return {[s]:1,[s]:v}}let a=f({n:7});Reflect.ownKeys(a)[0]===s&&a[s].n===7");
    boolean("let log='';let source={get x(){log+='g';return 2}};function f(v){return {get x(){return 1},...source,x:v}}let a=f(0.25);log==='g'&&a.x===0.25&&Object.getOwnPropertyDescriptor(a,'x').writable");
    boolean("let log='';let key={toString(){log+='k';return 'x'}};function value(){log+='v';return 0.25}let a={[key]:value(),y:(log+='y',7)};log==='kvy'&&a.x===0.25&&a.y===7");
    boolean("let __proto__={n:7};let a={__proto__};Object.hasOwn(a,'__proto__')&&a.__proto__.n===7&&Object.getPrototypeOf(a)===Object.prototype");
    boolean("let proto={x:1};let a={__proto__:proto,m(){return super.x},x:0.25};a.m()===1&&a.x===0.25");
}

TEST_F(JsMvpLmd, PropertyExpansionLiteralHome) {
    boolean("function make(p){return {__proto__:p,m(){return super.x}}}let a=make({x:1}),b=make({x:2});a.m()===1&&b.m()===2");
    boolean("let a={__proto__:{x:1},m(){return super.x}};let f=a.m;Object.setPrototypeOf(a,{x:3});f.call({x:9})===3");
    boolean("let proto={get x(){return this.n},set x(v){this.n=v}};let a={__proto__:proto,n:2,get x(){return super.x},set x(v){super.x=v}};a.x=7;a.x===7&&a.n===7&&!Object.hasOwn(proto,'n')");
    boolean("let p={f(v){return this.n+v}};let a={__proto__:p,n:3,m(){return super.f(4)}};a.m()===7&&a.m.call({n:8})===12");
    boolean("let a={__proto__:{x:2},m(){return ()=>super.x}};let f=a.m();Object.setPrototypeOf(a,{x:7});f()===7");
    boolean("let a={__proto__:{f(){return this.n}},n:3,m(){return ()=>()=>super.f()}};let f=a.m()();a.n=7;f.call({n:99})===7");
    boolean("let a={__proto__:{get x(){return this.n},set x(v){this.n=v}},n:2,m(){return v=>super['x']=v},g(){return ()=>super['x']}};let f=a.m(),g=a.g();f(7);g()===7&&a.n===7");
    boolean("class A{f(){return this.n}}class B extends A{m(){return ()=>super.f()}}let b=new B();b.n=7;let f=b.m();f.call({n:99})===7");
    boolean("class A{static f(){return this.n}}class B extends A{static m(){return ()=>super.f()}}B.n=7;let f=B.m();f()===7");
    boolean("function make(n){let value=n;return {__proto__:{x:2},m(){return ()=>super.x+value}}}let a=make(5);let f=a.m();a=null;let garbage=[];for(let i=0;i<2000;i++)garbage.push({i});f()===7");
    boolean("let p={f(a,b){return this.n+a+b}};let a={__proto__:p,n:1,m(){return super.f(...[2,4])}};a.m()===7");
    boolean("let a={__proto__:{x:7},m(){return super.x}};a.m.extra=1;Object.getOwnPropertyDescriptors(a.m);a.m()===7");
    error("let a={__proto__:null,m(){return super.x}};a.m()", "TypeError");
    boolean("class A{constructor(v){this.x=v}}class B extends A{y=2;constructor(){let init=()=>super(5);init();this.z=this.x+this.y}}let b=new B();b.x===5&&b.y===2&&b.z===7");
    boolean("class A{constructor(v){this.x=v}}class B extends A{constructor(){let init=()=>()=>super(7);init()()}}new B().x===7");
    error("class A{}class B extends A{constructor(){let f=()=>super.x;f();super()}}new B()", "ReferenceError");
    error("class A{}class B extends A{constructor(){let f=()=>super();f();f()}}new B()", "ReferenceError");
    boolean("class A{constructor(a,b){this.x=a+b}}class B extends A{constructor(){super(...[2,5])}}new B().x===7");
    boolean("class A{constructor(a,b){this.x=a+b}}class B extends A{y=3;constructor(){let init=()=>super(...[2,5]);init();this.z=this.x+this.y}}let b=new B();b.x===7&&b.z===10");
    boolean("let log='';class A{constructor(v){log+='a';this.x=v}}class B extends A{constructor(){let init=()=>super(...(log+='v',[7]));init()}}let b=new B();log==='va'&&b.x===7");
}

TEST_F(JsMvpLmd, PropertyExpansionCapturedScalarKinds) {
    boolean("function f(){return (step|0)+1}let before=f();var step=8;before===1&&f()===9");
    boolean("function f(){return step+1}let before=f();var step=8;Number.isNaN(before)&&f()===9");
    boolean("function f(){return typeof step}let before=f();var step=8;before==='undefined'&&f()==='number'");
    boolean("function f(){return step===undefined}let before=f();var step=8;before&&!f()");
    boolean("function make(){var step=2;return {get(){return step+1},set(v){step=v}}}let o=make();let a=o.get();o.set(6);a===3&&o.get()===7");
    boolean("var step=2;function f(){return step+1}let a=f();step={valueOf(){return 6}};a===3&&f()===7");
    boolean("var step=2;function f(){return step+1}let a=f();step='6';a===3&&f()==='61'");
    boolean("var step=2;function f(){return step+1}globalThis.step=6;f()===7");
    boolean("function f(){return !!enabled}let before=f();var enabled=true;!before&&f()");
    boolean("function f(){return text+'!'}let before=f();var text='ok';before==='undefined!'&&f()==='ok!'");
    boolean("function f(){return value===null}let before=f();var value=null;!before&&f()");
    boolean("function f(){return 1/value}var value=-0;f()===-Infinity");
    boolean("var value=5e-324;function f(){let x=value;value=1e-323;return x}let x=f();x===5e-324&&value===1e-323");
    boolean("function make(){let value=5e-324;return ()=>value}let f=make();let garbage=[];for(let i=0;i<2000;i++)garbage.push({i});f()===5e-324");
    error("function f(){return value+1}f();let value=2", "ReferenceError");
    error("function f(){return value|0}f();const value=2", "ReferenceError");
    boolean("var step=8;function f(){let total=0;for(let i=0;i<32;i++)total+=(i%step);return total}f()===112");
    char* mir = dump("temp/mvp_lmd_captured_scalar_kinds.mir");
    ASSERT_NE(mir, nullptr);
    EXPECT_EQ(strstr(mir, "\timport\tmvp_lmd_string_to_number\n"), nullptr);
    mem_free(mir);
}

TEST_F(JsMvpLmd, PropertyExpansionPrototypeTraversalBorrow) {
    boolean("function keys(o){let a=[];for(let k in o)a.push(k);return a.join(',')}let p={},o=Object.create(p);o.a=1;let a=keys(o);p.b=2;let b=keys(o);Object.setPrototypeOf(p,{c:3});a==='a'&&b==='a,b'&&keys(o)==='a,b,c'");
    boolean("let p={x:7},o=Object.create(p);function read(){return o.x}let a=read();Object.setPrototypeOf(o,{x:9});a===7&&read()===9");
    boolean("let hits=0;let p=new Proxy({x:7},{get(t,k,r){hits++;return Reflect.get(t,k,r)}});let o=Object.create(p);o.x===7&&hits===1");
    boolean("let hits=0;let p=new Proxy({x:7},{ownKeys(t){hits++;return Reflect.ownKeys(t)}});let o=Object.create(p);let keys=[];for(let k in o)keys.push(k);keys.join(',')==='x'&&hits===1");
    boolean("let n=0;let p={get x(){n++;Object.setPrototypeOf(this,{x:9});return 7}};let o=Object.create(p);let a=o.x;a===7&&o.x===9&&n===1");
    boolean("let a=[];let p={x:7};Object.setPrototypeOf(a,p);let keys=[];for(let k in a)keys.push(k);a.x===7&&keys.join(',')==='x'");
    boolean("let p={x:7},o=Object.create(p);Object.defineProperty(o,'x',{value:9,enumerable:false});let keys=[];for(let k in o)keys.push(k);keys.length===0&&o.x===9");
    boolean("let p={x:7},o=Object.create(p);Object.setPrototypeOf(p,null);let first=o.x;delete p.x;first===7&&o.x===undefined");
}

TEST_F(JsMvpLmd, PropertyExpansionReceiverInitialization) {
    boolean("class A{m(){return this}}let a=new A();a.m()===a&&a.m.call(null)===null&&a.m.call(undefined)===undefined");
    boolean("function f(){'use strict';return this}f()===undefined&&f.call(null)===null");
    boolean("let a={__proto__:{x:7},m(){return super.x}};a.m.call(undefined)===7");
    boolean("class A{}class B extends A{constructor(){let target=new.target;super();this.target=target}}let b=new B();b.target===B");
    boolean("class A{}class B extends A{constructor(){let f=()=>new.target;let target=f();super();this.target=target}}new B().target===B");
    boolean("class A{}class B extends A{constructor(){let f=()=>this;super();this.same=f()===this}}new B().same");
    boolean("class A{m(){return ()=>this}}let a=new A();let f=a.m();f.call({})===a");
    error("class A{}class B extends A{constructor(){this.x=1;super()}}new B()", "ReferenceError");
    error("class A{}class B extends A{constructor(){let f=()=>this;f();super()}}new B()", "ReferenceError");
    error("class A{}class B extends A{constructor(){let f=()=>super.x;f();super()}}new B()", "ReferenceError");
    boolean("class A{m(){return this}}let a=new A();a.m()===a");
    char* mir = dump("temp/mvp_lmd_receiver_initialization.mir");
    ASSERT_NE(mir, nullptr);
    char* method = strstr(mir, "mvp_lmd_f1:\tfunc");
    ASSERT_NE(method, nullptr);
    char* end = strstr(method, "\tendfunc");
    ASSERT_NE(end, nullptr);
    *end = '\0';
    EXPECT_EQ(strstr(method, "mvp_lmd_fail"), nullptr);
    mem_free(mir);
}

TEST_F(JsMvpLmd, PropertyExpansionPrivatePrototypeKeys) {
    boolean("function make(){return {x:1}}let a=make(),b=make(),c=make();Object.setPrototypeOf(b,{p:7});b.y=2;Object.defineProperty(b,'x',{writable:false});a.x===1&&a.p===undefined&&b.p===7&&c.p===undefined&&Object.getPrototypeOf(c)===Object.prototype");
    boolean("let p={v:7},o=Object.create(p);o.x=1;o.x='x';o.x={};delete o.x;Object.defineProperty(o,'x',{get(){return this.v}});o.x===7&&Object.getPrototypeOf(o)===p");
    boolean("let o={};o['MVP [[Prototype]]']=7;o[Symbol('MVP [[Prototype]]')]=9;Object.getPrototypeOf(o)===Object.prototype&&Reflect.ownKeys(o).length===2");
    boolean("let p={v:7},o=Object.create(p);o['MVP [[Prototype]]']=3;delete o['MVP [[Prototype]]'];Object.getPrototypeOf(o)===p&&o.v===7&&Reflect.ownKeys(o).length===0");
    boolean("let p={v:7},o=Object.create(p);Object.freeze(o);Object.getPrototypeOf(o)===p&&o.v===7&&!Reflect.setPrototypeOf(o,{})&&Reflect.setPrototypeOf(o,p)");
    boolean("let p={v:7},a=[1,2];Object.setPrototypeOf(a,p);a[100000]=3;Object.defineProperty(a,'x',{get(){return this.v}});Object.getPrototypeOf(a)===p&&a.x===7&&a[100000]===3");
    boolean("class A{x=1}class B extends A{y=2}let a=new B(),b=new B();Object.setPrototypeOf(a,{z:7});a.x='x';a.y=null;Object.seal(a);a.z===7&&b instanceof B&&b instanceof A&&b.x===1");
    boolean("let hits=0,p=new Proxy({v:7},{get(t,k,r){hits++;return Reflect.get(t,k,r)}}),o=Object.create(p);Object.defineProperty(o,'x',{value:2});o.v===7&&hits===1&&Object.getPrototypeOf(o)===p");
    boolean("let p={v:7},o=Object.create(p);for(let i=0;i<2000;i++){o.x={i};o.x=i}Object.getPrototypeOf(o)===p&&o.v===7&&o.x===1999");
    Item result = run("function make(){return {x:1}}let a=make(),b=make(),c=make(),d=make();"
        "Object.setPrototypeOf(b,{p:7});Object.preventExtensions(c);d['MVP [[Prototype]]']=9;[a,b,c,d]");
    ASSERT_EQ(get_type_id(result), LMD_TYPE_ARRAY);
    for (int i = 0; i < 4; i++) {
        TypeMap* shape = (TypeMap*)result.array->items[i].map->type;
        bool found = false;
        FOR_EACH_MAP_FIELD(shape, field) found |= field->key_kind == NAME_KEY_PRIVATE;
        EXPECT_EQ(found, i == 1 || i == 2);
    }
}

TEST_F(JsMvpLmd, PropertyExpansionReservedArrayLiteral) {
    boolean("let a=[0,1,2,3,4,5,6,7,8,9];a.length===10&&a.join(',')==='0,1,2,3,4,5,6,7,8,9'");
    boolean("let a=[5e-324,1e-323,2e-323,-0,5e-324];a.length===5&&a[0]===5e-324&&a[1]===1e-323&&a[2]===2e-323&&1/a[3]===-Infinity&&a[4]===5e-324");
    boolean("let a=[,5e-324,,undefined,];a.length===4&&!(0 in a)&&a[1]===5e-324&&!(2 in a)&&3 in a");
    boolean("let a=[5e-324,...[1,2],1e-323,...[,,],7];a.length===7&&a[0]===5e-324&&a[3]===1e-323&&4 in a&&5 in a&&a[6]===7");
    boolean("let log='';function f(v){log+=v;return {v}}let a=[f(1),f(2),f(3)];log==='123'&&a[0].v===1&&a[1].v===2&&a[2].v===3");
    boolean("let n=0;Object.defineProperty(Array.prototype,'0',{set(v){n++},configurable:true});let a=[7,8];delete Array.prototype[0];n===0&&Object.hasOwn(a,'0')&&a[0]===7&&a[1]===8");
    boolean("function gc(){let a=[];for(let i=0;i<2000;i++)a.push({i});return a}let a=[5e-324,gc(),1e-323,gc(),{x:7}];a.length===5&&a[0]===5e-324&&a[1][1999].i===1999&&a[2]===1e-323&&a[3][0].i===0&&a[4].x===7");
    boolean("function make(){return [0,1,2,3]}make().join(',')==='0,1,2,3'");
    char* mir = dump("temp/mvp_lmd_reserved_array_literal.mir");
    ASSERT_NE(mir, nullptr);
    EXPECT_EQ(strstr(mir, "tail_homes"), nullptr);
    EXPECT_EQ(strstr(mir, "\timport\tmvp_lmd_array_store\n"), nullptr);
    mem_free(mir);
}

TEST_F(JsMvpLmd, PropertyExpansionArrayInheritedReadLifetime) {
    boolean("let a=[7];let p={get x(){let garbage=[];for(let i=0;i<2000;i++)garbage.push({i});return this[0]}};Object.setPrototypeOf(a,p);a.x===7");
    boolean("let a=[7],p=[];Object.setPrototypeOf(a,p);Object.defineProperty(p,'x',{get(){Object.setPrototypeOf(this,{x:9});let garbage=[];for(let i=0;i<2000;i++)garbage.push({i});return this[0]}});let x=a.x;x===7&&a.x===9");
    boolean("let a=[7],p={get f(){let self=this;let garbage=[];for(let i=0;i<2000;i++)garbage.push({i});return function(){return this===self&&this[0]===7}}};Object.setPrototypeOf(a,p);a.f()");
    boolean("let a=[7],p={get x(){throw 17}};Object.setPrototypeOf(a,p);let thrown=0;try{a.x}catch(e){thrown=e}thrown===17&&('x' in a)");
    boolean("let a=[7],n=0,p=new Proxy({x:9},{get(t,k,r){n++;let garbage=[];for(let i=0;i<2000;i++)garbage.push({i});return r[0]+Reflect.get(t,k,r)}});Object.setPrototypeOf(a,p);a.x===16&&n===1");
    boolean("let a=[7];function p(){}Object.setPrototypeOf(a,p);a.name==='p'&&a.length===1&&'name' in a");
    boolean("let p=[];Object.setPrototypeOf(p,{get x(){let garbage=[];for(let i=0;i<2000;i++)garbage.push({i});return this[0]}});let a=[7];Object.setPrototypeOf(a,p);a.x===7");
    error("let r=Proxy.revocable({x:1},{}),a=[];Object.setPrototypeOf(a,r.proxy);r.revoke();a.x", "TypeError");
    boolean("let a=[7];Object.setPrototypeOf(a,null);a.x===undefined&&!('x' in a)&&a[0]===7");
}

TEST_F(JsMvpLmd, PropertyExpansionBorrowedNativePrototype) {
    boolean("let test=RegExp.prototype.test.bind(/a/);test('cat')&&!test('dog')");
    boolean("let test=RegExp.prototype.test.bind(/a/g),r=[test('a'),test('a'),test('a')];r[0]&&!r[1]&&r[2]");
    boolean("Object.prototype.toString.call([])==='[object Array]'&&Object.prototype.toString.call(7)==='[object Number]'");
    boolean("Object.prototype.hasOwnProperty.call({x:7},'x')&&!Object.prototype.hasOwnProperty.apply({},['x'])");
    boolean("let test=RegExp.prototype.test.bind(/a/);RegExp.prototype.test=function(){return 7};test('a')&&/b/.test('x')===7");
    boolean("let p=Object.prototype;function f(o){return Object.prototype.toString.call(o)}let first=f({});p.toString=function(){return 'changed'};first==='[object Object]'&&f({})==='changed'");
    boolean("Object.prototype.extra=7;Object.prototype.hasOwnProperty.call({},'extra')===false&&({}).extra===7");
    boolean("let p=Object['pro'+'totype'];p.x=7;Object.prototype.toString.call({})==='[object Object]'&&({}).x===7");
    boolean("let o={get x(){return 7}};Object.prototype.hasOwnProperty.call(o,'x')&&o.x===7");
    boolean("let o={};Object.defineProperty(o,Symbol.toStringTag,{get(){let a=[];for(let i=0;i<2000;i++)a.push({i});return 'Custom'}});Object.prototype.toString.call(o)==='[object Custom]'");
    boolean("let p=new Proxy({x:7},{get(t,k,r){return Reflect.get(t,k,r)}});Object.prototype.toString.call(p)==='[object Object]'&&p.x===7");
    boolean("let make=[]['con'+'structor'];let a=make(1000000000);a[0]=7;a[999999999]=9;a.length===1000000000&&a[0]===7&&a[999999999]===9&&a[1]===undefined");
    boolean("let map=[]['m'+'ap'];let a=map.call({length:100001,100000:7},x=>x+1);a.length===100001&&a[100000]===8&&a[0]===undefined");
    boolean("Object.defineProperty(Array.prototype,'0',{get(){return 'seven'},configurable:true});let value=[][0];delete Array.prototype[0];value==='seven'");
    boolean("Object.defineProperty(Array.prototype,'1',{get(){return {n:7}},configurable:true});let value=[3][1];delete Array.prototype[1];value.n===7");
    boolean("let o={x:7};Object.prototype.hasOwnProperty.call(o,'x')&&o.x===7");
    char* mir = dump("temp/mvp_lmd_borrowed_native_prototype.mir");
    ASSERT_NE(mir, nullptr);
    EXPECT_NE(strstr(mir, "\timport\tmvp_lmd_class_property_fixed"), nullptr);
    EXPECT_EQ(strstr(mir, "\timport\tmvp_lmd_property_with_receiver\n"), nullptr);
    mem_free(mir);
}

TEST_F(JsMvpLmd, PropertyExpansionDefaultArraySpecies) {
    boolean("let a=[1,2];Object.freeze(Array);a.slice().join(',')==='1,2'&&a.map(x=>x+1).join(',')==='2,3'");
    boolean("let log='',a=[1,2];Object.defineProperty(a,'constructor',{get(){log+='c';return Array}});let b=a.slice();log==='c'&&b.join(',')==='1,2'");
    boolean("let log='';Object.defineProperty(Array.prototype,'constructor',{get(){log+='c';return Array},configurable:true});let a=[1,2].slice();log==='c'&&a.join(',')==='1,2'");
    boolean("let a=[1,2],hits=0;Object.defineProperty(Array,Symbol.species,{get(){hits++;a[0]=7;return Array},configurable:true});let b=a.slice();hits===1&&b[0]===7&&b[1]===2");
    boolean("let hits=0;Object.defineProperty(Array,Symbol.species,{get(){hits++;Object.defineProperty(Array,Symbol.species,{value:null});return Array},configurable:true});let a=[1].slice(),b=[2].slice();hits===1&&a[0]===1&&b[0]===2");
    boolean("let hits=0;delete Array[Symbol.species];Object.defineProperty(Object.getPrototypeOf(Array),Symbol.species,{get(){hits++;return Array},configurable:true});let a=[7].slice();hits===1&&a[0]===7");
    boolean("let log='',p=new Proxy([7],{get(t,k,r){if(k==='constructor')log+='c';return Reflect.get(t,k,r)}});let a=Array.prototype.slice.call(p);log==='c'&&a[0]===7");
    boolean("let a=[7],hits=0,p=Object.create(Array.prototype);Object.defineProperty(p,'constructor',{get(){hits++;return Array}});Object.setPrototypeOf(a,p);let b=a.slice();hits===1&&b[0]===7");
    boolean("let a=[1,2],log='';let start={valueOf(){Object.defineProperty(a,'constructor',{get(){log+='c';return Array}});log+='v';return 0}};let b=a.slice(start);log==='vc'&&b[0]===1");
    boolean("let a=[];a[100000]=5e-324;Object.freeze(Array);let b=a.slice(99999);b.length===2&&!(0 in b)&&b[1]===5e-324");
    boolean("Object.defineProperty(Array,Symbol.species,{get:Object.prototype.valueOf});let a=[7].slice();a[0]===7");
    boolean("let a=[7];Object.defineProperty(Array,Symbol.species,{get(){let garbage=[];for(let i=0;i<2000;i++)garbage.push({i});return Array}});a.slice()[0]===7");
}

TEST_F(JsMvpLmd, PropertyExpansionScalarTruthDispatch) {
    boolean("function f(v){return !!v}let values=[undefined,null,false,true,0,-0,NaN,1,-1,0.25,-0.25,5e-324,-5e-324,Infinity,-Infinity,'','0',[],{},function(){}];let result='';for(let i=0;i<values.length;i++)result+=f.call(null,values[i])?'1':'0';result==='00010001111111101111'");
    boolean("function f(v){return !!v}f(0n)===false&&f(1n)&&f(-1n)&&f(123456789012345678901234567890n)&&f(Symbol())");
    boolean("let hits=0,o={valueOf(){hits++;throw 1},toString(){hits++;throw 2}};let p=new Proxy(o,{get(){hits++;throw 3}}),r=Proxy.revocable({},{});r.revoke();!!o&&!!p&&!!r.proxy&&hits===0");
    boolean("function f(v){if(v)return 1;return 0}function g(v){return v?1:0}let a=[false,true,null,undefined,0,-0,NaN,5e-324,'',[],{}];let n=0;for(let i=0;i<a.length;i++)n+=f(a[i])+g(a[i]);n===8");
    boolean("function read(){return !!value}let first=read();var value=true;!first&&read()");
    boolean("let a=new Array(2);Object.defineProperty(Array.prototype,'0',{get(){return 'yes'},configurable:true});let x=!!a[0],y=!!a[1];delete Array.prototype[0];x&&!y");
    boolean("class Node{constructor(next){this.next=next}}let n=new Node(new Node(null)),count=0;while(n){count++;n=n.next}count===2");
    boolean("function f(value){return !!value}f.call(null,{})");
    char* mir = dump("temp/mvp_lmd_scalar_truth.mir");
    ASSERT_NE(mir, nullptr);
    char* function = strstr(mir, "mvp_lmd_f1:\tfunc");
    ASSERT_NE(function, nullptr);
    char* end = strstr(function, "\tendfunc");
    ASSERT_NE(end, nullptr);
    *end = '\0';
    EXPECT_EQ(strstr(function, "container_type"), nullptr);
    mem_free(mir);
}

TEST_F(JsMvpLmd, PropertyExpansionDenseWriteLoop) {
    boolean("let sparse=[];sparse[20000]=1;function fill(a){for(var i=a.length;i--;)a[i]=0;return i}let a=new Array(8);fill(a)===-1&&a.join(',')==='0,0,0,0,0,0,0,0'");
    char* mir = dump("temp/mvp_lmd_dense_write_loop.mir");
    ASSERT_NE(mir, nullptr);
    EXPECT_NE(strstr(mir, "\timport\tmvp_lmd_array_indices_use_own_storage\n"), nullptr);
    EXPECT_NE(strstr(mir, "\tubge\t"), nullptr);
    mem_free(mir);
    boolean("let sparse=[];sparse[20000]=1;function f(a){for(let i=0;i<a.length;i++)a[i]=i%2===0}let a=new Array(4);f(a);a[0]&&!a[1]&&a[2]&&!a[3]");
    boolean("let sparse=[];sparse[20000]=1;function f(a){for(let i=0;i<a.length;i++)a[i]=i===1?-0:5e-324}let a=new Array(8);f(a);let garbage=[];for(let i=0;i<2000;i++)garbage.push({i});a[0]===5e-324&&1/a[1]===-Infinity&&a[7]===5e-324");
    boolean("let hits=0;Object.defineProperty(Array.prototype,'1',{set(v){hits+=v},configurable:true});function f(a){for(var i=a.length;i--;)a[i]=7}let a=new Array(3);f(a);delete Array.prototype[1];hits===7&&a[0]===7&&!(1 in a)&&a[2]===7");
    boolean("let a=[1,2,3];Object.defineProperty(a,'1',{writable:false});function f(a){for(let i=0;i<a.length;i++)a[i]=7}f(a);a[0]===7&&a[1]===2&&a[2]===7");
    error("let a=[1,2,3];Object.defineProperty(a,'1',{writable:false});function f(a){'use strict';for(let i=0;i<a.length;i++)a[i]=7}f(a)", "TypeError");
    boolean("let a=new Array(3);Object.preventExtensions(a);function f(a){for(var i=a.length;i--;)a[i]=7}f(a);!(0 in a)&&!(1 in a)&&!(2 in a)");
    boolean("let a=[1,2,3];Object.seal(a);Object.defineProperty(a,'length',{writable:false});function f(a){for(let i=0;i<a.length;i++)a[i]=7}f(a);a.join(',')==='7,7,7'");
    boolean("let hits=0,p=new Proxy({}, {set(t,k,v,r){hits++;return Reflect.set(t,k,v,r)}}),a=new Array(3);Object.setPrototypeOf(a,p);function f(a){for(let i=0;i<a.length;i++)a[i]=7}f(a);hits===3&&a[0]===7&&a[2]===7");
    boolean("let hits=0;function p(){}Object.defineProperty(p,'0',{set(v){hits+=v}});let a=new Array(2);Object.setPrototypeOf(a,p);function f(a){for(var i=a.length;i--;)a[i]=7}f(a);hits===7&&!Object.hasOwn(a,'0')&&a[1]===7");
    boolean("let sparse=[];sparse[20000]=1;function f(a){let total=0;for(let i=0;i<4;i++){total++;a[i]=total}return total}let a=[0,0];f(a)===4&&a.join(',')==='1,2,3,4'");
    boolean("let sparse=[];sparse[20000]=1;function f(a){let n=0;for(var i=3;i--;){n++;a[i]=n}return n*10+i}let a=[0];f(a)===29&&a.join(',')==='3,2,1'");
    boolean("let sparse=[];sparse[20000]=1;function f(a){let n=0;for(let i=-1;i<2;i++){n++;a[i]=n}return n}let a=[0,0];f(a)===3&&a[-1]===1&&a[0]===2&&a[1]===3");
    boolean("let sparse=[];sparse[20000]=1;function f(a){for(let i=0;i<2;i++)a[i+0.5]=i}let a=[7,8];f(a);a[0]===7&&a[1]===8&&a[0.5]===0&&a[1.5]===1");
    boolean("let hits=0,a=[0];Object.defineProperty(a,'1',{get(){hits++;return 9},configurable:true});function f(a){for(let i=0;i<1;i++){a[i]=7;if(a[i+1]===9)a[i]=8}}f(a);a[0]===8&&hits===1");
    boolean("let hits=0,a=new Array(2);function f(a){for(let i=0;i<2;i++){a[i]=7;Object.defineProperty(a,'1',{set(v){hits+=v},configurable:true})}}f(a);a[0]===7&&hits===7");
    boolean("let sparse=[];sparse[20000]=1;function f(a){for(let i=0;i<2;i++){for(let j=0;j<2;j++)a[i+j]=i+j}}let a=[0,0];f(a);a.join(',')==='0,1,2'");
}

TEST_F(JsMvpLmd, PropertyExpansionOptionalNumericKeys) {
    boolean("function f(a){let ip=0,n=0;while(ip<a.length){ip=a[ip];n++;ip++}return n}let a=new Int32Array(4);a[0]=1;a[2]=3;f(a)===2");
    boolean("function f(a){let ip=0,n=0;while(ip<a.length){ip=a[ip];n++;if(n===2)ip=20;ip++}return n}let a=new Int32Array(4);a[0]=1;f(a)===2");
    boolean("let a=[2,3],indices=new Int32Array(1);a.undefined='named';let key=indices[9];a[key]==='named'&&typeof a[key]==='string'");
    boolean("let indices=new Int32Array(1);Object.defineProperty(Array.prototype,'undefined',{get(){return 'inherited'},configurable:true});let key=indices[9],value=[2,3][key];delete Array.prototype.undefined;value==='inherited'");
    boolean("let a=[1,2],key=0;for(let i=0;i<2;i++)key=a[key];typeof key==='number'&&key===2");
    boolean("let a=[1,2],key=0;for(let i=0;i<3;i++)key=a[key];key===undefined");
    // the typed-array adapter still rejects named keys; inference must retain that capability check.
    error(R"JS(let a=new Int32Array(1),index=a[9];a[index])JS", "capability");
}

TEST_F(JsMvpLmd, PropertyExpansionOwnMapRead) {
    boolean("function f(){let o={x:1},alias=o,total=0;for(let i=0;i<20;i++){o.x=i;total+=o.x;o.x='text';o.x=i}return total}f()===190");
    char* mir = dump("temp/mvp_lmd_own_map_read.mir");
    ASSERT_NE(mir, nullptr);
    EXPECT_NE(strstr(mir, "\timport\ttypemap_hash_lookup_key\n"), nullptr);
    EXPECT_NE(strstr(mir, "\timport\tmap_shape_field_to_item\n"), nullptr);
    mem_free(mir);
    boolean("function f(o,k){return o[k]}let o={x:1};o.x='text';f(o,'x')==='text'&&f(o,'absent')===undefined");
    boolean("let o={x:1},alias=o;o.x=null;let a=alias.x;o.x=undefined;let b=alias.x;delete o.x;a===null&&b===undefined&&alias.x===undefined");
    boolean("let o={x:1},alias=o;for(let i=0;i<10;i++){o.x={n:i};if(alias.x.n!==i)throw 1;o.x='text'}alias.x==='text'");
    boolean("function change(o){o.x=9;let a=[];for(let i=0;i<2000;i++)a.push({i});return 7}let o={x:1};o.x=5e-324;let before=o.x;let result=o.x+change(o);before===5e-324&&result===7&&o.x===9");
    boolean("let o={x:1};o.x=-0;let a=o.x;o.x=5e-324;1/a===-Infinity&&o.x===5e-324");
    boolean("let o={x:1};o.x='text';o.absent===undefined&&o.x==='text'");
    boolean("let o={x:1};typeof o['con'+'structor']==='function'");
    boolean("let hits=0,o={get x(){hits++;return 7}};o.x===7&&hits===1");
    mir = dump("temp/mvp_lmd_own_map_accessor.mir");
    ASSERT_NE(mir, nullptr);
    EXPECT_EQ(strstr(mir, "\timport\ttypemap_hash_lookup_key\n"), nullptr);
    mem_free(mir);
}

TEST_F(JsMvpLmd, PropertyExpansionObjectTagLifetime) {
    boolean("function garbage(){let a=[];for(let i=0;i<2000;i++)a.push({i});return 'Fresh'}let o={get [Symbol.toStringTag](){return garbage()+'Tag'}};Object.prototype.toString.call(o)==='[object FreshTag]'");
    boolean("let o={get [Symbol.toStringTag](){Object.setPrototypeOf(this,null);let a=[];for(let i=0;i<2000;i++)a.push({i});return 'Detached'}};Object.prototype.toString.call(o)==='[object Detached]'&&Object.getPrototypeOf(o)===null");
    boolean("let hits=0,p=new Proxy([],{get(t,k,r){hits++;let a=[];for(let i=0;i<2000;i++)a.push({i});return k===Symbol.toStringTag?'CustomArray':Reflect.get(t,k,r)}});Object.prototype.toString.call(p)==='[object CustomArray]'&&hits===1");
    boolean("let p=new Proxy(function(){}, {get(t,k,r){let a=[];for(let i=0;i<2000;i++)a.push({i});return k===Symbol.toStringTag?'Callable':Reflect.get(t,k,r)}});Object.prototype.toString.call(p)==='[object Callable]'");
    boolean("function f(){};Object.setPrototypeOf(f,{get [Symbol.toStringTag](){let a=[];for(let i=0;i<2000;i++)a.push({i});return 'Lazy'}});Object.prototype.toString.call(f)==='[object Lazy]'");
    boolean("let o={get [Symbol.toStringTag](){throw 7}},value=0;try{Object.prototype.toString.call(o)}catch(e){value=e}value===7");
    boolean("let o={get [Symbol.toStringTag](){let a=[];for(let i=0;i<2000;i++)a.push({i});return 5e-324}};Object.prototype.toString.call(o)==='[object Object]'");
    error("let r=Proxy.revocable({},{});r.revoke();Object.prototype.toString.call(r.proxy)", "TypeError");
}

TEST_F(JsMvpLmd, PropertyExpansionMapPairOwnership) {
    boolean("function f(){let m=new Map([[1,5e-324],[2,-0]]),n=0;for(let [k,v] of m){m.set(k,7);let a=[];for(let i=0;i<1000;i++)a.push({i});if(k===1&&v===5e-324)n++;if(k===2&&1/v===-Infinity)n++}return n}f()===2");
    boolean("function f(){let m=new Map([[5e-324,-5e-324]]),a=[];for(let [k,v] of m){m.clear();a.push(()=>k,()=>v)}return a[0]()===5e-324&&a[1]()===-5e-324}f()");
    boolean("function f(){let key={x:7},value={y:9},m=new Map([[key,value]]);for(let [k,v] of m){m.clear();key=null;value=null;let a=[];for(let i=0;i<2000;i++)a.push({i});return k.x+v.y}return 0}f()===16");
    boolean("function f(){let m=new Map([[1,5e-324],[2,1e-323]]),sum=0;for(let [k,v] of m){for(let [x,y] of m){if(x===k){sum+=v+y;break}}}return sum}f()===3e-323");
    boolean("let k,v,m=new Map([[1,5e-324]]);for([k,v] of m){m.set(k,7)}k===1&&v===5e-324");
    boolean("function f(){let m=new Map([[1,2]]);m[Symbol.iterator]=function*(){yield [7,5e-324]};for(let [k,v] of m)return k===7&&v===5e-324}f()");
}

TEST_F(JsMvpLmd, PropertyExpansionProjectionOrdering) {
    boolean("let o={a:1,b:2,c:3};Object.keys(o).join(',')==='a,b,c'&&Object.values(o).join(',')==='1,2,3'&&Object.entries(o)[1].join(',')==='b,2'");
    boolean("let o={z:9,10:10,2:2,a:7,1:1};Object.keys(o).join(',')==='1,2,10,z,a'&&Object.values(o).join(',')==='1,2,10,9,7'");
    boolean("let a=Symbol('a'),b=Symbol('b'),o={};o[a]=1;o.z=2;o[b]=3;o[2]=4;o[1]=5;let k=Reflect.ownKeys(o);k.length===5&&k[0]==='1'&&k[1]==='2'&&k[2]==='z'&&k[3]===a&&k[4]===b");
    boolean("let o={a:1,b:2,c:3};delete o.b;o.b=4;Object.keys(o).join(',')==='a,c,b'&&Object.values(o).join(',')==='1,3,4'");
    boolean("let o={a:1,b:2,c:3};Object.defineProperty(o,'b',{enumerable:false});Object.keys(o).join(',')==='a,c'&&Object.getOwnPropertyNames(o).join(',')==='a,b,c'");
    boolean("let o={get a(){delete this.b;this.d=4;return 1},b:2,c:3};Object.values(o).join(',')==='1,3'&&Object.keys(o).join(',')==='a,c,d'");
    boolean("let a=[7,8];a[100000]=9;Object.defineProperty(a,'1',{get(){return 5},enumerable:true,configurable:true});a.z=4;Object.keys(a).join(',')==='0,1,100000,z'&&Object.values(a).join(',')==='7,5,9,4'");
    boolean("let o={a:5e-324,b:-0};let values=Object.values(o);o.a=7;o.b=8;let garbage=[];for(let i=0;i<2000;i++)garbage.push({i});values[0]===5e-324&&1/values[1]===-Infinity");
    boolean("Object.keys({}).length===0&&Object.values({x:7})[0]===7&&Object.entries({}).length===0");
}

TEST_F(JsMvpLmd, PropertyExpansionPrimitiveStringBatch) {
    boolean("function f(n){return '['+n+','+(n+1)+','+(n+2)+','+(n+3)+','+(n+4)+','+(n+5)+']'}f(10)==='[10,11,12,13,14,15]'");
    char* mir = dump("temp/mvp_lmd_primitive_string_batch.mir");
    ASSERT_NE(mir, nullptr);
    EXPECT_NE(strstr(mir, "\timport\tfn_strcat_many\n"), nullptr);
    mem_free(mir);
    boolean("let n=0;function piece(){n++;return '['+n+']'}let s='>'+piece()+piece()+piece()+piece()+piece()+piece()+piece()+piece()+'<';s==='>[1][2][3][4][5][6][7][8]<'&&n===8");
    boolean("function f(){let a=[];for(let i=0;i<1000;i++)a.push({i});return 'value'}let s=':'+f()+':'+f()+':'+f()+':'+f()+':'+f()+':'+f()+':';s===':value:value:value:value:value:value:'");
    boolean("function f(n){return ''+n+':'+true+':'+null+':'+undefined+':'+(-0)+':'+NaN+':'+Infinity}f(7)==='7:true:null:undefined:0:NaN:Infinity'");
    boolean("let a='a',b=a+'b',c=b+'c';let s=b+':'+c+':'+b+':'+c+':'+b+':'+c;b==='ab'&&c==='abc'&&s==='ab:abc:ab:abc:ab:abc'");
    boolean("function f(s){return s+'\\0'+s+'é'+s+'😀'+s+'\\0'+s}let s=f('x');s==='x\\0xéx😀x\\0x'&&s.length===10");
    boolean("let seen='',o={toString(){seen+='o';return 'O'}};function p(){seen+='p';return 'P'}let s='a'+p()+':'+p()+':'+o+':'+p()+':'+p();s==='aP:P:O:P:P'&&seen==='ppopp'");
    boolean("let n=0;function p(){n++;if(n===4)throw 7;return 'x'}let caught=0;try{let s=':'+p()+p()+p()+p()+p()+p()+p()}catch(e){caught=e}n===4&&caught===7");
    boolean("let n=0,sym=Symbol('x');function p(){n++;return 'x'}let caught=false;try{let s=':'+p()+p()+p()+sym+p()+p()+p()}catch(e){caught=true}caught&&n===3");
    boolean("let s='a'+1+2+3+4+5+6+7+8+9+10+11+12+13+14+15+16+17+18+19+20+21+22+23+24+25+26+27+28+29+30+31+32+33+34+35+36+37+38+39+40+41+42+43+44+45+46+47+48+49+50+51+52+53+54+55+56+57+58+59+60+61+62+63+64+65+66+67+68+69+70;s.length===132&&s.slice(0,12)==='a12345678910'&&s.slice(-8)==='67686970'");
}

TEST_F(JsMvpLmd, PropertyExpansionAsciiMapKeys) {
    boolean("let s='ACGTACGTACGTACGTACGT',m=new Map();for(let i=0;i<20;i++){let k=s.substring(0,12);m.set(k,(m.get(k)||0)+1)}m.size===1&&m.get('ACGTACGTACGT')===20&&m.has(s.substring(0,12))");
    boolean("let m=new Map(),k='long\\0ascii-key';m.set(k,7);m.get('long'+'\\0'+'ascii-key')===7&&m.get('long')===undefined&&m.delete(k)&&m.size===0");
    boolean("let m=new Map(),a='é'+'😀',b='é'+'\\ud83d'+'\\ude00';m.set(a,7);m.set(b,9);m.size===1&&m.get(a)===9&&m.has(b)&&m.delete(b)&&m.size===0");
    boolean("let m=new Map(),s='éabcdefghijkl';m.set(s.substring(1),7);m.get('abcdefghijkl')===7&&m.has(s.substring(1))");
    boolean("let m=new Map(),a='\\ud800',b='\\udc00';m.set(a,1);m.set(b,2);m.set(a+b,3);m.size===3&&m.get(a)===1&&m.get(b)===2&&m.get('𐀀')===3");
    boolean("let o={},k='long\\0key';o[k]=7;o['long'+'\\0key']===7&&Object.hasOwn(o,k)&&o.long===undefined");
}

TEST_F(JsMvpLmd, PropertyExpansionArrayWriteBranches) {
    boolean("function put(a,i,v){a[i]=v}let a=[1,2];put(a,0,7);put(a,2,9);put(a,9,11);a.length===10&&a[0]===7&&a[1]===2&&a[2]===9&&a[8]===undefined&&a[9]===11");
    boolean("function put(a,i,v){a[i]=v}let a=new Array(3);put(a,1,7);put(a,-1,8);put(a,0.5,9);a.length===3&&!(0 in a)&&a[1]===7&&a[-1]===8&&a[0.5]===9");
    boolean("function put(a,i,v){a[i]=v}let a=[1];put(a,100000,7);put(a,0,9);put(a,100000,11);a.length===100001&&a[0]===9&&a[100000]===11&&a[99999]===undefined");
    boolean("function put(a,i,v){a[i]=v}let a=[];put(a,0,5e-324);put(a,1,-0);put(a,0,-5e-324);let b=[];for(let i=0;i<1000;i++)b.push({i});a.length===2&&a[0]===-5e-324&&1/a[1]===-Infinity");
    boolean("function put(a,i,v){a[i]=v}let a=[1,2];Object.defineProperty(a,'length',{writable:false});put(a,0,7);put(a,2,9);a.length===2&&a[0]===7&&a[2]===undefined");
    boolean("function put(a,i,v){a[i]=v}let n=0,a=[1,2];Object.defineProperty(a,'1',{set(v){n+=v}});put(a,1,7);put(a,2,9);n===7&&a.length===3&&a[2]===9");
}

TEST_F(JsMvpLmd, PropertyExpansionLocalScalarInitialization) {
    boolean("function f(a){const x=a[0];a[0]=7;let b=[];for(let i=0;i<1000;i++)b.push({i});return x===5e-324}f([5e-324])");
    boolean("function f(a){const x=a[0],y=x;a[0]=9;return x===-5e-324&&y===-5e-324}f([-5e-324])");
    boolean("function f(a){let sum=0;for(let i=0;i<3;i++){const x=a[i];a[i]=7;if(x===undefined||x===5e-324||1/x===-Infinity)sum++}return sum}f([5e-324,-0])===3");
    boolean("function f(a){const x=a[0];let saved=()=>x;a[0]=7;return saved()}f([5e-324])===5e-324");
    boolean("function f(){let a=[5e-324];function read(){const x=a[0];a[0]=9;return x}return read()}f()===5e-324");
    boolean("function f(a){const x=a[0];a[0]=7;let b=[];for(let i=0;i<1000;i++)b.push({i});return x.n}f([{n:9}])===9");
    boolean("function f(a){const x=a[0];a[0]=7;return x}let n=0,a=[];Object.defineProperty(a,'0',{get(){n++;return 5e-324},set(v){n+=v}});f(a)===5e-324&&n===8");
    boolean("function f(a){const x=a[100000];a[100000]=7;return x}let a=[];a[100000]=5e-324;f(a)===5e-324");
    boolean("let x;function f(a){const local=a[0];x=local;a[0]=7}f([5e-324]);x===5e-324");
}

TEST_F(JsMvpLmd, PropertyExpansionArrayPairOwnership) {
    boolean("function f(rows){let n=0;for(let [x,y] of rows){rows[0][0]=7;rows[0][1]=8;let a=[];for(let i=0;i<1000;i++)a.push({i});if(x===5e-324&&1/y===-Infinity)n++}return n}f([[5e-324,-0]])===1");
    boolean("function f(rows){let saved=[];for(let [x,y] of rows){rows[0][0]=7;rows[0][1]=8;saved.push(()=>x,()=>y)}return saved[0]()===5e-324&&saved[1]()===-5e-324}f([[5e-324,-5e-324]])");
    boolean("function f(rows){for(let [x,y] of rows){rows[0]=null;let a=[];for(let i=0;i<2000;i++)a.push({i});return x.n+y.n}}f([[{n:7},{n:9}]])===16");
    boolean("function f(rows){for(let [x,y] of rows)return x===5e-324&&y===9}let pair=[5e-324,0];Object.defineProperty(pair,'1',{get(){pair[0]=7;let a=[];for(let i=0;i<1000;i++)a.push({i});return 9}});f([pair])");
    boolean("function f(rows){for(let [x,y] of rows){rows[0][0]=7;return x===5e-324&&y===undefined}}let pair=[5e-324];pair[100000]=9;f([pair])");
    boolean("let x,y,rows=[[5e-324,-0]];for([x,y] of rows){rows[0][0]=7;rows[0][1]=8}x===5e-324&&1/y===-Infinity");
    boolean("function f(rows){for(let [x,y] of rows)return x===7&&y===5e-324}let rows=[[1,2]];rows[Symbol.iterator]=function*(){yield [7,5e-324]};f(rows)");
    boolean("function f(rows){let sum=0;for(let [x,y] of rows){for(let [u,v] of rows){sum+=x+y+u+v}}return sum}f([[5e-324,5e-324]])===2e-323");
}
