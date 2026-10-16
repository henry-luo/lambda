#include "lambda/js/js_runtime_state.hpp"
#include "lambda/js/js_runtime.h"
#include "lambda/js/js_interp.hpp"
#include "lambda/js/js_transpiler.hpp"
#include "lambda/js/js_mir_internal.hpp"
#include "lambda/js/js_event_loop.h"
#include <gtest/gtest.h>
#include "lambda/js/mvp-lmd/mvp_lmd.h"
#include "lambda/js/mvp-lmd/mvp_lmd_runtime.h"
#include "lambda/js/js_object_meta.h"
#include "lambda/js/js_typed_array.h"
#include "lambda/js/parser/js_parser.h"
#include "lambda/runtime/runtime-state.h"
#include "lambda/runtime/module_registry.h"
#include "lambda/runtime/context_capsule.h"
#include "lambda/runtime/heap_api.h"
#include "lambda/runtime/transpiler.hpp"
#include "lambda/runtime/gc/gc_heap.h"
#include "lambda/runtime/lambda-root-frame.hpp"
#include "lib/file.h"
#include "lib/mem.h"
#include "lib/hashmap.h"
#include "lib/strbuf.h"
#include <math.h>
#include <string.h>

class JsFormalMir : public ::testing::Test {
protected:
    Runtime runtime = {};
    uint64_t result_home = 0;
    char* previous_backend = NULL;
    void SetUp() override {
        const char* previous = getenv("JS_EXEC_BACKEND");
        previous_backend = previous ? mem_dup_n(previous, strlen(previous), MEM_CAT_TEMP) : NULL;
        setenv("JS_EXEC_BACKEND", "mir", 1);
        runtime_init(&runtime);
    }
    void TearDown() override {
        runtime_cleanup(&runtime);
        if (previous_backend) setenv("JS_EXEC_BACKEND", previous_backend, 1);
        else unsetenv("JS_EXEC_BACKEND");
        mem_free(previous_backend);
    }
    Item run(const char* source) {
        return transpile_js_to_mir_len(&runtime, source, strlen(source), "<formal-mir>", &result_home);
    }
};

TEST_F(JsFormalMir, PublicEntryRetainsMvpClosureAndCallerHeap) {
    Item value = run("let x={n:40}; function next(){return ++x.n} next");
    ASSERT_EQ(get_type_id(value), LMD_TYPE_FUNC);
    EXPECT_TRUE(function_has_abi(value.function, FN_ENTRY_ABI_MVP_LMD));
    EXPECT_EQ(context, runtime.eval_context);
    RootFrame roots(1);
    Rooted<Item> function(roots, value);
    heap_gc_collect();
    Item result = js_call_function_into(function.get(), make_js_undefined(), NULL, 0, &result_home);
    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(it2d(result), 41);
}

TEST_F(JsFormalMir, PublicScriptsShareLexicalsIntrinsicsAndEscapingClosures) {
    Item value = run("let total=40;var firstArray=Array;function next(){return ++total}next");
    ASSERT_EQ(get_type_id(value), LMD_TYPE_FUNC);
    RootFrame roots(1);
    Rooted<Item> function(roots, value);
    Item result = run("const step=2;total+=step;gc();firstArray===Array&&next()===43");
    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(result.item, ITEM_TRUE);
    result = js_call_function_into(function.get(), make_js_undefined(), NULL, 0, &result_home);
    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(it2d(result), 44);
    result = run("Function('return total+step')()");
    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(it2d(result), 46);
}

TEST_F(JsFormalMir, PublicRedeclarationFailsBeforeExecutingAndKeepsRealm) {
    ASSERT_FALSE(item_is_error(run("let kept=7;globalThis.effects=0")));
    Item result = run("effects++;let kept=9");
    ASSERT_TRUE(item_is_error(result));
    char message[128];
    js_error_lane_format(result, message, sizeof(message));
    EXPECT_NE(strstr(message, "SyntaxError"), nullptr);
    result = run("kept===7&&effects===0");
    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(result.item, ITEM_TRUE);
}

TEST_F(JsFormalMir, PublicHeapReplacementCreatesFreshRealm) {
    ASSERT_FALSE(item_is_error(run("let oldValue=42;Array.prototype.oldMarker=1")));
    runtime_reset_heap(&runtime);
    Item result = run("typeof oldValue==='undefined'&&Array.prototype.oldMarker===undefined");
    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(result.item, ITEM_TRUE);
}

TEST_F(JsFormalMir, HostPerformanceAndDateShareVirtualAndFrameClocks) {
    ASSERT_FALSE(item_is_error(run("performance.timeOrigin>1e12")));
    double start = js_performance_monotonic_now_ms();
    js_performance_virtual_clock_set(true, start);
    Item result = run(R"JS(
        var clockStart=performance.now(), dateStart=Date.now(), clockOrigin=performance.timeOrigin;
        Math.abs(clockOrigin+clockStart-dateStart)<1 && new Date().getTime()===dateStart
    )JS");
    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(result.item, ITEM_TRUE);
    js_performance_virtual_clock_set(true, start + 250);
    result = run(R"JS(
        gc(); Math.abs(performance.now()-clockStart-250)<1e-6 &&
        Date.now()-dateStart===250 && new Date().getTime()===Date.now() &&
        Date()===(new Date()).toString() && performance.timeOrigin===clockOrigin
    )JS");
    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(result.item, ITEM_TRUE);
    js_performance_frame_clock_begin(start + 500);
    result = run("Math.abs(performance.now()-clockStart-500)<1e-6 && Date.now()-dateStart===500");
    js_performance_frame_clock_end();
    EXPECT_FALSE(item_is_error(result));
    EXPECT_EQ(result.item, ITEM_TRUE);
    result = run("Math.abs(performance.now()-clockStart-250)<1e-6 && Date.now()-dateStart===250");
    js_performance_virtual_clock_set(false, 0);
    EXPECT_FALSE(item_is_error(result));
    EXPECT_EQ(result.item, ITEM_TRUE);
}

TEST_F(JsFormalMir, PublicTimersReuseNativeHandlesAndCancellation) {
    Item result = run(R"JS(
        let timer=setInterval(()=>{throw 'cancelled timer ran'},100000),
            numeric=setTimeout(()=>{throw 'numeric cancellation failed'},100000);
        gc();
        let valid=typeof timer==='object' && typeof timer[Symbol.toPrimitive]==='function' &&
            timer.hasRef() && timer.unref()===timer && !timer.hasRef() &&
            timer.ref()===timer && timer.hasRef() && timer.refresh()===timer && Number(timer)>0;
        clearInterval(timer); clearTimeout(Number(numeric));
        valid && timer._destroyed && numeric._destroyed
    )JS");
    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(result.item, ITEM_TRUE);
}

TEST_F(JsFormalMir, StandaloneHostFamiliesShareDescriptorsAndConstructorIdentity) {
    Item result = run(R"JS(
        localStorage.clear(); localStorage.setItem('formal','value');
        sessionStorage.clear(); sessionStorage.setItem('other','session');
        let storage=localStorage, storageDescriptor=Object.getOwnPropertyDescriptor(globalThis,'localStorage');
        let encoder=new TextEncoder(), decoder=new TextDecoder();
        let headers=new Headers([['X-Formal','value']]);
        let strategy=new CountQueuingStrategy({highWaterMark:3});
        gc();
        globalThis===self && self===window && window===parent && parent===top && top===global &&
        typeof event==='undefined' && storage===localStorage && storage instanceof Storage &&
        storage.getItem('formal')==='value' && sessionStorage.getItem('other')==='session' &&
        typeof storageDescriptor.get==='function' && storageDescriptor.set===undefined &&
        Storage.prototype.constructor===Storage && typeof matchMedia('(min-width: 1px)').matches==='boolean' &&
        decoder.decode(encoder.encode('héllo'))==='héllo' && TextEncoder.prototype.constructor===TextEncoder &&
        headers.get('x-formal')==='value' && Headers.prototype.constructor===Headers &&
        strategy.highWaterMark===3 && strategy.size('anything')===1
    )JS");
    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(result.item, ITEM_TRUE);
}

TEST_F(JsFormalMir, LazyHostFamiliesPreserveReplacedAndDeletedGlobals) {
    Item result = run(R"JS(
        let replacement=function UserDecoder(){};
        Object.defineProperty(globalThis,'TextDecoder',{value:replacement,writable:false,configurable:false});
        delete globalThis.sessionStorage;
        Object.preventExtensions(globalThis);
        let Encoder=TextEncoder, storage=localStorage;
        gc();
        let valid=TextDecoder===replacement && !('sessionStorage' in globalThis) &&
            Object.getOwnPropertyDescriptor(globalThis,'TextDecoder').writable===false &&
            Encoder.prototype.constructor===Encoder && new Encoder().encode('ok').length===2 &&
            storage instanceof Storage;
        delete globalThis.TextEncoder;
        valid && !('TextEncoder' in globalThis) && new Encoder().encode('kept').length===4
    )JS");
    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(result.item, ITEM_TRUE);
}

TEST_F(JsFormalMir, NativeOwnKeysPreserveMvpSymbolIdentity) {
    Item value = run("let nativeKey=Symbol('native-key');({value:1,[nativeKey]:2})");
    ASSERT_FALSE(item_is_error(value));
    RootFrame roots(2);
    Rooted<Item> object(roots, value), keys(roots, js_reflect_own_keys(object.get()));
    ASSERT_EQ(get_type_id(keys.get()), LMD_TYPE_ARRAY);
    ASSERT_EQ(keys.get().array->length, 2);
    EXPECT_EQ(get_type_id(keys.get().array->items[0]), LMD_TYPE_STRING);
    EXPECT_EQ(get_type_id(keys.get().array->items[1]), LMD_TYPE_SYMBOL);
    Item key = run("nativeKey");
    ASSERT_FALSE(item_is_error(key));
    EXPECT_EQ(keys.get().array->items[1].item, key.item);
}

TEST_F(JsFormalMir, NativeWindowTasksRetainTheActiveGlobalAndPayload) {
    Item value = run(R"JS(
        let messages=[];
        window.addEventListener('message', function(event) {
            gc(); messages.push(this===window && event.source===window &&
                event.data.tiny===5e-324 && event instanceof MessageEvent);
        });
        window.postMessage({tiny:5e-324},'*'); gc(); messages
    )JS");
    ASSERT_FALSE(item_is_error(value));
    ASSERT_EQ(get_type_id(value), LMD_TYPE_ARRAY);
    ASSERT_EQ(value.array->length, 1);
    EXPECT_EQ(value.array->items[0].item, ITEM_TRUE);
}

TEST_F(JsFormalMir, NativeHostBasesPreserveConstructionFieldsAndLiveLinks) {
    Item value = run(R"JS(
        class Strategy extends CountQueuingStrategy {
            constructor(n) { super({highWaterMark:n}); this.own=n+1; }
            size() { return this.own; }
        }
        class Derived extends Strategy { extra=6; }
        let instance=new Derived(4), getter=Object.getOwnPropertyDescriptor(CountQueuingStrategy.prototype,'highWaterMark').get;
        gc();
        let valid=instance instanceof Derived && instance instanceof Strategy &&
            instance instanceof CountQueuingStrategy && instance.highWaterMark===4 &&
            instance.size()===5 && instance.extra===6 &&
            Object.getPrototypeOf(Strategy)===CountQueuingStrategy &&
            Object.getPrototypeOf(Strategy.prototype)===CountQueuingStrategy.prototype;
        Object.setPrototypeOf(instance,null);
        valid && getter.call(instance)===4
    )JS");
    ASSERT_FALSE(item_is_error(value));
    EXPECT_EQ(value.item, ITEM_TRUE);
}

TEST_F(JsFormalMir, NativeWritesPreserveMvpIteratorShapeAndMethods) {
    Item value = run("[1,2].values()");
    ASSERT_FALSE(item_is_error(value));
    RootFrame roots(3);
    Rooted<Item> iterator(roots, value), prototype(roots, js_get_prototype_of(iterator.get()));
    Rooted<Item> next(roots, js_get_key_cstr(iterator.get(), "next"));
    ASSERT_EQ(get_type_id(next.get()), LMD_TYPE_FUNC);
    TypeMap* shared_shape = (TypeMap*)iterator.get().map->type;
    int64_t shared_length = shared_shape->length;
    ASSERT_FALSE(item_is_error(js_iterator_return_checked(iterator.get(), true, "invalid iterator")));
    EXPECT_EQ(shared_shape->length, shared_length);
    heap_gc_collect();
    EXPECT_EQ(js_get_prototype_of(iterator.get()).item, prototype.get().item);
    EXPECT_EQ(js_get_key_cstr(iterator.get(), "next").item, next.get().item);
    Item keys = js_reflect_own_keys(iterator.get());
    ASSERT_EQ(get_type_id(keys), LMD_TYPE_ARRAY);
    EXPECT_EQ(keys.array->length, 0);
    Item result = js_iterator_step(iterator.get());
    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(it2d(result), 1);
    iterator.set(run("[3,4].values()"));
    ASSERT_FALSE(item_is_error(iterator.get()));
    result = js_iterator_step(iterator.get());
    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(it2d(result), 3);
}

TEST_F(JsFormalMir, NativeFactoriesKeepLazyBindingOverridesAndIntrinsicPrototypes) {
    Item value = run(R"JS(
        let blobs=[];
        delete globalThis.Blob;
        Object.defineProperty(globalThis,'Blob',{get(){throw 'guest Blob read'},configurable:true});
        new Response('kept',{headers:{'content-type':'text/plain'}}).blob().then(async blob=>{
            gc(); blobs.push(await blob.text()==='kept' && blob.type==='text/plain' &&
                typeof Object.getPrototypeOf(blob).constructor==='function');
        }); blobs
    )JS");
    ASSERT_FALSE(item_is_error(value));
    ASSERT_EQ(get_type_id(value), LMD_TYPE_ARRAY);
    ASSERT_EQ(value.array->length, 1);
    EXPECT_EQ(value.array->items[0].item, ITEM_TRUE);
}

TEST_F(JsFormalMir, NativeIteratorCacheBypassesGuestPropertiesAndKeepsNextSnapshot) {
    Item value = run(R"JS(
        let nextReads=0;
        let source=Object.freeze({
            get next(){nextReads++;return function(){gc();return {value:7,done:false}}},
            get __iter_next__(){throw 'guest cache read'},
            [Symbol.iterator](){return this}
        }); source
    )JS");
    ASSERT_FALSE(item_is_error(value));
    RootFrame roots(2);
    Rooted<Item> source(roots, value), iterator(roots, js_get_iterator(source.get()));
    ASSERT_FALSE(item_is_error(iterator.get()));
    heap_gc_collect();
    for (int i = 0; i < 2; i++) {
        Item result = js_iterator_step(iterator.get());
        ASSERT_FALSE(item_is_error(result));
        EXPECT_EQ(it2d(result), 7);
    }
    Item result = run("nextReads===1 && Reflect.ownKeys(source).length===3 && Object.isFrozen(source)");
    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(result.item, ITEM_TRUE);
}

TEST_F(JsFormalMir, SharedRequireRetainsSourcePathAndNestedModuleCache) {
    const char* source = R"JS(
        let retainedRequire=require, first=require('./main.cjs'), second=require('./main.cjs');
        let explicitRequire=require('module').createRequire('test/js/interp_cjs/entry.js');
        gc(); first.answer===42 && first===second && explicitRequire('./main.cjs')===first &&
            globalThis.__interp_cjs_main_loads===1 && globalThis.__interp_cjs_dep_loads===1
    )JS";
    Item value = transpile_js_to_mir_len(&runtime, source, strlen(source),
        "test/js/interp_cjs/entry.js", &result_home);
    ASSERT_FALSE(item_is_error(value));
    EXPECT_EQ(value.item, ITEM_TRUE);
    value = run("gc();typeof console.log==='function' && retainedRequire('./dep.cjs').value===40");
    ASSERT_FALSE(item_is_error(value));
    EXPECT_EQ(value.item, ITEM_TRUE);
}

TEST_F(JsFormalMir, StaticMethodSourceReusesSharedFunctionSpans) {
    Item value = run(R"JS(
        class Sources {
            static /* before */ method /* args */ () { /* body */ }
            static /* before */ get value() { return 1; }
            static /* before */ set value(v) { }
            static /* before */ async task() { }
            static /* before */ *items() { }
            static /* before */ async *tasks() { }
            static /* before */ #hidden() { }
            static reveal() { return this.#hidden; }
        }
        let show=Function.prototype.toString, descriptor=Object.getOwnPropertyDescriptor(Sources,'value');
        gc(); show.call(Sources.method)==='method /* args */ () { /* body */ }' &&
            show.call(descriptor.get)==='get value() { return 1; }' &&
            show.call(descriptor.set)==='set value(v) { }' &&
            show.call(Sources.task)==='async task() { }' &&
            show.call(Sources.items)==='*items() { }' &&
            show.call(Sources.tasks)==='async *tasks() { }' &&
            show.call(Sources.reveal())==='#hidden() { }'
    )JS");
    ASSERT_FALSE(item_is_error(value));
    EXPECT_EQ(value.item, ITEM_TRUE);
}

TEST_F(JsFormalMir, LooseBoxedEqualityReusesRetainedCoercion) {
    Item value = run(R"JS(
        let calls=0, hint='', marker={};
        let object={[Symbol.toPrimitive](h){calls++;hint=h;gc();return 5e-324}};
        let same={}, different={valueOf(){throw 'unexpected identity coercion'}};
        let valid=Object.bind(null)(42)==42 && new Boolean(true)==true &&
            object==5e-324 && calls===1 && hint==='default' &&
            same==same && !(same==different) && !(object==null);
        let failed=false;
        try { ({valueOf(){gc();throw marker}})==1; } catch(error) {failed=error===marker}
        valid && failed
    )JS");
    ASSERT_FALSE(item_is_error(value));
    EXPECT_EQ(value.item, ITEM_TRUE);
}

TEST_F(JsFormalMir, FunctionEnumerationPreparesOwnStorageAndSkipsInheritedAccessors) {
    Item value = run(R"JS(
        Object.defineProperty(Function.prototype,'hidden',{get(){throw 'getter ran'},configurable:true});
        function fresh(){} let keys=[];
        for(let name in fresh) keys.push(name);
        gc(); keys.length===0 && !fresh.hasOwnProperty('hidden')
    )JS");
    ASSERT_FALSE(item_is_error(value));
    EXPECT_EQ(value.item, ITEM_TRUE);
}

TEST_F(JsFormalMir, ArrayKeyAndEntryIteratorsReuseSequenceStorage) {
    Item result = run(R"JS(
        let values=[5e-324,,3],keys=values.keys(),entries=values.entries();
        let first=entries.next().value;
        values[0]=7;gc();
        let second=entries.next().value;
        values.push(4);
        let ok=keys[Symbol.iterator]()===keys&&first[0]===0&&first[1]===5e-324&&
            second[0]===1&&second[1]===undefined&&[...keys].join(',')==='0,1,2,3'&&
            entries.next().value[1]===3&&entries.next().value[1]===4&&entries.next().done;
        values.push(5);
        ok&&entries.next().done&&Array.prototype.keys.length===0&&Array.prototype.entries.name==='entries'&&
            !Object.getOwnPropertyDescriptor(Array.prototype,'entries').enumerable
    )JS");
    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(result.item, ITEM_TRUE);
}

TEST_F(JsFormalMir, HostAndMvpDatesSharePrivateValueStorage) {
    ASSERT_FALSE(item_is_error(run("globalThis.Date")));
    RootFrame roots(2);
    Rooted<Item> host_date(roots, js_date_new_from(flt2it(123.9))), value(roots, ItemNull);
    ASSERT_FALSE(item_is_error(host_date.get()));
    ASSERT_FALSE(item_is_error(js_set_global_property(js_make_string("hostDate"), host_date.get(), 0)));
    Item result = run(R"JS(
        let copy=new Date(hostDate),bad=0;
        Object.freeze(hostDate); hostDate.setTime(456);
        for(let value of [{__time__:123},Object.create(Date.prototype),new Proxy(hostDate,{})]) {
            try { Date.prototype.getTime.call(value); } catch(e) { bad+=e instanceof TypeError; }
        }
        gc();
        hostDate instanceof Date&&hostDate.getTime()===456&&copy.getTime()===123&&bad===3&&
        Reflect.ownKeys(hostDate).length===0&&hostDate.toISOString()==='1970-01-01T00:00:00.456Z';
    )JS");
    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(result.item, ITEM_TRUE);
    value.set(run("class Clock extends Date{};Object.freeze(new Clock(789))"));
    ASSERT_FALSE(item_is_error(value.get()));
    heap_gc_collect();
    EXPECT_EQ(it2d(js_date_method(value.get(), 0)), 789);
    Item stored = js_date_setter(value.get(), 20, flt2it(321), ItemError, ItemError, ItemError);
    ASSERT_FALSE(item_is_error(stored));
    EXPECT_EQ(it2d(stored), 321);
    EXPECT_EQ(it2d(js_date_method(value.get(), 0)), 321);
    host_date.set(js_date_new_from(value.get()));
    EXPECT_EQ(it2d(js_date_method(host_date.get(), 0)), 321);
}

TEST_F(JsFormalMir, SharedDateKernelsRejectPublicAndProxyBrands) {
    ASSERT_FALSE(item_is_error(run("Date")));
    RootFrame roots(1);
    Rooted<Item> value(roots, ItemNull);
    const char* sources[] = {"({__time__:0})", "Object.create(Date.prototype)",
        "new Proxy(new Date(0),{})"};
    for (const char* source : sources) {
        value.set(run(source));
        ASSERT_FALSE(item_is_error(value.get()));
        Item result = js_date_method(value.get(), 0);
        ASSERT_TRUE(item_is_error(result));
        char message[128];
        js_error_lane_format(result, message, sizeof(message));
        EXPECT_NE(strstr(message, "TypeError"), nullptr);
    }
}

TEST_F(JsFormalMir, ArrayKeyAndEntryIteratorsKeepGenericAccessOrder) {
    Item result = run(R"JS(
        let events='',source={get length(){events+='l';return 2},get 0(){events+='v';gc();return 9}};
        let keys=Array.prototype.keys.call(source),entries=Array.prototype.entries.call(source);
        let ok=events===''&&keys.next().value===0&&events==='l';
        let first=entries.next().value;
        ok=ok&&first[0]===0&&first[1]===9&&events==='llv';
        let text=Array.prototype.entries.call('\uD83D\uDE00');
        let a=text.next().value,b=text.next().value;
        let caught=0;
        for(let method of [Array.prototype.keys,Array.prototype.entries])
            for(let value of [null,undefined])try{method.call(value)}catch(e){if(e instanceof TypeError)caught++}
        ok&&a[0]===0&&a[1]==='\uD83D'&&b[0]===1&&b[1]==='\uDE00'&&text.next().done&&caught===4
    )JS");
    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(result.item, ITEM_TRUE);
}

TEST_F(JsFormalMir, ModuleCodeReuseKeepsClosuresClassesAndCachesRealmLocal) {
    const char* path = "temp/js-formal-mir-code-cache.mjs";
    const char* source = "let count=0;export function check(){"
        "class C{#x=40;value(){return this.#x+(++count)}}"
        "let o={text:'abc'};if(!/b/.test(o.text))throw Error('regexp');"
        "gc();return new C().value()}";
    ASSERT_EQ(file_ensure_dir("temp"), 0);
    ASSERT_EQ(write_binary_file(path, source, strlen(source)), 0);
    void* entry = NULL;
    for (int pass = 0; pass < 3; pass++) {
        Item ns = load_js_module(&runtime, path);
        ASSERT_FALSE(item_is_error(ns));
        {
            RootFrame roots(2);
            Rooted<Item> module(roots, ns);
            Rooted<Item> callable(roots, js_get_key_default(module.get(), js_make_string("check")));
            ASSERT_EQ(get_type_id(callable.get()), LMD_TYPE_FUNC);
            EXPECT_TRUE(function_has_abi(callable.get().function, FN_ENTRY_ABI_MVP_LMD));
            if (!pass) entry = (void*)callable.get().function->ptr;
            else EXPECT_EQ((void*)callable.get().function->ptr, entry);
            for (int call = 1; call <= 2; call++) {
                Item value = js_call_function_into(callable.get(), make_js_undefined(), NULL, 0, &result_home);
                ASSERT_FALSE(item_is_error(value));
                EXPECT_EQ(it2d(value), 40 + call);
            }
        }
        runtime_cleanup(&runtime);
        runtime = {};
        runtime_init(&runtime);
    }
}

TEST_F(JsFormalMir, CachedPreambleAndClassicsUseMvpAcrossFreshRealms) {
    struct Images {
        JsPreambleState preamble = {}, classic = {}, consumer = {};
        ~Images() {
            preamble_state_destroy(&consumer);
            preamble_state_destroy(&classic);
            preamble_state_destroy(&preamble);
        }
    } images;
    const char* preamble = "var window=globalThis;let total=40;function next(){return ++total}";
    const char* classic = "function later(){return next()+2} later";
    ASSERT_FALSE(item_is_error(compile_js_mir_preamble_len(&runtime, preamble,
        strlen(preamble), "<cached-preamble>", &images.preamble)));
    ASSERT_NE(images.preamble.mvp_code, nullptr);
    ASSERT_FALSE(item_is_error(compile_js_mir_with_preamble_len(&runtime, classic,
        strlen(classic), "<cached-classic>", &images.preamble, &images.classic)));
    void* entry = NULL;
    for (int pass = 0; pass < 3; pass++) {
        ASSERT_FALSE(item_is_error(instantiate_js_preamble(&runtime, &images.preamble, &images.consumer)));
        Item value = execute_compiled_js_in_current_realm(&runtime, &images.consumer, &images.classic, true);
        ASSERT_EQ(get_type_id(value), LMD_TYPE_FUNC);
        EXPECT_TRUE(function_has_abi(value.function, FN_ENTRY_ABI_MVP_LMD));
        if (!pass) entry = (void*)value.function->ptr;
        else EXPECT_EQ((void*)value.function->ptr, entry);
        {
            RootFrame roots(1);
            Rooted<Item> function(roots, value);
            EXPECT_TRUE(preamble_state_update_from_compiled(&images.consumer, &images.classic));
            const char* inline_source = "total+=1;window.totalSeen=total";
            ASSERT_FALSE(item_is_error(transpile_js_to_mir_with_preamble_len(&runtime,
                inline_source, strlen(inline_source), "<inline-classic>", &images.consumer, &result_home)));
            preamble_state_destroy(&images.consumer);
            if (pass == 2) {
                // escaping functions keep the image after its cache owner retires.
                preamble_state_destroy(&images.preamble);
                preamble_state_destroy(&images.classic);
            }
            heap_gc_collect();
            Item answer = js_call_function_into(function.get(), make_js_undefined(), NULL, 0, &result_home);
            ASSERT_FALSE(item_is_error(answer));
            EXPECT_EQ(it2d(answer), 44);
        }
        runtime_cleanup(&runtime);
        runtime = {};
        runtime_init(&runtime);
    }
}

TEST_F(JsFormalMir, PublicHostPublishesNativeCallbackIntoActiveRealm) {
    ASSERT_FALSE(item_is_error(run("0")));
    RootFrame roots(2);
    Rooted<Item> function(roots, js_new_native_function([]() -> Item {
        heap_gc_collect(); return Item{.item = i2it(42)};
    })), key(roots, js_name_item("collectHost"));
    ASSERT_FALSE(item_is_error(js_set_global_property(key.get(), function.get(), 0)));
    Item result = run("collectHost()");
    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(it2d(result), 42);
}

TEST_F(JsFormalMir, PublicHostReadsTheActiveGlobalAndLexicalEnvironment) {
    Item value = run("let lexical=41;globalThis.visible=42;globalThis");
    ASSERT_FALSE(item_is_error(value));
    EXPECT_EQ(value.item, js_get_global_this().item);
    EXPECT_EQ(it2d(js_get_key_cstr(js_get_global_this(), "visible")), 42);
    Item key = js_name_item("lexical");
    EXPECT_EQ(it2d(js_get_global_property(key)), 41);
    EXPECT_EQ(js_global_binding_exists_after_with_lookup(key), 1);
    EXPECT_EQ(js_get_key_default(js_get_global_this(), key).item, ITEM_JS_UNDEFINED);
    EXPECT_EQ(js_get_global_property(js_name_item("absentHostName")).item, ITEM_JS_UNDEFINED);
}

TEST_F(JsFormalMir, NativeArrayCallablesUseThePublishedAbi) {
    ASSERT_FALSE(item_is_error(run("0")));
    RootFrame roots(3);
    Rooted<Item> array(roots, js_array_new(0)), method(roots, ItemNull), constructor(roots, ItemNull);
    method.set(js_new_native_this_span_function([](Item receiver, Item* args, int count) -> Item {
        RootFrame roots(2);
        Rooted<Item> owner(roots, receiver), value(roots, count ? args[0] : ItemNull);
        heap_gc_collect();
        return count ? value.get() : owner.get();
    }));
    constructor.set(js_new_native_constructor([]() -> Item { return js_new_object(); }));
    js_array_push(array.get(), method.get());
    js_array_push(array.get(), constructor.get());
    js_set_key_cstr(array.get(), "method", method.get());
    ASSERT_FALSE(item_is_error(js_set_global_property(js_name_item("nativeArray"), array.get(), 0)));
    Item result = run(R"JS(
        let receiver={keep:17},values=[5e-324,9007199254740991,'kept'],ok=true;
        for(let value of values) {
            let saved=nativeArray[0](value);gc();ok=ok&&saved===value;
        }
        let ctor=nativeArray[1], instance=new ctor();
        ok&&nativeArray.method()===nativeArray&&nativeArray[0]()===nativeArray&&
        Reflect.apply(nativeArray[0],receiver,[])===receiver&&
        Function.prototype.call.call(nativeArray[0],receiver)===receiver&&
        Function.prototype.bind.call(nativeArray[0],receiver)()===receiver&&
        instance instanceof ctor&&Reflect.construct(ctor,[]) instanceof ctor;
    )JS");
    ASSERT_FALSE(item_is_error(result)); EXPECT_EQ(result.item, ITEM_TRUE);
}

TEST_F(JsFormalMir, HostArrayCompanionPreservesPrototypeAndDescriptors) {
    ASSERT_FALSE(item_is_error(run("globalThis.hostArrayPrototype={get inherited(){return this[0]+1},"
        "[Symbol.toStringTag]:'HostSequence'}")));
    RootFrame roots(2);
    Rooted<Item> array(roots, js_array_new(1)), prototype(roots,
        js_get_global_property(js_name_item("hostArrayPrototype")));
    js_elements_set_int(array.get(), 0, Item{.item = i2it(41)});
    js_set_prototype(array.get(), prototype.get());
    ASSERT_FALSE(item_is_error(js_set_global_property(js_name_item("hostArray"), array.get(), 0)));
    Item result = run(R"JS(
        let before=Object.getPrototypeOf(hostArray)===hostArrayPrototype&&hostArray.inherited===42&&
            ('inherited' in hostArray)&&!hostArray.hasOwnProperty('inherited')&&
            Object.prototype.toString.call(hostArray)==='[object HostSequence]';
        Object.defineProperty(hostArray,'answer',{get(){gc();return this[0]+2},configurable:true});
        let descriptor=Object.getOwnPropertyDescriptor(hostArray,'answer');
        gc();before&&hostArray.answer===43&&descriptor.get.call(hostArray)===43&&
            Reflect.ownKeys(hostArray).includes('answer')&&delete hostArray.answer&&
            !('answer' in hostArray);
    )JS");
    ASSERT_FALSE(item_is_error(result)); EXPECT_EQ(result.item, ITEM_TRUE);
}

TEST_F(JsFormalMir, NativeCallablePropertiesRetainIdentityAndDescriptors) {
    ASSERT_FALSE(item_is_error(run("0")));
    RootFrame roots(3);
    Rooted<Item> host(roots, js_new_native_function([]() -> Item {
        heap_gc_collect(); return Item{.item = i2it(42)};
    })), method(roots, js_new_native_this_span_function(
        [](Item receiver, Item*, int) -> Item { heap_gc_collect(); return receiver; })),
        key(roots, js_name_item("hostCallable"));
    js_set_key_cstr(host.get(), "self", host.get());
    js_set_key_cstr(host.get(), "method", method.get());
    ASSERT_FALSE(item_is_error(js_set_global_property(key.get(), host.get(), 0)));
    Item result = run("hostCallable===hostCallable.self && hostCallable.method()===hostCallable && "
        "Object.getOwnPropertyDescriptor(hostCallable,'method').value===hostCallable.method && "
        "Object.keys(hostCallable).join(',')==='self,method' && hostCallable()===42 && "
        "hostCallable.call===Function.prototype.call && Object.getPrototypeOf(hostCallable)===Function.prototype");
    ASSERT_FALSE(item_is_error(result)); EXPECT_EQ(result.item, ITEM_TRUE);
    result = run("Object.defineProperty(hostCallable,'answer',{get(){gc();return 42},configurable:true});"
        "hostCallable.answer===42 && delete hostCallable.answer && !('answer' in hostCallable)");
    ASSERT_FALSE(item_is_error(result)); EXPECT_EQ(result.item, ITEM_TRUE);
    js_set_key_cstr(host.get(), "late", Item{.item = i2it(7)});
    result = run("hostCallable.late===7 && Reflect.set(hostCallable,'late',9) && hostCallable.late===9");
    ASSERT_FALSE(item_is_error(result)); EXPECT_EQ(result.item, ITEM_TRUE);
    EXPECT_EQ(it2d(js_get_key_cstr(host.get(), "late")), 9);
}

TEST_F(JsFormalMir, NativeObjectsKeepAccessorsAndMvpCallbacks) {
    ASSERT_FALSE(item_is_error(run("0")));
    RootFrame roots(2);
    Rooted<Item> object(roots, js_new_object()), key(roots, js_name_item("hostObject"));
    js_set_key_cstr(object.get(), "value", Item{.item = i2it(40)});
    ASSERT_FALSE(item_is_error(js_set_global_property(key.get(), object.get(), 0)));
    Item result = run("Object.defineProperty(hostObject,'twice',{get(){gc();return this.value*2},enumerable:true});"
        "hostObject.value+=2;Object.values(hostObject).join(',')==='42,84' && "
        "Object.entries(hostObject)[1][1]===84 && Reflect.ownKeys(hostObject).length===2");
    ASSERT_FALSE(item_is_error(result)); EXPECT_EQ(result.item, ITEM_TRUE);
    result = run("let derived=Object.create(hostObject);derived.value=21;"
        "derived.twice===42 && ('twice' in derived) && !('absent' in derived)");
    ASSERT_FALSE(item_is_error(result)); EXPECT_EQ(result.item, ITEM_TRUE);
    result = run("Object.preventExtensions(hostObject);!Object.isExtensible(hostObject) && "
        "!Reflect.set(hostObject,'newValue',1) && hostObject.value===42 && Object.getPrototypeOf(hostObject)===Object.prototype");
    ASSERT_FALSE(item_is_error(result)); EXPECT_EQ(result.item, ITEM_TRUE);
    EXPECT_EQ(it2d(js_get_key_cstr(object.get(), "value")), 42);
}

TEST_F(JsFormalMir, SharedHostReadsMvpCallablePropertiesByAbi) {
    Item value = run("function PublicConstructor(){};PublicConstructor.prototype.answer=42;"
        "Object.defineProperty(PublicConstructor,'observed',{get(){gc();return this.prototype}});"
        "PublicConstructor");
    ASSERT_EQ(get_type_id(value), LMD_TYPE_FUNC);
    RootFrame roots(3);
    Rooted<Item> function(roots, value), prototype(roots, js_get_key_cstr(function.get(), "prototype")),
        observed(roots, js_get_key_cstr(function.get(), "observed"));
    ASSERT_EQ(get_type_id(prototype.get()), LMD_TYPE_MAP);
    EXPECT_EQ(observed.get().item, prototype.get().item);
    EXPECT_EQ(it2d(js_get_key_cstr(prototype.get(), "answer")), 42);
    EXPECT_EQ(it2d(js_get_key_cstr(function.get(), "length")), 0);
    EXPECT_EQ(js_get_key_cstr(function.get(), "absent").item, ITEM_JS_UNDEFINED);
}

TEST_F(JsFormalMir, SharedHostNamespacesAreLazyAndUseTheActiveRealm) {
    Item result = run("let arrayCtor=Array;let namespace=Intl;"
        "let collator=new Intl.Collator('en');let desc=Object.getOwnPropertyDescriptor(globalThis,'Intl');"
        "namespace===Intl && desc.value===namespace && !desc.enumerable && desc.writable && desc.configurable && "
        "collator.compare('a','b')<0 && Object.getPrototypeOf(Intl.Collator)===Function.prototype && "
        "arrayCtor===Array");
    ASSERT_FALSE(item_is_error(result)); EXPECT_EQ(result.item, ITEM_TRUE);
    result = run("delete globalThis.Intl;typeof Intl==='undefined'");
    ASSERT_FALSE(item_is_error(result)); EXPECT_EQ(result.item, ITEM_TRUE);
}

TEST_F(JsFormalMir, SharedBufferGlobalPublishesNativeStatics) {
    Item result = run("let bytesCtor=Uint8Array;let b=Buffer.from([1,2,3]);gc();"
        "Buffer.isBuffer(b) && b.length===3 && b[1]===2 && b.toString('hex')==='010203' && "
        "Buffer.from===Object.getOwnPropertyDescriptor(Buffer,'from').value && bytesCtor===Uint8Array && "
        "new Uint8Array([7])[0]===7 && Object.getOwnPropertyDescriptor(globalThis,'Buffer').enumerable===false");
    ASSERT_FALSE(item_is_error(result)); EXPECT_EQ(result.item, ITEM_TRUE);
}

TEST_F(JsFormalMir, HostConstructorCatalogAndErrorNamesUseActiveRealm) {
    ASSERT_FALSE(item_is_error(run("0")));
    RootFrame roots(2);
    Rooted<Item> fail_type(roots, js_new_native_function([]() -> Item {
        return js_throw_type_error("host receiver");
    })), fail_named(roots, js_new_native_function([]() -> Item {
        RootFrame roots(1);
        Rooted<Item> error(roots, js_new_named_error("TypeError", "host state"));
        // renaming an error cannot change its native class identity.
        js_set_key_cstr(error.get(), "name", js_name_item("InvalidStateError"));
        return js_throw_value(error.get());
    }));
    ASSERT_FALSE(item_is_error(js_set_global_property(js_name_item("failType"), fail_type.get(), 0)));
    ASSERT_FALSE(item_is_error(js_set_global_property(js_name_item("failNamed"), fail_named.get(), 0)));
    Item result = run("let event=new MouseEvent('probe');let typed=false,named=false;"
        "try{failType()}catch(e){gc();typed=e instanceof TypeError && e.name==='TypeError' && e.message==='host receiver'}"
        "try{failNamed()}catch(e){gc();named=e instanceof TypeError && e.name==='InvalidStateError' && e.message==='host state'}"
        "event.type==='probe' && event instanceof Event && event instanceof MouseEvent && typed && named");
    ASSERT_FALSE(item_is_error(result)); EXPECT_EQ(result.item, ITEM_TRUE);
}

TEST_F(JsFormalMir, PreparedHostGlobalsMoveIntoTheMvpRealm) {
    EvalContext* owner = NULL;
    bool reusing = false;
    ASSERT_TRUE(js_prepare_eval_context(&runtime, true, &owner, &reusing));
    ASSERT_TRUE(js_runtime_state_ensure_input(owner));
    RootFrame roots(2);
    Rooted<Item> original(roots, js_get_global_this()),
        function(roots, js_new_native_function([]() -> Item { heap_gc_collect(); return Item{.item = i2it(42)}; }));
    js_set_key_cstr(original.get(), "preparedHost", function.get());
    js_set_key_cstr(original.get(), "preparedValue", Item{.item = i2it(7)});
    js_mark_non_writable(original.get(), js_name_item("preparedValue"));
    js_runtime_state.dom.default_view = original.get();
    Item result = run("let event=new Event('probe');gc();"
        "window===globalThis && self===globalThis && top===globalThis && parent===globalThis && "
        "preparedHost()===42 && preparedValue===7 && !Object.getOwnPropertyDescriptor(globalThis,'preparedValue').writable && "
        "event.type==='probe' && typeof Object==='function'");
    ASSERT_FALSE(item_is_error(result)); EXPECT_EQ(result.item, ITEM_TRUE);
    EXPECT_NE(original.get().item, js_get_global_this().item);
    EXPECT_EQ(js_runtime_state.dom.default_view.item, js_get_global_this().item);
}

TEST_F(JsFormalMir, PublicStructuredCloneUsesSharedKernelAndRealmCarriers) {
    Item result = run(
        "let original={list:[{value:40}],bytes:new Uint8Array([1,2]),"
        "empty:new Uint8Array(0),buffer:new ArrayBuffer(2),"
        "get extra(){gc();return {value:2}}};"
        "let copy=structuredClone(original);copy.bytes[0]=9;gc();"
        "copy!==original&&Object.getPrototypeOf(copy)===Object.prototype&&"
        "Object.getPrototypeOf(copy.list)===Array.prototype&&"
        "copy.list!==original.list&&copy.list[0]!==original.list[0]&&"
        "copy.list[0].value+copy.extra.value===42&&"
        "copy.bytes instanceof Uint8Array&&copy.bytes[0]===9&&original.bytes[0]===1&&"
        "copy.empty!==original.empty&&copy.empty.length===0&&"
        "copy.buffer!==original.buffer&&copy.buffer instanceof ArrayBuffer&&copy.buffer.byteLength===2");
    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(result.item, ITEM_TRUE);
}

TEST_F(JsFormalMir, PublicEntryUsesRetainedInterpreterForDynamicSource) {
    Item result = run("let x=7;function f(){eval('x+=2');return Function('return x')()}f()");
    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(it2d(result), 9);
}

TEST_F(JsFormalMir, PublicEntryReportsSyntaxAndThrownError) {
    Item result = run("throw new TypeError('formal message')");
    ASSERT_TRUE(item_is_error(result));
    char message[128];
    js_error_lane_format(result, message, sizeof(message));
    EXPECT_STREQ(message, "TypeError: formal message");
}

TEST_F(JsFormalMir, PublicEntryRetainsParseError) {
    Item result = run("let = ;");
    ASSERT_TRUE(item_is_error(result));
    char message[128];
    js_error_lane_format(result, message, sizeof(message));
    EXPECT_NE(strstr(message, "SyntaxError"), nullptr);
}

TEST_F(JsFormalMir, PublicEntryDetectsModuleGoal) {
    Item result = run("export const answer=42; if(this!==undefined)throw Error('module receiver');");
    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(result.item, ITEM_JS_UNDEFINED);
}

TEST_F(JsFormalMir, PublicModuleEntryReturnsLiveNamespace) {
    Item value = transpile_js_module_to_mir(&runtime,
        "export let answer=41;export function next(){return ++answer}", "formal-namespace.mjs");
    ASSERT_FALSE(item_is_error(value));
    ASSERT_EQ(get_type_id(value), LMD_TYPE_VMAP);
    RootFrame roots(2);
    Rooted<Item> ns(roots, value), function(roots, js_get_key_cstr(ns.get(), "next"));
    ASSERT_EQ(get_type_id(function.get()), LMD_TYPE_FUNC);
    EXPECT_TRUE(function_has_abi(function.get().function, FN_ENTRY_ABI_MVP_LMD));
    heap_gc_collect();
    Item result = js_call_function_into(function.get(), make_js_undefined(), NULL, 0, &result_home);
    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(it2d(result), 42);
    EXPECT_EQ(it2d(js_get_key_cstr(ns.get(), "answer")), 42);
    EXPECT_EQ(it2d(vmap_get_by_str(ns.get().vmap, "answer")), 42);
    Item repeated = transpile_js_module_to_mir(&runtime,
        "export let answer=41;export function next(){return ++answer}", "formal-namespace.mjs");
    ASSERT_FALSE(item_is_error(repeated));
    EXPECT_EQ(repeated.item, ns.get().item);
    EXPECT_EQ(it2d(vmap_get_by_str(repeated.vmap, "answer")), 42);
}

TEST_F(JsFormalMir, LambdaImportsReuseCanonicalNamespaceAndRootedExportCells) {
    create_dir("temp/mvp_modules");
    write_text_file("temp/mvp_modules/formal-exports.ls",
        "pub fn add(x, y) { x + y }\npub fn data() { {n: 42} }\n"
        "pub fn record() { map([\"tick\",120,\"empty\",null,\"nested\",map([\"height\",2.5])]) }\n"
        "pub pn later(value) { return value + 1 }\n");
    write_text_file("temp/mvp_modules/formal-exports.txt", "null\n");
    write_text_file("temp/mvp_modules/formal-reexports.mjs",
        "export * from './formal-exports.ls';export {add as sum} from './formal-exports.ls';");
    Item result = transpile_js_module_to_mir(&runtime, R"JS(
import {add, data, later, record} from './formal-exports.ls';
import * as ns from './formal-exports.ls';
import {sum} from './formal-reexports.mjs';
gc();
let rejected=false;try{new add(1,2)}catch(e){rejected=e instanceof TypeError}
export const good=add===ns.add&&add===sum&&add.name==='add'&&add.length===2&&
    add(20,22)===42&&data().n===42&&record().tick===120&&record().empty===null&&
    record().missing===undefined&&record().nested.height===2.5&&rejected&&Object.getPrototypeOf(ns)===null&&
    !Object.isExtensible(ns)&&!Reflect.set(ns,'add',1)&&
    Object.keys(ns).join(',')==='add,data,later,record'&&Object.prototype.toString.call(ns)==='[object Module]';
export const pending=later(41).then(value=>{gc();return value===42&&ns.add===add});
export const repeat=import('./formal-exports.ls').then(value=>value===ns&&value.add===add);
)JS", "temp/mvp_modules/formal-entry.mjs");
    ASSERT_FALSE(item_is_error(result));
    RootFrame roots(2);
    Rooted<Item> ns(roots, result), promise(roots, ItemNull);
    EXPECT_EQ(js_get_key_cstr(ns.get(), "good").item, ITEM_TRUE);
    for (const char* name : {"pending", "repeat"}) {
        promise.set(js_get_key_cstr(ns.get(), name));
        ASSERT_TRUE(js_promise_is(promise.get()));
        auto* settled = (JsPromise*)promise.get().vmap;
        ASSERT_EQ(settled->state, JS_PROMISE_FULFILLED);
        EXPECT_EQ(settled->result.item, ITEM_TRUE);
    }
    ModuleDescriptor* foreign = module_get_for_runtime(&runtime, "temp/mvp_modules/formal-exports.ls");
    ASSERT_NE(foreign, nullptr);
    EXPECT_STREQ(foreign->source_lang, "lambda");
    // the Lambda registry retains its Core Function, independently of the JS adapter.
    Item original = module_namespace_get(foreign, "add");
    ASSERT_EQ(get_type_id(original), LMD_TYPE_FUNC);
    EXPECT_TRUE(function_has_abi(original.function, FN_ENTRY_ABI_LAMBDA_BOXED_FUNCTION));
}

TEST_F(JsFormalMir, WarmLambdaImportsValidateExportsBeforeEnteringTheImporter) {
    ASSERT_FALSE(item_is_error(run("globalThis.importEffects=0")));
    RootFrame roots(2);
    Rooted<Item> original(roots, js_load_lambda_module(&runtime, "test/js/_lambda_data_map.ls")), ns(roots, ItemNull);
    ASSERT_FALSE(item_is_error(original.get()));
    Item failed = transpile_js_module_to_mir(&runtime,
        "import {absent} from './_lambda_data_map.ls';globalThis.importEffects++;",
        "test/js/formal-missing-export.mjs");
    ASSERT_TRUE(item_is_error(failed));
    Item effects = run("importEffects");
    ASSERT_FALSE(item_is_error(effects));
    EXPECT_EQ(it2d(effects), 0);
    ns.set(transpile_js_module_to_mir(&runtime,
        "export {data_map as kept} from './_lambda_data_map.ls';", "test/js/formal-warm-export.mjs"));
    ASSERT_FALSE(item_is_error(ns.get()));
    Item exported = js_get_key_cstr(ns.get(), "kept");
    ASSERT_EQ(get_type_id(exported), LMD_TYPE_FUNC);
    EXPECT_TRUE(function_has_abi(exported.function, FN_ENTRY_ABI_MVP_LMD));
    ModuleDescriptor* descriptor = module_get_for_runtime(&runtime, "test/js/_lambda_data_map.ls");
    ASSERT_NE(descriptor, nullptr);
    EXPECT_EQ(descriptor->namespace_obj.item, original.get().item);
}

TEST_F(JsFormalMir, SharedCallCapabilityRecognizesDefaultClassConstructors) {
    Item result = run("class Empty{};class Derived extends Empty{};[Empty,Derived]");
    ASSERT_FALSE(item_is_error(result));
    ASSERT_EQ(get_type_id(result), LMD_TYPE_ARRAY);
    RootFrame roots(3);
    Rooted<Item> constructors(roots, result), constructor(roots, ItemNull), instance(roots, ItemNull);
    for (int index = 0; index < 2; index++) {
        constructor.set(constructors.get().array->items[index]);
        EXPECT_TRUE(js_has_call_capability(constructor.get()));
        EXPECT_TRUE(js_is_callable(constructor.get()));
        instance.set(js_construct_value(constructor.get(), NULL, 0, constructor.get(), NULL, true));
        ASSERT_FALSE(item_is_error(instance.get()));
        EXPECT_EQ(mvp_lmd_instanceof(instance.get(), constructor.get()).item, ITEM_TRUE);
        EXPECT_TRUE(item_is_error(js_call_function_into(constructor.get(), make_js_undefined(), NULL, 0, &result_home)));
    }
}

TEST_F(JsFormalMir, ChildRealmModulesRetainRegistryAndQueuedPolicy) {
    create_dir("temp/mvp_modules");
    write_text_file("temp/mvp_modules/child-identity.js",
        "export const ctor=Array;export const label=await Promise.resolve(globalThis.realmLabel);"
        "export async function again(){return import('./child-identity.js')}"
        "export function fail(){throw new TypeError('child')} ");
    Item result = run(R"JS(
globalThis.realmLabel='parent';
let first=$262.createRealm(),second=$262.createRealm();
first.global.realmLabel='first';second.global.realmLabel='second';
let a=first.evalScript("import('./temp/mvp_modules/child-identity.js')");
let b=second.evalScript("import('./temp/mvp_modules/child-identity.js')");
let p=import('./temp/mvp_modules/child-identity.js');
gc();
Promise.all([a,b,p]).then(async values=>{
    let x=values[0],y=values[1],z=values[2];
    let again=await x.again();
    let correctError=false;try{x.fail()}catch(e){correctError=e instanceof first.global.TypeError&&!(e instanceof TypeError)}
    return x!==y&&y!==z&&again===x&&x.ctor===first.global.Array&&y.ctor===second.global.Array&&z.ctor===Array
        &&x.label==='first'&&y.label==='second'&&z.label==='parent'&&correctError;
})
)JS");
    ASSERT_FALSE(item_is_error(result));
    ASSERT_TRUE(js_promise_is(result));
    auto* promise = (JsPromise*)result.vmap;
    ASSERT_EQ(promise->state, JS_PROMISE_FULFILLED);
    EXPECT_EQ(promise->result.item, ITEM_TRUE);
}

TEST_F(JsFormalMir, UnsetBackendSelectsMvpMir) {
    unsetenv("JS_EXEC_BACKEND");
    Item value = run("function answer(){return 42} answer");
    ASSERT_EQ(get_type_id(value), LMD_TYPE_FUNC);
    EXPECT_TRUE(function_has_abi(value.function, FN_ENTRY_ABI_MVP_LMD));
}

TEST_F(JsFormalMir, ReceiverOnlyEntryDoesNotRequireBuiltinLibrary) {
    Item value = run("this");
    ASSERT_FALSE(item_is_error(value));
    EXPECT_EQ(get_type_id(value), LMD_TYPE_MAP);
}

TEST_F(JsFormalMir, PublicTypedArraysAcceptLambdaBinary) {
    Item value = run("function check(b){let a=new Uint8Array(b),c=new Uint8ClampedArray(b),d=Uint8Array.from(b);"
        "a[0]=17;gc();return a instanceof Uint8Array&&a.buffer instanceof ArrayBuffer&&"
        "c instanceof Uint8ClampedArray&&a[0]===17&&c[0]===222&&d[3]===239}check");
    ASSERT_EQ(get_type_id(value), LMD_TYPE_FUNC);
    RootFrame roots(2);
    Rooted<Item> function(roots, value), source(roots, ItemNull);
    const char bytes[] = {(char)0xde, (char)0xad, (char)0xbe, (char)0xef};
    source.set(Item{.item = x2it(heap_binary_from_bytes(bytes, sizeof(bytes)))});
    Item argument = source.get();
    Item result = js_call_function_into(function.get(), make_js_undefined(), &argument, 1, &result_home);
    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(result.item, ITEM_TRUE);
    EXPECT_EQ(binary_data(source.get().get_safe_binary())[0], 0xde);
}

TEST_F(JsFormalMir, PublicDataViewUsesPhysicalCarrierBrand) {
    Item value = run("new DataView(new Uint8Array([222,173,190,239]).buffer,1,2)");
    ASSERT_FALSE(item_is_error(value));
    RootFrame roots(2);
    Rooted<Item> view(roots, value), binary(roots, ItemNull);
    ASSERT_TRUE(js_is_dataview(view.get()));
    binary.set(fn_binary(view.get()));
    ASSERT_EQ(get_type_id(binary.get()), LMD_TYPE_BINARY);
    ASSERT_EQ(binary_length(binary.get().get_safe_binary()), 2);
    EXPECT_EQ(binary_data(binary.get().get_safe_binary())[0], 0xad);
    EXPECT_EQ(binary_data(binary.get().get_safe_binary())[1], 0xbe);
}

TEST_F(JsFormalMir, PublicHostRetainsBufferAndChildRealmFacilities) {
    Item result = run("let b=new ArrayBuffer(8);$262.detachArrayBuffer(b);"
        "let r=$262.createRealm();b.byteLength===0&&r.global.Array!==Array");
    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(result.item, ITEM_TRUE);
}

TEST_F(JsFormalMir, HostedWithClosuresKeepCapturedObjectsAndUnscopables) {
    Item result = run("let x=3;let f=Function('o', 'with(o){return ()=>x}');"
        "let o={x:7};let g=f(o);gc();let a=g();"
        "o[Symbol.unscopables]={x:true};let b=g();"
        "delete o[Symbol.unscopables];o.x=9;a===7&&b===3&&g()===9");
    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(result.item, ITEM_TRUE);
}

TEST_F(JsFormalMir, DirectEvalWithKeepsLexicalRecordOrder) {
    Item result = run(R"JS(
let x=1;let o={x:7,a:'object'};let f;
with(o){if(eval('x')!==7)throw 1;{let x=9;if(eval('x')!==9)throw 2;}
f=function(a){return eval('a')};}
f('parameter')==='parameter'&&x===1;
    )JS");
    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(result.item, ITEM_TRUE);
}

TEST_F(JsFormalMir, DirectEvalWithRetainsClosuresAndVariableEnvironment) {
    Item result = run(R"JS(
function make(){let o={x:4};let f;with(o){eval('var made=8;x=5');f=eval('(v)=>{x=v;return x+made}');}
return [o,f,()=>made]}
let a=make();gc();a[1](11)===19&&a[0].x===11&&a[2]()===8;
    )JS");
    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(result.item, ITEM_TRUE);
}

TEST_F(JsFormalMir, DirectEvalWithVarInitializersResolveObjectBeforeCallerVar) {
    Item result = run(R"JS(
var x=0;var o={x:2};var before=function(){return x};var during;
with(eval('var x=1'),o)var x=3,unused=during=function(){return x};
function nested(){var o={x:7};var r=eval('var x=2;with(o){x=9;x}');
return r===9&&o.x===9&&x===2}
before()===1&&during()===3&&o.x===3&&nested();
    )JS");
    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(result.item, ITEM_TRUE);
}

TEST_F(JsFormalMir, DirectEvalWithReusesUnscopablesAndStableReferences) {
    Item result = run(R"JS(
let x=3;let o={x:7};let trace='';let p=new Proxy(o,{
has(t,k){if(k==='x')trace+='H';return Reflect.has(t,k)},
get(t,k,r){if(k===Symbol.unscopables)trace+='U';if(k==='x')trace+='G';return Reflect.get(t,k,r)}});
with(p){trace='';if(eval('x')!==7||trace!=='HUHG')throw 1;}
o[Symbol.unscopables]={x:true};with(o){if(eval('x')!==3)throw 2;}
delete o[Symbol.unscopables];with(o){eval('x=(delete o.x,12)');}
o.x===12&&x===3;
    )JS");
    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(result.item, ITEM_TRUE);
}

TEST_F(JsFormalMir, DirectEvalWithCallsKeepObjectReceiver) {
    Item result = run(R"JS(
let o={v:7,f(){return this.v},tag(s){return this.v+s[0].length}};
function mir(){with(o){eval('');return f()+tag`a`}}
function interpreted(){with(o){return eval('f()+tag`a`')}}
mir()===15&&interpreted()===15;
    )JS");
    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(result.item, ITEM_TRUE);
}

TEST_F(JsFormalMir, DirectEvalWithAbruptExitAndTypeofDoNotReprobe) {
    Item result = run(R"JS(
let x=2;let hits=0;let bad={x:7,get [Symbol.unscopables](){throw 19}};let caught=0;
try{with(bad){eval('x')}}catch(e){caught=e}finally{if(eval('x')!==2)throw 1;}
let p=new Proxy({x:7},{has(t,k){if(k==='x')hits++;return Reflect.has(t,k)}});
with(p){if(eval('typeof x')!=='number')throw 2;}
caught===19&&hits===2&&eval('typeof missingWithName')==='undefined';
    )JS");
    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(result.item, ITEM_TRUE);
}

TEST_F(JsFormalMir, HostedWithKeepsNestedLexicalsAndEscapedObjectRecord) {
    Item result = run(R"JS(
let f=Function('o','with(o){{let x=9;if(eval("x")!==9)throw 1;}return [function(x){return eval("x")},()=>eval("x"),()=>delete x]}');
let o={x:7};let a=f(o);gc();let first=a[1]();let removed=a[2]();
a[0](13)===13&&first===7&&removed&&!Object.hasOwn(o,'x');
    )JS");
    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(result.item, ITEM_TRUE);
}

TEST_F(JsFormalMir, HostedWithAnnexBPublishesToVariableRecord) {
    Item result = run(R"JS(
let o={g:1};let f=Function('o','let saved;with(o){{function g(){return 7}}saved=g;}return [saved,g]');
let a=f(o);let after;function caller(g){eval('{function g(){return 9}}after=g')}
caller(1);a[0]===1&&a[1]()===7&&o.g===1&&after()===9;
    )JS");
    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(result.item, ITEM_TRUE);
}

TEST_F(JsFormalMir, UnicodeStringSetsPreferLongestElement) {
    Item result = run("let a=/[\\q{a|ab}]/v.exec('ab')[0];"
        "let b=/[\\q{|a}]b/v.exec('ab')[0];"
        "let c=/[\\q{a|ab}]b/v.exec('ab')[0];a==='ab'&&b==='ab'&&c==='ab'");
    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(result.item, ITEM_TRUE);
}

TEST_F(JsFormalMir, ClosedClassAncestryReachesObjectPrototype) {
    Item result = run("class A{constructor(){this.x=1}}class B extends A{}"
        "class C extends B{hasOwnProperty(k){return k==='override'}}"
        "let b=new B;let c=new C;b.hasOwnProperty('x')&&!b.hasOwnProperty('y')"
        "&&c.hasOwnProperty('override')&&!c.hasOwnProperty('x')");
    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(result.item, ITEM_TRUE);
}

TEST_F(JsFormalMir, NativeFunctionSourceKeepsOriginalName) {
    Item result = run("const show=Function.prototype.toString;"
        "const a=show.call(Object);const b=show.call(DataView);"
        "Object.defineProperty(Object,'name',{get(){throw Error('observable name')}});"
        "a==='function Object() { [native code] }'&&"
        "b==='function DataView() { [native code] }'&&show.call(Object)===a&&"
        "show.call(Object.getOwnPropertyDescriptor(RegExp,'$&').get)==='function () { [native code] }'&&"
        "show.call(DataView.bind(null))==='function () { [native code] }'");
    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(result.item, ITEM_TRUE);
}

TEST_F(JsFormalMir, TemporalDeadZoneReportsBindingName) {
    Item result = run("function f(){try{return later}catch(e){"
        "return e instanceof ReferenceError&&e.message.includes('later')}let later=1}f()");
    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(result.item, ITEM_TRUE);
}

TEST_F(JsFormalMir, RegExpLegacyStateKeepsUtf16CapturesAndDynamicCalls) {
    Item result = run("/(.)(.)/.exec('😀');"
        "const a=RegExp.$1,b=RegExp.$2;gc();"
        "const pairs=a.length===1&&a.charCodeAt(0)===0xd83d&&"
        "b.length===1&&b.charCodeAt(0)===0xde00&&RegExp.input==='😀';"
        "eval('/(漢)/.exec(\"é漢z\")');gc();"
        "pairs&&RegExp.$1==='漢'&&RegExp.lastParen==='漢'&&"
        "RegExp.lastMatch==='漢'&&RegExp.leftContext==='é'&&RegExp.rightContext==='z'&&"
        "RegExp.$2===''&&RegExp['$&']==='漢'&&RegExp['$_']==='é漢z'");
    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(result.item, ITEM_TRUE);
}

TEST_F(JsFormalMir, RegExpLegacyStateIsLocalToChildRealm) {
    Item result = run("/(parent)/.test('parent');"
        "const child=$262.createRealm();child.evalScript('/(child)/.test(\"child\")');gc();"
        "const getter=Object.getOwnPropertyDescriptor(child.global.RegExp,'lastMatch').get;"
        "let rejected=false;try{getter.call(RegExp)}catch(e){rejected=true}"
        "RegExp.lastMatch==='parent'&&child.global.RegExp.lastMatch==='child'&&rejected&&"
        "child.evalScript('RegExp.$1')==='child'");
    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(result.item, ITEM_TRUE);
}

TEST_F(JsFormalMir, RegExpCompileRefreshesUnicodeMatchingMode) {
    Item result = run("const r=/./;r.compile('.','u');"
        "const whole=r.exec('😀')[0];r.compile('.','');"
        "const half=r.exec('😀')[0];"
        "whole==='😀'&&half.length===1&&half.charCodeAt(0)===0xd83d&&"
        "RegExp.rightContext.length===1&&RegExp.rightContext.charCodeAt(0)===0xde00");
    ASSERT_FALSE(item_is_error(result));
    EXPECT_EQ(result.item, ITEM_TRUE);
}

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
    Item module(const char* source, const char* path, MvpLmdFailurePhase* phase = NULL) {
        mvp_lmd_destroy(execution);
        MvpLmdOptions options = {path, true};
        execution = mvp_lmd_execute_with_options(source, strlen(source), NULL, NULL, &options);
        const char* diagnostic = mvp_lmd_diagnostic(execution, phase);
        if (!phase) EXPECT_EQ(diagnostic, nullptr) << source << "\n" << (diagnostic ? diagnostic : "");
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
TEST_F(JsMvpLmd, FullParityReadonlyGlobalAssignments) {
    boolean("let n=0;let a=(undefined=++n),b=(NaN=++n),c=(Infinity=++n);a===1&&b===2&&c===3&&undefined===void 0&&Number.isNaN(NaN)&&Infinity===1/0");
    boolean("undefined++;NaN+=3;Infinity--;undefined===void 0&&Number.isNaN(NaN)&&Infinity===1/0");
    boolean("let n=0;[undefined,NaN,Infinity]=[++n,++n,++n];({x:undefined}={x:++n});n===4&&undefined===void 0&&Number.isNaN(NaN)&&Infinity===1/0");
    boolean("let n=0;while(n<3){NaN=n++;Infinity--}n===3&&Number.isNaN(NaN)&&Infinity===1/0");
    boolean("function f(undefined,NaN,Infinity){undefined++;NaN+=2;[Infinity]=[5];return undefined+NaN+Infinity}f(1,2,3)===11");
    boolean("'use strict';let n=0,caught=0;try{undefined=++n}catch(e){if(e instanceof TypeError)caught++}try{NaN=++n}catch(e){if(e instanceof TypeError)caught++}try{Infinity=++n}catch(e){if(e instanceof TypeError)caught++}n===3&&caught===3");
    boolean("'use strict';let caught=0;try{NaN++}catch(e){if(e instanceof TypeError)caught++}try{[undefined]=[1]}catch(e){if(e instanceof TypeError)caught++}try{({x:Infinity}={x:1})}catch(e){if(e instanceof TypeError)caught++}caught===3");
}
TEST_F(JsMvpLmd, FullParityAtomicStorageAndCoercion) {
    boolean("let a=new Int8Array(2);Atomics.store(a,1,-128);let old=Atomics.add(a,1,-1);old===-128&&Atomics.load(a,1)===127");
    boolean("let a=new Uint32Array(1);Atomics.store(a,0,4294967295);Atomics.add(a,0,2)===4294967295&&a[0]===1&&Atomics.compareExchange(a,0,1,7)===1&&Atomics.exchange(a,0,2)===7&&Atomics.sub(a,0,1)===2");
    boolean("let a=new Int32Array(new SharedArrayBuffer(8)),b=new Int32Array(a.buffer,4);Atomics.store(b,0,0);Atomics.load(a,1)===0&&Atomics.compareExchange(b,0,1,9)===0&&Atomics.load(a,1)===0");
    boolean("let a=new Uint8Array(1);Atomics.store(a,0,15);Atomics.and(a,0,6)===15&&Atomics.or(a,0,8)===6&&Atomics.xor(a,0,7)===14&&a[0]===9");
    boolean("let a=new BigInt64Array(1);Atomics.store(a,0,9223372036854775808n)===9223372036854775808n&&Atomics.add(a,0,1n)===-9223372036854775808n&&a[0]===-9223372036854775807n");
    boolean("let a=new Int8Array(1),order='';let old=Atomics.compareExchange(a,{valueOf(){order+='i';return 0}},{valueOf(){order+='e';return 0}},{valueOf(){order+='r';return 3}});old===0&&a[0]===3&&order==='ier'");
    boolean("let a=new Int8Array(1);Object.is(Atomics.store(a,0,-0),0)&&Atomics.store(a,0,257.75)===257&&a[0]===1");
    error("Atomics.add(new Float64Array(1),0,1)", "TypeError");
    error("Atomics.load(new Int32Array(1),1)", "RangeError");
    error("Atomics.store(new BigInt64Array(1),0,1)", "TypeError");
    error("Atomics.store(new Int32Array(1),0,1n)", "TypeError");
}
TEST_F(JsMvpLmd, FullParityAtomicBufferRevalidation) {
    boolean("let b=new ArrayBuffer(8,{maxByteLength:16}),a=new Int32Array(b);let failed=false;try{Atomics.add(a,1,{valueOf(){b.resize(4);return 1}})}catch(e){failed=e instanceof RangeError}failed&&a[0]===0");
    boolean("let b=new ArrayBuffer(8,{maxByteLength:16}),a=new Int32Array(b,0,2);let failed=false;try{Atomics.load(a,{valueOf(){b.resize(4);return 0}})}catch(e){failed=e instanceof TypeError}failed");
    boolean("let b=new ArrayBuffer(8,{maxByteLength:16}),a=new Int32Array(b);let old=Atomics.add(a,1,{valueOf(){b.resize(16);return 7}});old===0&&a[1]===7&&a.length===4");
    boolean("let b=new ArrayBuffer(8,{maxByteLength:16}),a=new Int32Array(b),seen=false;try{Atomics.load(a,{valueOf(){b.resize(16);return 3}})}catch(e){seen=e instanceof RangeError}seen");
    boolean("let b=new ArrayBuffer(8),a=new Int32Array(b),order='';try{Atomics.compareExchange(a,0,{valueOf(){b.transfer();order+='e';return 0}},{valueOf(){order+='r';return 1}})}catch(e){order+=e instanceof TypeError?'t':'x'}order==='ert'");
}
TEST_F(JsMvpLmd, FullParityAtomicNamespace) {
    boolean("Object.getPrototypeOf(Atomics)===Object.prototype&&Object.prototype.toString.call(Atomics)==='[object Atomics]'&&Atomics.compareExchange.length===4&&Atomics.load.name==='load'&&!Object.getOwnPropertyDescriptor(Atomics,'load').enumerable&&Atomics.load.prototype===undefined");
    boolean("Atomics.isLockFree({valueOf(){return 4.5}})&&!Atomics.isLockFree(1e300)&&!Atomics.isLockFree(NaN)&&Atomics.pause(-2)===undefined&&Atomics.pause()===undefined");
    error("Atomics.pause({valueOf(){return 1}})", "TypeError");
    error("Atomics.pause(1.5)", "TypeError");
    error("new Atomics.add()", "TypeError");
}
TEST_F(JsMvpLmd, FullParityAtomicBlockingValidationAndDeadline) {
    boolean("let a=new Int32Array(new SharedArrayBuffer(8)),order='';let r=Atomics.wait(a,{valueOf(){order+='i';return 0}},{valueOf(){order+='v';return 1}},{valueOf(){order+='t';return 0}});order==='ivt'&&r==='not-equal'");
    boolean("let a=new BigInt64Array(new SharedArrayBuffer(8));Atomics.wait(a,0,0n,-1)==='timed-out'&&Atomics.wait(a,0,1n)==='not-equal'");
    error("Atomics.wait(new Int32Array(1),0,0,0)", "TypeError");
    error("Atomics.wait(new Uint32Array(new SharedArrayBuffer(4)),0,0,0)", "TypeError");
    error("Atomics.wait(new BigInt64Array(new SharedArrayBuffer(8)),0,0,0)", "TypeError");
    error("Atomics.wait(new Int32Array(new SharedArrayBuffer(4)),0,0,Symbol())", "TypeError");
    MvpLmdHost host = {0, NULL, NULL, true};
    Item result = run("let a=new Int32Array(new SharedArrayBuffer(4)),events=[];Promise.resolve().then(()=>events.push('promise'));setTimeout(()=>events.push('timer'),0);let before=performance.now();let status=Atomics.wait(a,0,0,2.5);events.push(status==='timed-out'&&performance.now()-before>=2.5&&events.length===0);events", &host);
    ASSERT_EQ(get_type_id(result), LMD_TYPE_ARRAY);
    ASSERT_EQ(result.array->length, 3);
    EXPECT_EQ(result.array->items[0].item, ITEM_TRUE);
}
TEST_F(JsMvpLmd, FullParityAtomicCannotSuspendValidationOrder) {
    MvpLmdHost host = {0, NULL, NULL, true};
    Item result = run("globalThis.__lambda_can_block=false;let a=new Int32Array(new SharedArrayBuffer(4)),n=0,order='';try{Atomics.wait(a,{valueOf(){order+='i';return 0}},{valueOf(){order+='v';return 1}},{valueOf(){order+='t';return 0}})}catch(e){if(e instanceof TypeError)n++}try{Atomics.wait(a,1,0,0)}catch(e){if(e instanceof RangeError)n++}try{Atomics.wait(a,0,{valueOf(){throw 7}},0)}catch(e){if(e===7)n++}n===3&&order==='ivt'&&!Atomics.waitAsync(a,0,0,0).async", &host);
    EXPECT_EQ(result.item, ITEM_TRUE);
}
TEST_F(JsMvpLmd, FullParityAtomicAsyncValidationAndCoercion) {
    boolean("let a=new Int32Array(new SharedArrayBuffer(8)),order='';let r=Atomics.waitAsync(a,{valueOf(){order+='i';return 0}},{valueOf(){order+='v';return 1}},{valueOf(){order+='t';return 0}});order==='ivt'&&!r.async&&r.value==='not-equal'&&Object.getPrototypeOf(r)===Object.prototype&&Object.keys(r).join(',')==='async,value'");
    boolean("let a=new BigInt64Array(new SharedArrayBuffer(8));let r=Atomics.waitAsync(a,0,0n,-1);!r.async&&r.value==='timed-out'&&Atomics.notify(a,0)===0");
    boolean("let b=new SharedArrayBuffer(8,{maxByteLength:16}),a=new Int32Array(b),caught=false;try{Atomics.waitAsync(a,{valueOf(){b.grow(16);return 3}},0,0)}catch(e){caught=e instanceof RangeError}caught&&a.length===4");
    boolean("let b=new SharedArrayBuffer(8,{maxByteLength:16}),a=new Int32Array(b);let r=Atomics.waitAsync(a,1,{valueOf(){b.grow(16);return 1}},0);r.value==='not-equal'&&a.length===4");
    boolean("let order='',a=new Int32Array(1);Atomics.notify(a,{valueOf(){order+='i';return 0}},{valueOf(){order+='c';return 1}})===0&&order==='ic'");
    error("Atomics.waitAsync(new Int32Array(1),0,0,0)", "TypeError");
    error("Atomics.waitAsync(new Uint32Array(new SharedArrayBuffer(4)),0,0,0)", "TypeError");
    error("Atomics.waitAsync(new BigInt64Array(new SharedArrayBuffer(8)),0,0,0)", "TypeError");
    error("Atomics.waitAsync(new Int32Array(new SharedArrayBuffer(4)),0,0,Symbol())", "TypeError");
    error("Atomics.notify(new Int32Array(new SharedArrayBuffer(4)),0,1n)", "TypeError");
}
TEST_F(JsMvpLmd, FullParityAtomicAsyncNotifyAndLifetime) {
    const char* sources[] = {
        "let a=new Int32Array(new SharedArrayBuffer(8)),b=new Int32Array(a.buffer,4),out=[];let r=Atomics.waitAsync(b,0,0);out.push(r.async&&r.value instanceof Promise);r.value.then(v=>out.push(v==='ok'));out.push(Atomics.notify(a,0)===0&&Atomics.notify(a,1)===1&&Atomics.notify(b,0)===0);out",
        "let b=new SharedArrayBuffer(16),a=new Int32Array(b),c=new BigInt64Array(b,8),out=[];let r=Atomics.waitAsync(c,0,0n);out.push(r.async);r.value.then(v=>out.push(v==='ok'));out.push(Atomics.notify(a,2)===1);out",
        "let a=new Int32Array(new SharedArrayBuffer(4)),out=[],order='';let p=Atomics.waitAsync(a,0,0).value,q=Atomics.waitAsync(a,0,0).value;p.then(()=>order+='p');q.then(()=>order+='q');out.push(Atomics.notify(a,0,1)===1);let r=Atomics.waitAsync(a,0,0).value;r.then(()=>{order+='r';out.push(order==='pqr')});out.push(Atomics.notify(a,0,1)===1);Atomics.notify(a,0);out",
        "let a=new Int32Array(new SharedArrayBuffer(4)),out=[];let p=Atomics.waitAsync(a,0,0,1000000).value;p.then(v=>out.push(v==='ok'));out.push(Atomics.notify(a,0,0)===0);out.push(Atomics.notify(a,0,1)===1);out"
    };
    for (const char* source : sources) {
        Item result = run(source);
        ASSERT_EQ(get_type_id(result), LMD_TYPE_ARRAY) << source;
        ASSERT_EQ(result.array->length, 3) << source;
        for (int i = 0; i < 3; i++) EXPECT_EQ(result.array->items[i].item, ITEM_TRUE) << source << i;
    }
}
TEST_F(JsMvpLmd, FullParityAtomicAsyncDeadlineWithoutHostTimers) {
    Item result = run("let a=new Int32Array(new SharedArrayBuffer(4)),out=[];let r=Atomics.waitAsync(a,0,0,0.25);out.push(r.async);r.value.then(v=>out.push(v==='timed-out'));out");
    ASSERT_EQ(get_type_id(result), LMD_TYPE_ARRAY);
    ASSERT_EQ(result.array->length, 2);
    EXPECT_EQ(result.array->items[0].item, ITEM_TRUE);
    EXPECT_EQ(result.array->items[1].item, ITEM_TRUE);
}
TEST_F(JsMvpLmd, FullParityAtomicCoercionOwnsBorrowedScalar) {
    Item array = run("new Int32Array(new SharedArrayBuffer(4))");
    RootFrame roots(1);
    ASSERT_TRUE(roots.valid());
    Rooted<Item> owner(roots, array);
    double borrowed = 0;
    static double* callback_home;
    callback_home = &borrowed;
    JsBufferViewOps ops = mvp_lmd_buffer_ops;
    ops.number = [](Item value) {
        // model a native callback overwriting the caller's transient scalar result home.
        if (get_type_id(value) == LMD_TYPE_INT) *callback_home = 7;
        return value;
    };
    bool asynchronous = false;
    Item result = js_atomics_wait_with_ops(owner.get(), Item{.item = i2it(0)},
        Item{.item = d2it(&borrowed)}, Item{.item = i2it(0)}, &ops, true, true, &asynchronous);
    ASSERT_EQ(get_type_id(result), LMD_TYPE_STRING);
    EXPECT_FALSE(asynchronous);
    EXPECT_STREQ(result.get_string()->chars, "timed-out");
    borrowed = 3;
    result = js_atomics_operation_with_ops(JS_ATOMICS_OP_STORE, owner.get(), Item{.item = i2it(0)},
        Item{.item = d2it(&borrowed)}, ItemNull, &ops);
    callback_home = NULL;
    ASSERT_TRUE(get_type_id(result) == LMD_TYPE_INT || get_type_id(result) == LMD_TYPE_FLOAT);
    EXPECT_DOUBLE_EQ(it2d(result), 3);
}
TEST_F(JsMvpLmd, FullParityAtomicAsyncTimeoutAndRealm) {
    MvpLmdHost host = {0, NULL, NULL, true};
    Item result = run("let g=$262.createRealm().global,a=new Int32Array(new SharedArrayBuffer(4)),out=[];let r=g.Atomics.waitAsync(a,0,0,1);out.push(Object.getPrototypeOf(r)===g.Object.prototype&&Object.getPrototypeOf(r.value)===g.Promise.prototype);r.value.then(v=>{out.push(v==='timed-out');out.push(Atomics.notify(a,0)===0)});out", &host);
    ASSERT_EQ(get_type_id(result), LMD_TYPE_ARRAY);
    ASSERT_EQ(result.array->length, 3);
    for (int i = 0; i < 3; i++) EXPECT_EQ(result.array->items[i].item, ITEM_TRUE) << i;
}
TEST_F(JsMvpLmd, FullParityWeakLifetimeConstructorsAndBrands) {
    boolean("let o={},w=new WeakRef(o);w.deref()===o&&Object.getPrototypeOf(w)===WeakRef.prototype&&Object.keys(w).length===0&&Object.prototype.toString.call(w)==='[object WeakRef]'");
    boolean("let r=new FinalizationRegistry(function(){}),o={},t={};r.register(o,3,t);r.register(o,4,t);r.unregister(t)&&!r.unregister(t)&&Object.keys(r).length===0&&Object.prototype.toString.call(r)==='[object FinalizationRegistry]'");
    boolean("class W extends WeakRef{}class F extends FinalizationRegistry{}let o={},w=new W(o),r=new F(()=>{});w instanceof W&&w instanceof WeakRef&&w.deref()===o&&r instanceof F&&r instanceof FinalizationRegistry");
    boolean("let n=0,C=new Proxy(function(){},{get(t,k){if(k==='prototype'){n++;throw 9}return t[k]}}),ok=0;try{Reflect.construct(WeakRef,[1],C)}catch(e){if(e instanceof TypeError)ok++}try{Reflect.construct(FinalizationRegistry,[1],C)}catch(e){if(e instanceof TypeError)ok++}n===0&&ok===2");
    boolean("let o={},w=new WeakRef(o);for(let i=0;i<2000;i++)({i});w.deref()===o");
    error("WeakRef({})", "TypeError");
    error("FinalizationRegistry(()=>{})", "TypeError");
    error("WeakRef.prototype.deref.call(Object.create(WeakRef.prototype))", "TypeError");
    error("FinalizationRegistry.prototype.register.call({}, {}, 1)", "TypeError");
    error("let r=new FinalizationRegistry(()=>{}),o={};r.register(o,o)", "TypeError");
    error("new WeakRef(Symbol.for('registered'))", "TypeError");
    boolean("let s=Symbol(),w=new WeakRef(s);w.deref()===s");
}
TEST_F(JsMvpLmd, FullParityWeakRefCollectionAndKeptObjects) {
    Item result = run("new WeakRef({value:7})");
    RootFrame roots(2);
    ASSERT_TRUE(roots.valid());
    auto deref = [](Item value) { return js_weak_method_with_ops(JS_BUILTIN_WEAKREF_DEREF, value, NULL, 0, NULL); };
    Rooted<Item> owner(roots, result), target(roots, deref(result));
    ASSERT_EQ(get_type_id(target.get()), LMD_TYPE_MAP);
    target.set(ItemNull);
    {
        JsOwnedCallActivation nested_call;
        ASSERT_FALSE(item_is_error(js_microtask_flush_result()));
        heap_gc_collect();
        EXPECT_EQ(get_type_id(deref(owner.get())), LMD_TYPE_MAP);
    }
    // deref keeps the target alive until the next host job checkpoint.
    heap_gc_collect();
    EXPECT_EQ(get_type_id(deref(owner.get())), LMD_TYPE_MAP);
    ASSERT_FALSE(item_is_error(js_weak_job_checkpoint()));
    heap_gc_collect();
    EXPECT_EQ(get_type_id(deref(owner.get())), LMD_TYPE_UNDEFINED);
}
TEST_F(JsMvpLmd, FullParityFinalizationCleanupAndScalarOwnership) {
    Item result = run("let values=[];let r=new FinalizationRegistry(v=>values.push(v));let o={};r.register(o,5e-324);let w=new WeakRef(o);o=null;[r,values,w]");
    ASSERT_EQ(get_type_id(result), LMD_TYPE_ARRAY);
    RootFrame roots(1);
    ASSERT_TRUE(roots.valid());
    Rooted<Item> owner(roots, result);
    heap_gc_collect();
    EXPECT_EQ(owner.get().array->items[1].array->length, 0);
    ASSERT_FALSE(item_is_error(js_weak_job_checkpoint()));
    ASSERT_EQ(owner.get().array->items[1].array->length, 1);
    EXPECT_EQ(owner.get().array->items[1].array->items[0].get_double(), 5e-324);
    heap_gc_collect();
    ASSERT_FALSE(item_is_error(js_weak_job_checkpoint()));
    EXPECT_EQ(owner.get().array->items[1].array->length, 1);
}
TEST_F(JsMvpLmd, FullParityFinalizationUnregisterAndStrongHoldings) {
    Item result = run("let values=[],token={};let r=new FinalizationRegistry(v=>values.push(v));r.register({},1,token);r.unregister(token);function add(){let o={};r.register(o,{o})}add();[r,values]");
    ASSERT_EQ(get_type_id(result), LMD_TYPE_ARRAY);
    RootFrame roots(1);
    ASSERT_TRUE(roots.valid());
    Rooted<Item> owner(roots, result);
    heap_gc_collect();
    ASSERT_FALSE(item_is_error(js_weak_job_checkpoint()));
    EXPECT_EQ(owner.get().array->items[1].array->length, 0);
}
TEST_F(JsMvpLmd, FullParityHostScriptPersistentDeclarations) {
    MvpLmdHost host = {0, NULL, NULL, true};
    const char* sources[] = {
        "$262.evalScript('let later=1;const fixed=7;class C{}');$262.evalScript('later+=2');later===3&&fixed===7&&typeof C==='function'&&!Object.hasOwn(globalThis,'later')&&!(delete later)",
        "$262.evalScript('var scriptVar=4;function scriptFn(){return scriptVar}');let d=Object.getOwnPropertyDescriptor(globalThis,'scriptVar');!d.configurable&&d.writable&&d.enumerable&&scriptFn()===4&&$262.evalScript('scriptVar')===4",
        "$262.evalScript('let x=5');let n=0;try{$262.evalScript('var leak;let x')}catch(e){if(e instanceof SyntaxError)n++}try{(0,eval)('var x')}catch(e){if(e instanceof SyntaxError)n++}n===2&&typeof leak==='undefined'&&x===5",
        "$262.evalScript('let x=1;function read(){return x}');x=5;read()===5&&$262.evalScript('x')===5",
        "let seen=0;try{$262.evalScript('throw 9;let forever')}catch(e){if(e===9)seen++}try{typeof forever}catch(e){if(e instanceof ReferenceError)seen++}seen===2",
        "$262.evalScript('\"use strict\";var strictVar=7;let strictLex=8');strictVar===7&&strictLex===8&&!Object.getOwnPropertyDescriptor(globalThis,'strictVar').configurable",
        "$262.evalScript('const fixed=7');let yes=false;try{fixed=8}catch(e){yes=e instanceof TypeError}yes&&fixed===7",
        "$262.evalScript('let outer=2');$262.evalScript('$262.evalScript(\"let inner=3\");outer+=inner');outer===5&&inner===3",
        "(0,eval)('let privateLex=3');$262.evalScript('let privateLex=4');privateLex===4",
        "Object.defineProperty(globalThis,'f',{value:7,writable:true,configurable:true});$262.evalScript('{function f(){return 8}}');let d=Object.getOwnPropertyDescriptor(globalThis,'f');f()===8&&!d.enumerable&&d.configurable"
    };
    for (const char* source : sources) {
        Item value = run(source, &host);
        ASSERT_EQ(value.item, ITEM_TRUE) << source;
    }
}
TEST_F(JsMvpLmd, FullParityHostedRealmIdentity) {
    MvpLmdHost host = {0, NULL, NULL, true};
    const char* sources[] = {
        "let r=$262.createRealm(),g=r.global;g!==globalThis&&g.globalThis===g&&g.Object!==Object&&g.Array!==Array&&g.TypeError!==TypeError&&g.Function!==Function&&g.Symbol.for('shared')===Symbol.for('shared')",
        "let r=$262.createRealm();r.evalScript('var realmVar=4;let realmLex=5');r.evalScript('realmLex+=2');r.global.realmVar===4&&r.evalScript('realmLex')===7&&typeof realmVar==='undefined'&&typeof realmLex==='undefined'&&!Object.hasOwn(r.global,'realmLex')",
        "let rootArray=[],r=$262.createRealm(),g=r.global,a=r.evalScript('[1,2]');Object.getPrototypeOf(rootArray)===Array.prototype&&g.Object.getPrototypeOf(rootArray)===Array.prototype&&Object.getPrototypeOf(a)===g.Array.prototype&&Object.getPrototypeOf(g.Object.prototype)===null&&Object.getPrototypeOf(g.Function.prototype)===g.Object.prototype",
        "let r=$262.createRealm(),g=r.global,f=g.Function('return this');f()===g&&Object.getPrototypeOf(f)===g.Function.prototype&&Object.getPrototypeOf(f.prototype)===g.Object.prototype",
        "let r=$262.createRealm(),g=r.global;g.Object.preventExtensions(g.Object.prototype);!Object.isExtensible(g.Object.prototype)&&Object.isExtensible(Object.prototype)",
        "let r=$262.createRealm(),g=r.global,n=new g.Number(7),d=new g.Date(9),re=new g.RegExp('x','g');Number.prototype.valueOf.call(n)===7&&Date.prototype.getTime.call(d)===9&&RegExp.prototype.exec.call(re,'x')[0]==='x'"
    };
    for (const char* source : sources) {
        Item value = run(source, &host);
        EXPECT_EQ(value.item, ITEM_TRUE) << source;
    }
}
TEST_F(JsMvpLmd, FullParityHostedRealmErrorsAndSuspension) {
    MvpLmdHost host = {0, NULL, NULL, true};
    const char* sources[] = {
        "let r=$262.createRealm(),g=r.global,ok=false;try{g.Object.defineProperty(null,'x',{})}catch(e){ok=e instanceof g.TypeError&&!(e instanceof TypeError)}ok",
        "let r=$262.createRealm(),g=r.global,f=g.Function('return class {#x=1;read(o){return o.#x}}'),C=f(),D=f(),ok=false;try{new C().read(new D())}catch(e){ok=e instanceof g.TypeError&&!(e instanceof TypeError)}ok&&new C().read(new C())===1",
        "let r=$262.createRealm(),g=r.global;r.global.callback=()=>{throw new TypeError('caller')};let ok=false;try{g.Function('callback()')()}catch(e){ok=e instanceof TypeError&&!(e instanceof g.TypeError)}ok",
        "let r=$262.createRealm(),g=r.global,f=r.evalScript('(function*(){yield [];return []})'),it=f();Object.getPrototypeOf(it.next().value)===g.Array.prototype&&Object.getPrototypeOf(it.next().value)===g.Array.prototype&&Object.getPrototypeOf([])===Array.prototype",
        "let r=$262.createRealm(),g=r.global,f=r.evalScript('(function*(){yield 1;throw new TypeError()})'),it=f();it.next();let ok=false;try{it.next()}catch(e){ok=e instanceof g.TypeError&&!(e instanceof TypeError)}ok"
    };
    for (const char* source : sources) {
        Item value = run(source, &host);
        EXPECT_EQ(value.item, ITEM_TRUE) << source;
    }
}
TEST_F(JsMvpLmd, FullParityHostedRealmPromises) {
    MvpLmdHost host = {0, NULL, NULL, true};
    const char* sources[] = {
        "let r=$262.createRealm(),g=r.global,out=[false,false];let p=r.evalScript('(async function(){await 0;return []})')();out[0]=Object.getPrototypeOf(p)===g.Promise.prototype;p.then(a=>{out[1]=Object.getPrototypeOf(a)===g.Array.prototype});out",
        "let r=$262.createRealm(),g=r.global,out=[false,false];out[0]=Object.getPrototypeOf(Promise.resolve())===Promise.prototype;g.Promise.resolve(1).then(g.Function('return []')).then(a=>{out[1]=Object.getPrototypeOf(a)===g.Array.prototype});out",
        "let r=$262.createRealm(),g=r.global,out=[false,false];g.out=out;Promise.resolve().then(g.Function('out[0]=Object.getPrototypeOf(Promise.resolve())===Promise.prototype')).then(()=>{out[1]=Object.getPrototypeOf(Promise.resolve())===Promise.prototype});out",
        "let out=[false,false];(async function(){await 0;let r=$262.createRealm();out[0]=r.global.Array!==Array;out[1]=Object.getPrototypeOf(r.evalScript('[]'))===r.global.Array.prototype})();out"
    };
    for (const char* source : sources) {
        Item value = run(source, &host);
        ASSERT_EQ(get_type_id(value), LMD_TYPE_ARRAY) << source;
        ASSERT_EQ(value.array->length, 2) << source;
        EXPECT_EQ(value.array->items[0].item, ITEM_TRUE) << source;
        EXPECT_EQ(value.array->items[1].item, ITEM_TRUE) << source;
    }
}
TEST_F(JsMvpLmd, FullParityHostedRealmConstructorFallback) {
    MvpLmdHost host = {0, NULL, NULL, true};
    const char* sources[] = {
        "let g=$262.createRealm().global,F=g.Function();F.prototype=null;let names=['Object','Array','Number','String','Boolean','Date','RegExp','Map','Set','WeakMap','WeakSet','Error','TypeError','ArrayBuffer','SharedArrayBuffer','Uint8Array','BigInt64Array'];let ok=true;for(let name of names){let a=Reflect.construct(globalThis[name],[],F);ok=ok&&Object.getPrototypeOf(a)===g[name].prototype}ok",
        "let g=$262.createRealm().global,F=g.Function();F.prototype=1;let args=[{},()=>{},()=>{}],names=['WeakRef','FinalizationRegistry','Promise'];let ok=true;for(let i=0;i<names.length;i++)ok=ok&&Object.getPrototypeOf(Reflect.construct(globalThis[names[i]],[args[i]],F))===g[names[i]].prototype;ok",
        "let g=$262.createRealm().global,F=g.Function(),B=F.bind(null),P=new Proxy(F,{});F.prototype=null;Object.defineProperty(B,'prototype',{value:null});Object.getPrototypeOf(Reflect.construct(Number,[],B))===g.Number.prototype&&Object.getPrototypeOf(Reflect.construct(Date,[],P))===g.Date.prototype",
        "let g=$262.createRealm().global,F=g.Function();F.prototype=null;let texts=['function(){}','function*(){}','async function(){}','async function*(){}'],ok=true;for(let t of texts){let C=eval('('+t+').constructor'),P=g.eval('('+t+').constructor.prototype');ok=ok&&Object.getPrototypeOf(Reflect.construct(C,[],F))===P}ok",
        "let g=$262.createRealm().global,a=new g.Array(1),seen=0;Object.defineProperty(g.Array,Symbol.species,{get(){seen++;throw 7}});let b=Array.prototype.slice.call(a);seen===0&&Object.getPrototypeOf(b)===Array.prototype&&Object.getPrototypeOf(g.Array.prototype.slice.call([]))===g.Array.prototype"
    };
    for (const char* source : sources) {
        Item value = run(source, &host);
        EXPECT_EQ(value.item, ITEM_TRUE) << source;
    }
}
TEST_F(JsMvpLmd, FullParityHostedRealmBuiltinArrays) {
    MvpLmdHost host = {0, NULL, NULL, true};
    const char* sources[] = {
        "let g=$262.createRealm().global,o={x:1};let a=g.Object.keys(o),b=g.Object.values(o),c=g.Object.entries(o),d=g.Reflect.ownKeys(o);[a,b,c,c[0],d].every(v=>Object.getPrototypeOf(v)===g.Array.prototype)",
        "let g=$262.createRealm().global,s=g.String.prototype.split;let a=s.call('a,b',','),b=s.call('a,b',',',1),c=s.call('ab',''),d=s.call('abc',undefined);[a,b,c,d].every(v=>Object.getPrototypeOf(v)===g.Array.prototype)",
        "let g=$262.createRealm().global,m=new g.Map([[1,2]]),s=new g.Set([1]);Object.getPrototypeOf(m.entries().next().value)===g.Array.prototype&&Object.getPrototypeOf(s.entries().next().value)===g.Array.prototype",
        "let g=$262.createRealm().global,re=new g.RegExp('x');Object.prototype.toString.call(re)==='[object RegExp]'&&g.Object.prototype.toString.call(/x/)==='[object RegExp]'",
        "class A extends Array{}class B extends A{}let a=new B(2);Array.isArray(a)&&a.length===2&&a instanceof B&&Object.getPrototypeOf(a)===B.prototype"
    };
    for (const char* source : sources) {
        Item value = run(source, &host);
        EXPECT_EQ(value.item, ITEM_TRUE) << source;
    }
    Item result = run("let g=$262.createRealm().global,out=[false,false,false];g.Promise.all([1,2]).then(a=>out[0]=Object.getPrototypeOf(a)===g.Array.prototype);g.Promise.allSettled([1]).then(a=>out[1]=Object.getPrototypeOf(a)===g.Array.prototype&&Object.getPrototypeOf(a[0])===g.Object.prototype);g.Promise.any([]).catch(e=>out[2]=Object.getPrototypeOf(e.errors)===g.Array.prototype);out", &host);
    ASSERT_EQ(get_type_id(result), LMD_TYPE_ARRAY);
    ASSERT_EQ(result.array->length, 3);
    for (int i = 0; i < 3; i++) EXPECT_EQ(result.array->items[i].item, ITEM_TRUE) << i;
}
TEST_F(JsMvpLmd, FullParityHostedRealmInheritance) {
    MvpLmdHost host = {0, NULL, NULL, true};
    const char* sources[] = {
        "let g=$262.createRealm().global;class A extends g.Array{}class B extends A{}let a=new B(2);Array.isArray(a)&&a.length===2&&a instanceof B&&a instanceof g.Array&&Object.getPrototypeOf(a)===B.prototype&&Object.getPrototypeOf(A)===g.Array",
        "let r=$262.createRealm(),g=r.global,C=r.evalScript('(class {#x=7;m(){return this.#x}array(){return []}})');class D extends C{}let d=new D();d.m()===7&&d instanceof C&&d instanceof D&&Object.getPrototypeOf(d.array())===g.Array.prototype",
        "let g=$262.createRealm().global;class D extends g.Date{}class T extends g.Uint16Array{}class E extends g.TypeError{}let d=new D(7),t=new T([2,3]),e=new E('x');d.getTime()===7&&d instanceof D&&t[1]===3&&t instanceof T&&e instanceof E&&e instanceof g.TypeError&&!(e instanceof TypeError)",
        "let r=$262.createRealm(),g=r.global;r.global.C=class {m(){return []}};let D=r.evalScript('(class extends C {own(){return []}})'),d=new D();Object.getPrototypeOf(d.m())===Array.prototype&&Object.getPrototypeOf(d.own())===g.Array.prototype"
    };
    for (const char* source : sources) {
        Item value = run(source, &host);
        EXPECT_EQ(value.item, ITEM_TRUE) << source;
    }
}
TEST_F(JsMvpLmd, TypedArrayConstructorsAndStorage) {
    boolean("let constructors=[Int8Array,Uint8Array,Int16Array,Uint16Array,Int32Array,Uint32Array,Float32Array,Float64Array,Uint8ClampedArray,Float16Array];let ok=true;for(let C of constructors){let a=new C([1,2,3]);ok=ok&&a.length===3&&a[1]===2&&a.byteLength===3*C.BYTES_PER_ELEMENT&&a.buffer.byteLength===a.byteLength&&Object.getPrototypeOf(a)===C.prototype}ok");
    boolean("let a=new Int8Array([255,128,-129]),b=new Uint8ClampedArray([-1,1.5,2.5,300,NaN]);a[0]===-1&&a[1]===-128&&a[2]===127&&b[0]===0&&b[1]===2&&b[2]===2&&b[3]===255&&b[4]===0");
    boolean("let a=new BigInt64Array([1n,-2n,18446744073709551615n]),b=new BigUint64Array(a);a[2]===-1n&&b[1]===18446744073709551614n&&b[0]===1n");
    boolean("let a=new Float64Array([5e-324,-0,NaN]),b=new Float64Array(a);a[0]=7;b[0]===5e-324&&1/b[1]===-Infinity&&Number.isNaN(b[2])");
    error("new BigInt64Array([1])", "TypeError");
    error("new Uint8Array(new BigInt64Array(1))", "TypeError");
    error("Int16Array(2)", "TypeError");
    boolean("let reads=0,F=(function(){}).bind(null);Object.defineProperty(F,'prototype',{get(){reads++;throw 7}});let type=false;try{Reflect.construct(Int16Array,[Symbol()],F)}catch(e){type=e instanceof TypeError}type&&reads===0");
}
TEST_F(JsMvpLmd, TypedArrayAliasingResizeAndCoercion) {
    boolean("let b=new ArrayBuffer(8,{maxByteLength:24}),a=new Int16Array(b),v=new Int16Array(b,2,2);a[1]=1234;let first=v[0]===1234;b.resize(16);first&&a.length===8&&v.length===2&&a.buffer===b&&v.byteOffset===2");
    boolean("let b=new ArrayBuffer(8,{maxByteLength:16}),a=new Int16Array(b,2,2);b.resize(2);let empty=a.length===0&&a.byteLength===0&&a.byteOffset===0&&a[0]===undefined;b.resize(8);empty&&a.length===2&&a.byteOffset===2");
    boolean("let b=new ArrayBuffer(8,{maxByteLength:16}),a=new Int16Array(b),calls=0;a[0]={valueOf(){calls++;b.resize(0);return 7}};calls===1&&a.length===0&&a[0]===undefined");
    boolean("let b=new ArrayBuffer(8),a=new Int16Array(b),calls=0;a[0]={valueOf(){calls++;b.transfer();return 7}};calls===1&&a.length===0&&a.buffer===b&&b.detached");
    boolean("let b=new ArrayBuffer(16),a=new BigInt64Array(b);a[0]={valueOf(){b.transfer();return 4n}};a.length===0&&a[0]===undefined");
    error("new Uint16Array(new ArrayBuffer(4),1)", "RangeError");
    error("new Uint32Array(new ArrayBuffer(4),0,2)", "RangeError");
}
TEST_F(JsMvpLmd, TypedArrayNumericReflectionAndReceivers) {
    boolean("let a=new Uint16Array([7,8]),s=Symbol('0');a['01']=9;a[s]=10;let d=Object.getOwnPropertyDescriptor(a,'0'),k=Reflect.ownKeys(a);d.value===7&&d.writable&&d.enumerable&&d.configurable&&k.length===4&&k[0]==='0'&&k[1]==='1'&&k[2]==='01'&&k[3]===s&&a[s]===10");
    boolean("let a=new Int8Array(1),n=0;for(let key of ['-0','-1','1.5','NaN','Infinity','1e+100']){a[key]={valueOf(){n++;return 5}};if(key in a)throw 9}n===6&&a[0]===0&&Reflect.deleteProperty(a,'-0')&&!Reflect.deleteProperty(a,'0')");
    boolean("let a=new Int8Array([1]);Reflect.defineProperty(a,'0',{value:3})&&a[0]===3&&!Reflect.defineProperty(a,'0',{configurable:false})&&!Reflect.defineProperty(a,'1',{value:4})&&!Reflect.defineProperty(a,'0',{get(){return 7}})");
    boolean("let a=new Int8Array([1]),r={};let yes=Reflect.set(a,'0',7,r),skip=Reflect.set(a,'2',9,r);yes&&skip&&a[0]===1&&r[0]===7&&!('2' in r)");
    boolean("let a=new Int8Array([1,2]);a.x=3;Object.keys(a).join(',')==='0,1,x'&&Object.values(a).join(',')==='1,2,3'&&Object.entries(a)[1].join(',')==='1,2'");
    boolean("let a=new Int8Array(1);Object.preventExtensions(a);a[0]=7;a[0]===7&&!Reflect.defineProperty(a,'x',{value:1})");
    error("Object.freeze(new Int16Array(1))", "TypeError");
}
TEST_F(JsMvpLmd, TypedArrayIterationAndSubclassOwnership) {
    boolean("let reads=0,steps=0,source={get [Symbol.iterator](){reads++;return function*(){steps++;yield {valueOf(){return steps}};steps++;yield 7}}};let a=new Int16Array(source);reads===1&&steps===2&&a[0]===2&&a[1]===7");
    boolean("let source={length:2,0:4,1:7,[Symbol.iterator]:undefined};let a=new Uint16Array(source);a.length===2&&a[0]===4&&a[1]===7");
    boolean("class C extends Uint16Array{constructor(b){super(b);this.tag=9}}let a=new C([2,3]);a instanceof C&&a instanceof Uint16Array&&a[1]===3&&a.tag===9&&Object.getPrototypeOf(a)===C.prototype&&a.buffer.byteLength===4");
    boolean("function F(){}F.prototype={tag:7};let a=Reflect.construct(Int16Array,[2],F);a[0]=9;Object.getPrototypeOf(a)===F.prototype&&a.tag===7&&a[0]===9&&ArrayBuffer.isView(a)");
    boolean("let a=new Int16Array([2,3]),i=a.values();Object.defineProperty(a,'length',{value:99});i.next().value===2&&i.next().value===3&&i.next().done");
    boolean("let a=new Int16Array([2,3]),b=Array.from(a);b.length===2&&b[0]===2&&b[1]===3&&a.values===a[Symbol.iterator]");
    boolean("let b=new ArrayBuffer(4),a=new Int16Array(b),i=a.values();b.transfer();let caught=false;try{i.next()}catch(e){caught=e instanceof TypeError}caught");
}
TEST_F(JsMvpLmd, TypedArrayScalarReturnOwnership) {
    Item array = run("new Uint16Array(1)");
    ASSERT_EQ(get_type_id(array), LMD_TYPE_MAP);
    RootFrame roots(3);
    ASSERT_TRUE(roots.valid());
    Rooted<Item> owner(roots, array), first(roots, ItemNull), second(roots, ItemNull);
    const int64_t initial = INT64_C(9007199254740993), later = INT64_C(9007199254741001);
    JsBufferViewOps ops = {};
    // the shared policy may admit Lambda wide integers at a native interop boundary.
    ops.number = [](Item value) { return push_d((double)value.get_int64()); };
    first.set(js_typed_array_set_numeric_with_ops(owner.get(), 0, false, box_int64_value(initial), &ops));
    second.set(js_typed_array_set_numeric_with_ops(owner.get(), 0, false, box_int64_value(later), &ops));
    // a second native call must not overwrite the first assignment's scalar home.
    ASSERT_EQ(get_type_id(first.get()), LMD_TYPE_INT64);
    EXPECT_EQ(first.get().get_int64(), initial);
    EXPECT_EQ(second.get().get_int64(), later);
}
TEST_F(JsMvpLmd, TypedArrayIteratorProjectionsAndSpecies) {
    boolean("let a=new Float64Array([5e-324,-0]),keys=a.keys(),entries=a.entries();let first=entries.next().value,second=entries.next().value;keys.next().value===0&&keys.next().value===1&&keys.next().done&&first[0]===0&&first[1]===5e-324&&second[0]===1&&1/second[1]===-Infinity&&entries.next().done");
    boolean("let b=new ArrayBuffer(2,{maxByteLength:4}),a=new Uint8Array(b),i=a.entries();a[0]=7;let first=i.next().value;b.resize(4);a[2]=9;let second=i.next().value,third=i.next().value;first[0]===0&&first[1]===7&&second[0]===1&&third[0]===2&&third[1]===9");
    boolean("let b=new ArrayBuffer(2,{maxByteLength:4}),a=new Uint8Array(b),i=a.keys();i.next();i.next();let done=i.next().done;b.resize(4);done&&i.next().done");
    boolean("let b=new ArrayBuffer(2),a=new Uint8Array(b),k=a.keys(),e=a.entries();b.transfer();let caught=0;for(let i of [k,e])try{i.next()}catch(error){if(error instanceof TypeError)caught++}caught===2");
    boolean("let T=Object.getPrototypeOf(Uint16Array),d=Object.getOwnPropertyDescriptor(T,Symbol.species);class C extends Uint16Array{}C[Symbol.species]===C&&Uint16Array[Symbol.species]===Uint16Array&&!d.enumerable&&d.configurable&&d.set===undefined&&d.get.call(7)===7");
    error("Int16Array.prototype.keys.call({length:2})", "TypeError");
    error("new Int16Array.prototype.entries()", "TypeError");
}
TEST_F(JsMvpLmd, TypedArraySubarraySpeciesAndResize) {
    boolean("let a=new Int16Array([1,2,3,4]),b=a.subarray(-3,-1);b[0]=8;b.buffer===a.buffer&&b.byteOffset===2&&b.length===2&&a[1]===8&&a.subarray(Infinity).length===0&&a.subarray(-Infinity).length===4");
    boolean("class C extends Uint16Array{}let a=new C([1,2,3]),b=a.subarray(1);a.extra=7;b instanceof C&&b.length===2&&b[0]===2&&b.extra===undefined");
    boolean("let a=new Uint8Array([1,2,3]);a.constructor={[Symbol.species]:null};Object.setPrototypeOf(a,{subarray:Uint8Array.prototype.subarray});let b=a.subarray(1);Object.getPrototypeOf(b)===Uint8Array.prototype&&b[0]===2");
    boolean("let a=new Uint8Array([1,2,3]),log='',argc=0;let out=new Int8Array(0);a.constructor={get [Symbol.species](){log+='s';return function(b,o,n){argc=arguments.length;log+='c';if(b!==a.buffer||o!==1||n!==1)throw 9;return out}}};let result=a.subarray({valueOf(){log+='b';return 1}},{valueOf(){log+='e';return 2}});result===out&&log==='besc'&&argc===3");
    boolean("let b=new ArrayBuffer(4,{maxByteLength:8}),a=new Uint8Array(b,1),v=a.subarray(1),f=a.subarray(1,2);b.resize(8);v.buffer===b&&v.byteOffset===2&&v.length===6&&f.length===1");
    boolean("let b=new ArrayBuffer(8,{maxByteLength:8}),a=new Uint8Array(b,4,2);b.resize(0);let v=a.subarray({valueOf(){b.resize(8);return 1}});v.byteOffset===4&&v.length===0");
    boolean("let b=new ArrayBuffer(4),a=new Uint8Array(b,1,2),out=new Uint8Array(0),seen=false;a.constructor={[Symbol.species]:function(buffer,offset,length){seen=buffer===b&&offset===2&&length===0;return out}};a.subarray(1,{valueOf(){b.transfer();return 0}})===out&&seen");
    boolean("let b=new ArrayBuffer(4,{maxByteLength:8}),a=new Uint8Array(b),out=new Uint8Array(0),seen=false;a.constructor={[Symbol.species]:function(buffer,offset,length){seen=buffer===b&&offset===3&&length===1;return out}};a.subarray({valueOf(){b.resize(0);return 3}},4)===out&&seen");
    error("let a=new Uint8Array(2);a.constructor={[Symbol.species]:function(){return new BigInt64Array(0)}};a.subarray(0)", "TypeError");
    error("let a=new Uint8Array(2);a.constructor={[Symbol.species]:function(){let b=new ArrayBuffer(1),v=new Uint8Array(b);b.transfer();return v}};a.subarray(0)", "TypeError");
    error("Uint8Array.prototype.subarray.call({})", "TypeError");
    boolean("let a=new Uint8Array(2),b=a.subarray(1);b.length===1&&b.buffer===a.buffer");
    error("let a=new Uint8Array(2);a.subarray(Symbol())", "TypeError");
}
TEST_F(JsMvpLmd, TypedArrayCallbacksAndReductions) {
    boolean("let a=new Uint8Array([1,2,3]),seen=0;a.forEach(function(v,i,o){if(o!==a||this.step!==4)throw 1;seen+=v+i}, {step:4});seen===9&&a.every(v=>v>0)&&a.some(v=>v===2)&&a.find(v=>v>1)===2&&a.findIndex(v=>v>1)===1&&a.findLast(v=>v>1)===3&&a.findLastIndex(v=>v>1)===2");
    boolean("let a=new Int8Array([1,2,3]);a.reduce((s,v)=>s*10+v)===123&&a.reduceRight((s,v)=>s*10+v)===321&&a.reduce((s,v)=>s+v,4)===10&&new Uint8Array(0).reduce(()=>{throw 1},undefined)===undefined");
    boolean("let a=new BigInt64Array([1n,2n,3n]);a.reduce((s,v)=>s+v,0n)===6n&&a.findLast(v=>v<3n)===2n");
    boolean("let a=new Uint8Array(2),b=a.map(v=>v+3);b instanceof Uint8Array&&b[0]===3&&b.length===2&&new Uint8Array(0).every(()=>false)&&!new Uint8Array(0).some(()=>true)");
    boolean("let a=new Uint8Array(2),key='ma'+'p',b=a[key](v=>v+3);b[0]===3&&b[1]===3");
    boolean("let a=new Float64Array(2);let run=(key)=>a[key](v=>v+0.5);let b=run('map');b[0]===0.5&&b[1]===0.5");
    boolean("let a=new Uint8Array(2),key='ma'+'p',method=a[key];let b=method.call(a,v=>v+3);b[0]===3&&b[1]===3");
    boolean("let a=new Float64Array(2);let get=(key)=>a[key];let method=get('map'),b=method.call(a,v=>v+0.5);b[0]===0.5&&b[1]===0.5");
    error("let a=new Uint8Array(1);a[0]()", "TypeError");
    error("new Uint8Array(0).reduce(v=>v)", "TypeError");
    error("Uint8Array.prototype.find.call({length:1},()=>true)", "TypeError");
    error("new Uint8Array(1).map(null)", "TypeError");
    error("new Uint8Array.prototype.filter()", "TypeError");
}
TEST_F(JsMvpLmd, TypedArrayCallbacksSpeciesAndEffectOrder) {
    boolean("let a=new Uint8Array([1,2,3]),log='';a.constructor={get [Symbol.species](){log+='s';return class extends Int16Array{constructor(n){super(n);log+='c'}}}};let b=a.map(v=>{log+='m';return v+256});b instanceof Int16Array&&b[0]===257&&log==='scmmm'");
    boolean("let a=new Uint8Array([1,2,3]),log='',size=0;a.constructor={get [Symbol.species](){log+='s';return function(n){size=n;log+='c';return new Float64Array(n)}}};let b=a.filter(v=>{log+='f';return v!==2});b.length===2&&b[1]===3&&log==='fffsc'&&size===2");
    boolean("class C extends Uint8Array{}let a=new C([1,2]);a.map(v=>v) instanceof C&&a.filter(v=>true) instanceof C");
    boolean("let a=new Uint8Array([1]),called=0;a.constructor={get [Symbol.species](){called++;throw 7}};let value;try{a.map(null)}catch(e){value=e instanceof TypeError}value&&called===0");
    error("let a=new Uint8Array([1]);a.constructor={[Symbol.species]:function(){return new BigInt64Array(1)}};a.map(x=>x)", "TypeError");
    error("let a=new BigInt64Array([1n]);a.map(x=>1)", "TypeError");
    error("let a=new Uint8Array([1,2]);a.constructor={[Symbol.species]:function(){return new Uint8Array(1)}};a.filter(()=>true)", "TypeError");
}
TEST_F(JsMvpLmd, TypedArrayCallbacksResizeAndBorrowing) {
    boolean("let b=new ArrayBuffer(3,{maxByteLength:6}),a=new Uint8Array(b),seen=[];a[0]=3;a[1]=4;a[2]=5;a.forEach((v,i)=>{seen.push(v);if(i===0)b.resize(0)});seen.length===3&&seen[0]===3&&seen[1]===undefined&&seen[2]===undefined");
    boolean("let b=new ArrayBuffer(3),a=new Uint8Array(b),seen=[];a[0]=3;a.reduce((s,v,i)=>{seen.push(v);if(i===0)b.transfer();return s+1},0);seen.length===3&&seen[1]===undefined");
    boolean("let b=new ArrayBuffer(2,{maxByteLength:4}),a=new Uint8Array(b),seen=0;a.forEach(()=>{seen++;b.resize(4)});seen===2");
    boolean("let b=new ArrayBuffer(3,{maxByteLength:6}),a=new Uint8Array(b),seen=0;Array.prototype.forEach.call(a,()=>{seen++;b.resize(0)});seen===1");
    boolean("let b=new ArrayBuffer(3),a=new Uint8Array(b);a[0]=7;let result=a.find((v,i)=>{b.transfer();return true});result===7");
    error("let b=new ArrayBuffer(1),a=new Uint8Array(b);b.transfer();a.forEach(()=>{})", "TypeError");
    boolean("let a=new Int8Array([1,2]),n=0;a.constructor={[Symbol.species]:function(k){return new Int8Array(k)}};let b=a.map(v=>({valueOf(){n++;return v+1}}));n===2&&b[0]===2&&b[1]===3");
}
TEST_F(JsMvpLmd, TypedArrayCallbacksScalarOwnership) {
    boolean("let a=new Float64Array([5e-324,-0,1e-323]);let b=a.filter((v,i)=>{a[i]=9;let n=new Float64Array([0.5,1.5]);return n[0]===0.5});b[0]===5e-324&&1/b[1]===-Infinity&&b[2]===1e-323");
    boolean("let a=new Float64Array([5e-324,1e-323]);let x=a.find((v,i)=>{a[i]=7;return true}),y=a.reduce((s,v)=>s+v,5e-324);x===5e-324&&y===7");
    boolean("let a=new BigInt64Array([3n,4n,5n]);let b=a.filter(v=>{let junk=[];for(let i=0;i<30;i++)junk.push({value:i});return v!==4n});b[0]===3n&&b[1]===5n");
    boolean("let a=new Uint8Array([1,2]),error={};let caught;try{a.map(()=>{throw error})}catch(e){caught=e}caught===error");
}
TEST_F(JsMvpLmd, ArrayFromArrayLikeAndConstructors) {
    boolean("let a=Array.from({length:3,0:4,2:7},function(v,i){return v===undefined?i:v+this.step},{step:1});a.length===3&&a[0]===5&&a[1]===1&&a[2]===8");
    boolean("let a=Array.from({length:2});a.length===2&&a.hasOwnProperty('0')&&a.hasOwnProperty('1')&&a[0]===undefined");
    boolean("let log='';let source={get [Symbol.iterator](){log+='i';return undefined},get length(){log+='l';return {valueOf(){log+='n';return 1}}},get 0(){log+='v';return 3}};function C(n){log+='c';this.request=n}let a=Array.from.call(C,source,function(v,i){log+='m';return v+i});a instanceof C&&a.request===1&&a.length===1&&a[0]===3&&log==='ilncvm'");
    boolean("let log='',reads=0;let source={get [Symbol.iterator](){reads++;log+='g';return function*(){log+='i';yield 4}}};function C(){log+='c';this.argc=arguments.length}let a=Array.from.call(C,source);reads===1&&log==='gci'&&a.argc===0&&a[0]===4&&a.length===1");
    boolean("let closed=0;function* source(){try{yield 1;yield 2}finally{closed++}}function C(){return Object.preventExtensions({})}let type=false;try{Array.from.call(C,source())}catch(e){type=e instanceof TypeError}type&&closed===1");
    boolean("let reads=0,source={get [Symbol.iterator](){reads++;throw 3}};let type=false;try{Array.from(source,7)}catch(e){type=e instanceof TypeError}type&&reads===0");
    boolean("let setter=0,proto={set 0(v){setter++}};function C(){return Object.create(proto)}let a=Array.from.call(C,{0:9,length:1});a[0]===9&&a.length===1&&a.hasOwnProperty('0')&&setter===0");
    boolean("let a=Array.from.call(()=>{},[3,4]);Array.isArray(a)&&a.length===2&&a[1]===4");
    error("Array.from({[Symbol.iterator]:7,length:1})", "TypeError");
    error("Array.from(null)", "TypeError");
}

TEST_F(JsMvpLmd, DateCalendarParsingAndFormatting) {
    boolean(R"JS(
        let d=new Date('2000-02-29T12:34:56.789Z');
        d.getUTCFullYear()===2000&&d.getUTCMonth()===1&&d.getUTCDate()===29&&d.getUTCDay()===2&&
        d.getUTCHours()===12&&d.getUTCMinutes()===34&&d.getUTCSeconds()===56&&d.getUTCMilliseconds()===789&&
        d.toISOString()==='2000-02-29T12:34:56.789Z'&&Date.parse(d.toISOString())===d.getTime()&&
        Date.UTC(2000,1,29,12,34,56,789)===d.getTime()&&Date.UTC(1970)===0&&
        new Date(1970,0,1).getFullYear()===1970&&typeof Date()==='string'&&
        new Date(NaN).toString()==='Invalid Date'&&Date.UTC.length===7&&Date.length===7;
    )JS");
}
TEST_F(JsMvpLmd, DateBrandAndSubclassOwnership) {
    boolean(R"JS(
        class Clock extends Date {constructor(t){super(t);this.extra=7;}}
        let d=new Clock(123.9),copy=new Date(d),bad=0;
        d.valueOf=()=>{throw 1;};let copy2=new Date(d);
        for(let o of [{__time__:0},Object.create(Date.prototype),new Proxy(d,{}),Date.prototype]){
            try{Date.prototype.getTime.call(o);}catch(e){bad+=e instanceof TypeError;}
        }
        Object.freeze(copy);copy.setTime(456);
        d instanceof Date&&d instanceof Clock&&d.extra===7&&copy2.getTime()===123&&copy.getTime()===456&&bad===4&&
        Object.prototype.toString.call(d)==='[object Date]'&&
        Object.prototype.toString.call(Date.prototype)==='[object Object]'&&
        Reflect.ownKeys(copy2).length===0&&
        Date.prototype.toGMTString===Date.prototype.toUTCString;
    )JS");
}
TEST_F(JsMvpLmd, DateReentrantSettersAndCoercion) {
    boolean(R"JS(
        let d=new Date(0),order='';
        let result=d.setUTCFullYear({valueOf(){order+='y';d.setTime(86400000);return 2000;}},
            {valueOf(){order+='m';return 1;}},{valueOf(){order+='d';return 29;}});
        let snapshot=result===Date.UTC(2000,1,29)&&order==='ymd';
        d.setUTCSeconds(10,{valueOf(){return 50;}},{valueOf(){throw 8;}});
        let extras=d.getUTCSeconds()===10&&d.getUTCMilliseconds()===50;
        d.setTime(NaN);let invalid=d.setUTCMonth({valueOf(){d.setTime(0);return 2;}});
        snapshot&&extras&&isNaN(invalid)&&d.getTime()===0&&
        new Date({[Symbol.toPrimitive](hint){return hint==='default'?'1970-01-01T00:00:00.000Z':1;}}).getTime()===0;
    )JS");
    boolean(R"JS(
        let d=new Date(0),large=d.setUTCMilliseconds(2147483648);
        let infinite=d.setUTCMilliseconds(Infinity);
        d.setTime(NaN);
        let result=d.setUTCMonth({valueOf(){d.setTime(123);return NaN;}});
        large===2147483648&&isNaN(infinite)&&isNaN(result)&&d.getTime()===123&&
        new Date(0).setUTCFullYear(1)===-62135596800000;
    )JS");
}
TEST_F(JsMvpLmd, DateGenericJsonAndPrimitiveProtocols) {
    boolean(R"JS(
        let p=Date.prototype,order='';
        let object={valueOf(){order+='v';return 0;},toISOString(){order+='i';return 'iso';}};
        let result=p.toJSON.call(object),nothing=p.toJSON.call({valueOf(){return Infinity;}});
        let d=new Date(0),text=d+'';let bad=0;
        try{p[Symbol.toPrimitive].call(d,'bad');}catch(e){bad+=e instanceof TypeError;}
        try{new Date(NaN).toISOString();}catch(e){bad+=e instanceof RangeError;}
        result==='iso'&&order==='vi'&&nothing===null&&text===d.toString()&&+d===0&&bad===2&&
        JSON.stringify({date:d})==='{"date":"1970-01-01T00:00:00.000Z"}'&&
        p[Symbol.toPrimitive].call({valueOf(){return 3;}},'number')===3;
    )JS");
}
TEST_F(JsMvpLmd, BufferViewAliasingAndNumericStorage) {
    boolean(R"JS(
        let b=new ArrayBuffer(24),a=new DataView(b),v=new DataView(b,4,16);
        a.setUint32(4,0x12345678,true);
        let same=v.getUint8(0)===0x78&&v.getUint16(0,true)===0x5678&&v.getUint32(0,false)===0x78563412;
        v.setInt8(0,-1);v.setInt16(2,-1234,true);v.setFloat64(8,-0,true);
        same&&a.getUint8(4)===255&&a.getInt16(6,true)===-1234&&1/v.getFloat64(8,true)===-Infinity&&
        v.buffer===b&&v.byteOffset===4&&v.byteLength===16&&b.byteLength===24&&ArrayBuffer.isView(v)&&!ArrayBuffer.isView(b);
    )JS");
    boolean(R"JS(
        let v=new DataView(new ArrayBuffer(8));
        v.setUint32(0,Math.pow(2,64)+Math.pow(2,32)+4096,true);
        let huge=v.getUint32(0,true)===4096;
        v.setFloat32(0,Infinity);let inf=v.getFloat32(0)===Infinity;
        v.setFloat64(0,NaN);huge&&inf&&Number.isNaN(v.getFloat64(0));
    )JS");
    boolean(R"JS(
        let v=new DataView(new ArrayBuffer(8));v.setBigInt64(0,-1n,true);
        let ok=v.getBigInt64(0,true)===-1n&&v.getBigUint64(0,true)===18446744073709551615n;
        v.setBigUint64(0,{valueOf(){return '18446744073709551617'}},true);
        ok&&v.getBigInt64(0,true)===1n;
    )JS");
}
TEST_F(JsMvpLmd, BufferViewResizableAndTransferOwnership) {
    boolean(R"JS(
        let b=new ArrayBuffer(8,{maxByteLength:32}),fixed=new DataView(b,4,4),tracking=new DataView(b,4);
        fixed.setUint32(0,123);b.resize(16);
        let ok=fixed.byteLength===4&&tracking.byteLength===12&&tracking.getUint32(0)===123;
        b.resize(3);let threw=false;try{fixed.byteLength}catch(e){threw=e instanceof TypeError}
        b.resize(12);ok=ok&&threw&&fixed.byteLength===4&&tracking.byteLength===8&&fixed.getUint32(0)===0;
        let next=b.transfer(16);threw=false;try{tracking.getUint8(0)}catch(e){threw=e instanceof TypeError}
        ok&&threw&&b.detached&&b.byteLength===0&&tracking.buffer===b&&next.resizable&&next.maxByteLength===32&&next.byteLength===16;
    )JS");
    boolean(R"JS(
        let b=new ArrayBuffer(8,{maxByteLength:16});new DataView(b).setUint8(0,77);
        let fixed=b.transferToFixedLength(12);
        b.detached&&!fixed.resizable&&fixed.maxByteLength===12&&new DataView(fixed).getUint8(0)===77&&new DataView(fixed).getUint8(10)===0;
    )JS");
    boolean(R"JS(
        function f(){let b=new ArrayBuffer(16);let v=new DataView(b,8);v.setFloat64(0,42.5);return v}
        let v=f();for(let i=0;i<200;i++){let junk={a:[i]};}
        v.getFloat64(0)===42.5&&v.buffer.byteLength===16;
    )JS");
}
TEST_F(JsMvpLmd, BufferViewBrandsReflectionAndSubclassing) {
    boolean(R"JS(
        class B extends ArrayBuffer{};class V extends DataView{};
        let b=new B(8),v=new V(b),errors=0;
        for(let f of [()=>ArrayBuffer(8),()=>DataView(b),()=>new DataView({}),
            ()=>DataView.prototype.getUint8.call({}),()=>new DataView(new Proxy(b,{})),
            ()=>Object.getOwnPropertyDescriptor(ArrayBuffer.prototype,'byteLength').get.call(ArrayBuffer.prototype),
            ()=>v.setBigInt64(0,1),()=>v.setUint8(0,1n)]){try{f()}catch(e){if(e instanceof TypeError)errors++}}
        v.x=3;Object.freeze(v);v.setUint8(0,9);
        errors===8&&b instanceof B&&b instanceof ArrayBuffer&&v instanceof V&&v instanceof DataView&&
        v.getUint8(0)===9&&v.x===3&&Object.keys(v).join()==='x'&&Object.prototype.toString.call(v)==='[object DataView]'&&
        Object.prototype.toString.call(b)==='[object ArrayBuffer]'&&ArrayBuffer[Symbol.species]===ArrayBuffer;
    )JS");
    boolean(R"JS(
        let d=Object.getOwnPropertyDescriptor(DataView.prototype,'byteLength'),m=DataView.prototype.setInt16;
        !d.enumerable&&d.configurable&&d.set===undefined&&d.get.name==='get byteLength'&&d.get.length===0&&
        m.length===2&&m.name==='setInt16'&&ArrayBuffer.length===1&&DataView.length===1;
    )JS");
}
TEST_F(JsMvpLmd, BufferViewReentrantCoercionAndBounds) {
    boolean(R"JS(
        let b=new ArrayBuffer(8,{maxByteLength:16}),v=new DataView(b),order='';
        v.setUint16({valueOf(){order+='i';return 0}},{valueOf(){order+='v';b.resize(4);return 513}},true);
        let ok=order==='iv'&&v.getUint16(0,true)===513;
        let marker={},same=false;try{v.setInt8(0,{valueOf(){throw marker}})}catch(e){same=e===marker}
        b.transfer();let observed=false;try{v.setInt8(0,{valueOf(){observed=true;return 0}})}catch(e){ok=ok&&e instanceof TypeError}
        ok&&same&&observed;
    )JS");
    boolean(R"JS(
        let order='',b=new ArrayBuffer(8,{maxByteLength:16});
        function Target(){};let target=new Proxy(Target,{get(t,k){if(k==='prototype'){order+='p';b.resize(12)}return t[k]}});
        let v=Reflect.construct(DataView,[b,{valueOf(){order+='o';return 4}}],target);
        order==='op'&&v.byteLength===undefined&&Object.getOwnPropertyDescriptor(DataView.prototype,'byteLength').get.call(v)===8&&
        Object.getPrototypeOf(v)===Target.prototype;
    )JS");
    boolean(R"JS(
        let v=new DataView(new ArrayBuffer(8)),errors=0;
        for(let x of [-1,Infinity,2147483647,9007199254740991]){try{v.getUint32(x)}catch(e){if(e instanceof RangeError)errors++}}
        v.setUint8(-0.5,7);errors===4&&v.getUint8(NaN)===7;
    )JS");
}
TEST_F(JsMvpLmd, BufferViewSliceSpeciesAndDetachment) {
    boolean(R"JS(
        let b=new ArrayBuffer(8),v=new DataView(b);v.setUint8(2,77);
        let copy=b.slice(2,6);let ok=copy.byteLength===4&&new DataView(copy).getUint8(0)===77;
        let result=new ArrayBuffer(8);b.constructor={[Symbol.species]:function(n){return result}};
        ok&&b.slice(2,6)===result&&new DataView(result).getUint8(0)===77&&b.slice(7,2).byteLength===8;
    )JS");
    boolean(R"JS(
        let b=new ArrayBuffer(8,{maxByteLength:16}),v=new DataView(b);v.setUint8(2,77);
        b.constructor={[Symbol.species]:function(n){b.resize(3);return new ArrayBuffer(n)}};
        let copy=b.slice(2,7);let ok=copy.byteLength===5&&new DataView(copy).getUint8(0)===77&&new DataView(copy).getUint8(1)===0;
        b.constructor={[Symbol.species]:function(n){b.transfer();return new ArrayBuffer(n)}};
        let threw=false;try{b.slice(0)}catch(e){threw=e instanceof TypeError}ok&&threw;
    )JS");
    MvpLmdHost host = {0, NULL, NULL, true};
    Item value = run(R"JS(
        let b=new ArrayBuffer(8),v=new DataView(b);$262.detachArrayBuffer(b);
        let threw=false;try{v.getUint8(0)}catch(e){threw=e instanceof TypeError}
        threw&&b.detached&&v.buffer===b;
    )JS", &host);
    ASSERT_EQ(get_type_id(value), LMD_TYPE_BOOL);
    EXPECT_EQ(value.item, ITEM_TRUE);
    value = run("let host=$262;host['evalScript']('1')===1", &host);
    EXPECT_EQ(value.item, ITEM_TRUE);
    const char* unsupported = "let host=$262;try{host['agent']}catch(e){}";
    mvp_lmd_destroy(execution);
    execution = mvp_lmd_execute(unsupported, strlen(unsupported), NULL, &host);
    ASSERT_NE(mvp_lmd_diagnostic(execution), nullptr);
    EXPECT_NE(strstr(mvp_lmd_diagnostic(execution), "capability"), nullptr);
}
TEST_F(JsMvpLmd, WithLexicalAndObjectEnvironments) {
    boolean(R"JS(
        let x=1,o={x:7},seen=0;
        with(o){x+=2;{let x=30;seen=x;}with({x:12}){seen+=x;}}
        let tdz=false;with(o){try{x;let x;}catch(e){tdz=e instanceof ReferenceError}}
        x===1&&o.x===9&&seen===42&&tdz;
    )JS");
    boolean(R"JS(
        function f(){let x=1;with({}){x='changed'}return x==='changed'}
        let o={x:1};var x=4;
        with(o){var x=(delete o.x,9);var y=3;}
        f()&&o.x===9&&x===4&&y===3;
    )JS");
    boolean(R"JS(
        let f=[],objects=[];
        for(let i=0;i<3;i++){let o={x:i};objects.push(o);with(o){f.push(()=>++x);}}
        let o={x:5},g;with(o){g=function(x){return ()=>x};}
        f[0]()===1&&f[1]()===2&&f[0]()===2&&f[2]()===3&&objects[0].x===2&&g(12)()===12;
    )JS");
}
TEST_F(JsMvpLmd, WithResolvedReferencesAndReceivers) {
    boolean(R"JS(
        let o={x:2},x=100;
        with(o){x+=(delete o.x,3);}
        let p={x:1};with(p){({x= (delete p.x,8)}={});}
        let q={x:1};with(q){var {x:z=0}={x:3};[x=(delete q.x,7)]=[];}
        o.x===5&&x===100&&p.x===8&&q.x===7&&z===3;
    )JS");
    boolean(R"JS(
        let o={f:function(v){return this===o&&v===7}},a,b,c;
        with(o){a=f((delete o.f,7));}
        o.f=function(v){return this===o&&v===8};with(o){b=f(...[8]);}
        o.f=function(parts,v){return this===o&&parts[0]==='a'&&v===9};with(o){c=f`a${9}`;}
        let untouched=0;with({f:null}){f?.(untouched++);}
        a&&b&&c&&untouched===0;
    )JS");
    boolean(R"JS(
        let missing,removed,outer=7;
        with({x:1}){missing=typeof absent;removed=delete x;}
        with({}){removed=removed&&!delete outer&&delete absent;}
        missing==='undefined'&&removed&&outer===7;
    )JS");
}
TEST_F(JsMvpLmd, WithUnscopablesAndPropertyEffects) {
    boolean(R"JS(
        let values=7,seen;with([]){seen=values;}
        let d=Object.getOwnPropertyDescriptor(Array.prototype,Symbol.unscopables),u=d.value;
        seen===7&&!d.writable&&!d.enumerable&&d.configurable&&Object.getPrototypeOf(u)===null&&
            u.values&&u.findLast&&u.toSpliced&&!Object.hasOwn(u,'with');
    )JS");
    boolean(R"JS(
        let x=5,o={x:9,[Symbol.unscopables]:{x:true}},seen;
        with(o){seen=x;}
        o[Symbol.unscopables]={x:false};with(o){seen+=x;}
        let blocked=function(){};blocked.x=true;o[Symbol.unscopables]=blocked;
        with(o){seen+=x;}
        seen===19;
    )JS");
    boolean(R"JS(
        let log=[],target={x:8},o=new Proxy(target,{
            has(t,k){if(k==='x')log.push('has');return k in t},
            get(t,k,r){if(k===Symbol.unscopables){log.push('unscopables');delete t.x;return null}
                if(k==='x')log.push('get');return t[k]}
        }),x=3,value;
        with(o){value=x;}
        value===undefined&&x===3&&log.join(',')==='has,unscopables,has';
    )JS");
    boolean(R"JS(
        let marker={},caught=false,o={x:1,get [Symbol.unscopables](){throw marker}};
        try{with(o){x;}}catch(e){caught=e===marker}
        let x=6;with({}){}caught&&x===6;
    )JS");
}
TEST_F(JsMvpLmd, WithPrimitiveAndAbruptCompletions) {
    boolean(R"JS(
        let size;with('abc'){size=length;}
        let first=false,second=false;
        try{with(null){}}catch(e){first=e instanceof TypeError}
        try{with(undefined){}}catch(e){second=e instanceof TypeError}
        function f(){let x=1;try{with({x:9}){return ()=>x;}}finally{x=4}}
        let x=2;outer:for(let i=0;i<2;i++){with({x:9}){break outer;}}
        size===3&&first&&second&&f()()===9&&x===2;
    )JS");
}
TEST_F(JsMvpLmd, CoreObjectReceiverConversions) {
    boolean(R"JS(
        let d=Object.getOwnPropertyDescriptor('a','0');
        let f=function(){};
        Object.defineProperties(f,{x:{value:7}});
        let boxed=Object.assign(3,{x:9});
        d.value==='a' && !d.writable && d.enumerable && !d.configurable && f.x===7 &&
            Object.keys(Object.defineProperties({},7)).length===0 && boxed.x===9 && boxed.valueOf()===3 &&
            Object.prototype.propertyIsEnumerable.call('a','0') &&
            Object.prototype.hasOwnProperty.call('a','length') && Object.hasOwn('a','0');
    )JS");
    boolean(R"JS(
        let log='',key={toString(){log+='k';return 'x'}},ok=true;
        try{Object.defineProperty(1,key,{})}catch(e){ok=ok&&e instanceof TypeError}
        ok=ok&&log==='';
        try{Object.getOwnPropertyDescriptor(null,key)}catch(e){ok=ok&&e instanceof TypeError}
        ok=ok&&log==='';
        try{Object.defineProperty({},key)}catch(e){ok=ok&&e instanceof TypeError}
        ok=ok&&log==='k';log='';
        try{Object.prototype.hasOwnProperty.call(null,key)}catch(e){ok=ok&&e instanceof TypeError}
        ok&&log==='k';
    )JS");
    boolean(R"JS(
        let f=function(){},descriptors=function(){};
        descriptors.x={value:4};Object.defineProperties(f,descriptors);
        let marker={},target={},ok=false;
        try{Object.defineProperties(target,{a:{value:1},get b(){throw marker}})}catch(e){ok=e===marker}
        f.x===4 && ok && !Object.hasOwn(target,'a');
    )JS");
}
TEST_F(JsMvpLmd, CoreIntrinsicMetadata) {
    boolean(R"JS(
        let functions=[Object.defineProperty,Object.defineProperties,Object.getOwnPropertyDescriptor,
            Object.keys,Object.freeze,Array.prototype.map,Array.prototype.pop,Array.prototype.slice];
        let names=['defineProperty','defineProperties','getOwnPropertyDescriptor','keys','freeze','map','pop','slice'];
        let lengths=[3,2,2,1,1,1,0,2],ok=true;
        for(let i=0;i<functions.length;i++) {
            let f=functions[i],d=Object.getOwnPropertyDescriptor(f,'name');
            let l=Object.getOwnPropertyDescriptor(f,'length');
            ok=ok && f.name===names[i] && f.length===lengths[i] && !d.writable && !d.enumerable &&
                d.configurable && !l.writable && !l.enumerable && l.configurable && !Object.hasOwn(f,'prototype');
        }
        ok && Object.getOwnPropertyNames(Object.keys).join(',')==='length,name' &&
            Object.getOwnPropertyNames(function(){}).join(',')==='length,name,prototype';
    )JS");
    error("new Object.defineProperty({},'x',{});", "TypeError");
    boolean(R"JS(
        let constructors=[Object,Array,String,Number,Boolean],ok=true;
        for(let f of constructors) {
            let p=Object.getOwnPropertyDescriptor(f,'prototype'),n=Object.getOwnPropertyDescriptor(f,'name');
            ok=ok&&!p.writable&&!p.enumerable&&!p.configurable&&!n.writable&&!n.enumerable&&n.configurable;
        }
        ok&&Function.prototype.call.name==='call'&&Function.prototype.apply.name==='apply'&&
            Function.prototype.bind.name==='bind'&&Object.setPrototypeOf(3,{})===3;
    )JS");
}
TEST_F(JsMvpLmd, CoreGenericArrayVisitors) {
    boolean(R"JS(
        let seen=[],boxed,owner={length:3,0:1,2:3};
        Object.setPrototypeOf(owner,{1:2});
        let sum=Array.prototype.reduceRight.call(owner,(a,v,i)=>{seen.push(i);return a*10+v},0);
        let found=[,4].find((v,i)=>i===0&&v===undefined);
        let index=[,4].findIndex((v,i)=>i===0&&v===undefined);
        let mapped=Array.prototype.map.call('ab',function(v,i,o){boxed=o;return v+i});
        let every=Array.prototype.every.call(owner,(v,i)=>v===i+1);
        sum===321&&seen.join(',')==='2,1,0'&&found===undefined&&index===0&&every&&
            typeof boxed==='object'&&boxed.valueOf()==='ab'&&mapped.join(',')==='a0,b1'&&
            Array.prototype.every.call(7,()=>false)&&[1,2,1].lastIndexOf(1)===2&&
            [1,2,1].lastIndexOf(1,undefined)===0&&[NaN].lastIndexOf(NaN)===-1;
    )JS");
    boolean(R"JS(
        let log='',o={get length(){log+='l';return 9007199254740992},0:7};
        let found=Array.prototype.find.call(o,(v,i)=>{log+='f';return true});
        let at=Array.prototype.at.call({length:4294967297,4294967296:9},-1);
        let a=[1,2,3],seen=[];
        let all=a.every((v,i)=>{seen.push(v);if(i===0){delete a[1];a.push(4)}return true});
        let big={length:9007199254740991},failed=false;
        try{Array.prototype.push.call(big,1)}catch(e){failed=e instanceof TypeError}
        found===7&&log==='lf'&&at===9&&all&&seen.join(',')==='1,3'&&failed;
    )JS");
    error("[].reduceRight(()=>0)", "TypeError");
    error("Array.prototype.every.call(null,()=>true)", "TypeError");
}
TEST_F(JsMvpLmd, CoreArraySpeciesObjects) {
    boolean(R"JS(
        let created=[];
        function Result(n){this.initial=n;created.push(this)}
        let a=[1,,3];a.constructor={[Symbol.species]:Result};
        let mapped=a.map(x=>x*2),filtered=a.filter(x=>x>1),sliced=a.slice(0),flat=a.flat(),joined=a.concat([4]);
        let removed=a.splice(1,2);
        mapped instanceof Result&&mapped.initial===3&&mapped[0]===2&&!Object.hasOwn(mapped,'1')&&mapped[2]===6&&
            filtered[0]===3&&filtered.length===undefined&&sliced.length===3&&!Object.hasOwn(sliced,'1')&&
            flat[0]===1&&flat[1]===3&&flat.length===undefined&&joined.length===4&&joined[3]===4&&
            removed.length===2&&!Object.hasOwn(removed,'0')&&removed[1]===3;
    )JS");
    boolean(R"JS(
        let events=[],a=[1,2],target={};
        function Result(n){return new Proxy(target,{defineProperty(t,k,d){events.push(k);return Reflect.defineProperty(t,k,d)}})}
        a.constructor={[Symbol.species]:Result};
        let r=a.map(v=>v+1);
        r[0]===2&&r[1]===3&&events.join(',')==='0,1'&&Object.getOwnPropertyDescriptor(target,'0').writable;
    )JS");
}
TEST_F(JsMvpLmd, ArrayFlatMapOrderAndOwnership) {
    boolean(R"JS(
        let source=[1,,3],seen=[],receiver={},nested=[,7],result;
        Object.setPrototypeOf(source,{1:2});
        result=Array.prototype.flatMap.call(source,function(v,i,o){
            seen.push(this===receiver&&o===source?i:-1);
            if(i===0){source[source.length]=4;return [v,nested]}
            return [v,,v+.25];
        },receiver);
        result.length===6&&result[0]===1&&result[1]===nested&&result[2]===2&&
            result[3]===2.25&&result[4]===3&&result[5]===3.25&&seen.join(',')==='0,1,2';
    )JS");
    boolean(R"JS(
        let log='',source=[1,2],target={};
        function Result(n){log+='s'+n;return new Proxy(target,{
            defineProperty(t,k,d){log+='d'+k;return Reflect.defineProperty(t,k,d)}
        })}
        source.constructor={[Symbol.species]:Result};
        let result=source.flatMap((v,i)=>{log+='m'+i;return new Proxy([v],{
            get(t,k,r){log+='g'+k;return Reflect.get(t,k,r)},
            has(t,k){log+='h'+k;return Reflect.has(t,k)}
        })});
        result[0]===1&&result[1]===2&&result.length===undefined&&
            log==='s0m0glengthh0g0d0m1glengthh0g0d1';
    )JS");
    boolean(R"JS(
        let boxed,calls=0,marker={},caught,log='';
        let out=Array.prototype.flatMap.call('ab',function(v,i,o){boxed=o;return [v,i]});
        try{[1,2].flatMap(v=>{calls++;throw marker})}catch(e){caught=e}
        let source={get length(){log+='l';return 0}},failed=false;
        try{Array.prototype.flatMap.call(source,null)}catch(e){failed=e instanceof TypeError}
        let values=[1.25,2.5].flatMap(x=>{let a=[x*3];return a});
        let revoked=Proxy.revocable([],{});revoked.revoke();let bad=false;
        try{[1].flatMap(()=>revoked.proxy)}catch(e){bad=e instanceof TypeError}
        boxed.valueOf()==='ab'&&out.join(',')==='a,0,b,1'&&calls===1&&caught===marker&&
            failed&&log==='l'&&values[0]===3.75&&values[1]===7.5&&bad;
    )JS");
}
TEST_F(JsMvpLmd, ArrayFindLastGenericEffects) {
    boolean(R"JS(
        let seen=[],receiver={},source={length:4,0:7,2:9};
        Object.setPrototypeOf(source,{1:8});
        let result=Array.prototype.findLast.call(source,function(v,i,o){
            seen.push(i+':'+v);if(i===3){source.length=0;delete source[2]}
            return this===receiver&&o===source&&i===1;
        },receiver);
        result===8&&seen.join(',')==='3:undefined,2:undefined,1:8'&&
            [,4,,].findLastIndex(v=>v===undefined)===2&&
            [1,2].findLast(()=>false)===undefined&&[1,2].findLastIndex(()=>false)===-1;
    )JS");
    boolean(R"JS(
        let log='',marker={},caught;
        let source=new Proxy({length:2,0:1,1:2},{
            get(t,k,r){log+='g'+k;return Reflect.get(t,k,r)},has(){throw marker}
        });
        let index=Array.prototype.findLastIndex.call(source,(v,i)=>{log+='p'+i;return i===0});
        try{[1].findLast(()=>{throw marker})}catch(e){caught=e}
        index===0&&log==='glengthg1p1g0p0'&&caught===marker&&
            Array.prototype.findLast.call({length:9007199254740992,9007199254740990:3},()=>true)===3&&
            [1.25,2.5].findLast(x=>x>2)===2.5;
    )JS");
    error("Array.prototype.findLast.call(null,()=>true)","TypeError");
    error("[].findLastIndex(null)","TypeError");
}
TEST_F(JsMvpLmd, CoreTaggedTemplates) {
    boolean(R"JS(
        let previous,ok=true,calls=0;
        function tag(strings,v){
            ok=ok&&Object.isFrozen(strings)&&Object.isFrozen(strings.raw)&&strings.raw[0]==='a\\n'&&strings[0]==='a\n';
            if(previous)ok=ok&&previous===strings;
            previous=strings;calls++;return v;
        }
        function run(v){return tag`a\n${v}b`}
        let x=run(1),y=run(2);
        let other=(s=>s)`a\n${0}b`;
        let invalid=(s=>s)`\unicode`;
        ok&&calls===2&&x===1&&y===2&&other!==previous&&invalid[0]===undefined&&invalid.raw[0]==='\\unicode';
    )JS");
    boolean(R"JS(
        let log='',owner={get tag(){log+='g';return function(s,v){log+='c';return this===owner&&v===7}}};
        let result=owner.tag`a${(log+='v',7)}b`;
        let caught=false;try{(0)`x${(log+='s')}`}catch(e){caught=e instanceof TypeError}
        result&&caught&&log==='gvcs';
    )JS");
}
TEST_F(JsMvpLmd, CoreStringAndPrimitiveMethods) {
    boolean(R"JS(
        let p=String.prototype,log='';
        let s={toString(){log+='s';return 'a😀b'}},i={valueOf(){log+='i';return 1}};
        let code=p.codePointAt.call(s,i);
        code===0x1f600&&log==='si'&&p.charAt.call(123,1)==='2'&&p.slice.call('a😀b',1,3)==='😀'&&
            p.concat.call(true,'-',7)==='true-7'&&'x'.padEnd(3,'😀').length===3&&
            '\ufeff a \n'.trim()==='a'&&' a '.trimStart()==='a '&&' a '.trimEnd()===' a'&&
            '😀'.at(-1).charCodeAt(0)===0xde00&&'ababa'.lastIndexOf('ba',2)===1&&'abc'.includes('b')&&
            String.fromCharCode(0xd83d,0xde00)==='😀'&&String.fromCodePoint(0x1f600)==='😀';
    )JS");
    boolean(R"JS(
        let log='',template={get raw(){log+='r';return {get length(){log+='l';return 2},
            get 0(){log+='a';return {toString(){log+='A';return 'a'}}},get 1(){log+='b';return 'b'}}}};
        let value=String.raw(template,{toString(){log+='s';return 7}});
        value==='a7b'&&log==='rlaAsb'&&String.raw({raw:'ab'},3)==='a3b'&&
            String.raw`a\n${4}b`==='a\\n4b'&&Number.isInteger(2)&&!Number.isInteger('2')&&
            Number.isSafeInteger(Number.MAX_SAFE_INTEGER)&&!Number.isSafeInteger(Number.MAX_SAFE_INTEGER+1)&&
            Number.parseInt('10',2)===2&&Number.parseFloat('1.5x')===1.5;
    )JS");
    boolean("let original=String.prototype.charAt;String.prototype.charAt=function(){return 'override'};let result='abc'.charAt(1);String.prototype.charAt=original;result==='override'");
    error("String.raw()", "TypeError");
    error("String.prototype.charAt.call(null,0)", "TypeError");
    error("String.fromCodePoint(1.5)", "RangeError");
    error("String.prototype.startsWith.call('abc',/a/)", "TypeError");
    boolean(R"JS(
        let calls='',a={valueOf(){calls+='a';return 0xd800}},b={valueOf(){calls+='b';return 65}};
        let text=String.fromCharCode(a,b);
        let codes=new Array(10000).fill(65);
        text==='\ud800A'&&calls==='ab'&&String.fromCodePoint(0xd83d,0xde00)==='😀'&&
            String.fromCodePoint.apply(null,codes).length===10000&&
            String.fromCharCode.apply(null,codes).length===10000;
    )JS");
    boolean("let seen='',marker={};try{String.fromCodePoint({valueOf(){seen+='a';return -1}},{valueOf(){seen+='b';return 65}})}catch(e){seen+=e instanceof RangeError?'r':'x'}seen==='ar'");
    boolean("String.prototype.trimLeft===String.prototype.trimStart&&String.prototype.trimRight===String.prototype.trimEnd&&String.prototype.trimLeft.call(' a ')==='a '");
    boolean("' a '.trimLeft()==='a '&&' a '.trimRight()===' a'");
    boolean("Number.NaN.toFixed('1.1')==='NaN'&&Number.prototype.toFixed.call(new Number(1.25),1)==='1.3'");
    error("Number.NaN.toFixed(Infinity)", "RangeError");
    error("Number.prototype.toFixed.call('1.25',1)", "TypeError");
}
TEST_F(JsMvpLmd, CoreStringMethodAdmissionProof) {
    boolean("function read(text){let owner={at(i){return i}};return owner.at(0)===0&&text.substring(1,2)==='b'}read('abc')");
    char* mir = dump("temp/mvp_lmd_string_method_admission.mir");
    ASSERT_NE(mir, nullptr);
    EXPECT_EQ(strstr(mir, "\timport\tmvp_lmd_array_method_value_fixed\n"), nullptr);
    mem_free(mir);
    boolean("let a=[1,2];function read(text){return a.at(-1)===2&&text.substring(1,2)==='b'}read('abc')");
    boolean("function read(text){return text.at(-1)}read('abc')==='c'&&String.prototype.at.call(123,-1)==='3'");
    boolean("String.prototype.substring=function(){return 17};'abc'.substring(0,1)===17");
    boolean("Number.prototype.toFixed=function(){return 17};(1.25).toFixed(1)===17&&(1).toFixed(1)===17");
    boolean("let key='prototype',owner=String;owner[key].substring=function(){return 19};'abc'.at(0)==='a'&&'abc'.substring(0,1)===19");
    boolean("let proto=Object.getPrototypeOf('abc');Object.defineProperty(proto,'substring',{get(){return function(){return 23}}});'abc'.substring(0,1)===23");
    boolean("function read(a,i,text){return a[i]+text.substring(1,2)}read([1],0,'abc')==='1b'&&'abc'.at(-1)==='c'");
}
TEST_F(JsMvpLmd, CoreLocalClassDefinitions) {
    boolean(R"JS(
        function make(n) {
            class Local {constructor(){this.n=n} read(){return n} change(v){n=v} self(){return Local}}
            return Local;
        }
        let A=make(1),B=make(2),a=new A(),b=new B();a.change(7);
        A!==B&&A.prototype!==B.prototype&&a.read()===7&&b.read()===2&&a.n===1&&b.n===2&&a.self()===A&&
            a instanceof A&&!(a instanceof B)&&Object.getPrototypeOf(a)===A.prototype;
    )JS");
    boolean(R"JS(
        let A=class Inner {self(){return Inner}};
        let B=class {self(){return B}};
        let d=Object.getOwnPropertyDescriptor(A,'prototype');
        A.name==='Inner'&&B.name==='B'&&new A().self()===A&&new B().self()===B&&
            !d.writable&&!d.enumerable&&!d.configurable&&Object.getOwnPropertyNames(A).join(',')==='length,name,prototype';
    )JS");
    error("let C=class D extends D {};", "ReferenceError");
    error("let C=class D {[D](){}};", "ReferenceError");
    error("let C=class D {static change(){D=1}};C.change();", "TypeError");
    boolean("class S extends Symbol{};Object.getPrototypeOf(S)===Symbol&&Object.getPrototypeOf(S.prototype)===Symbol.prototype");
    error("class S extends Symbol{};new S()", "TypeError");
    error("class S extends Symbol{constructor(){super()}};new S()", "TypeError");
}
TEST_F(JsMvpLmd, CoreComputedClassMembers) {
    boolean(R"JS(
        let key=Symbol('s'),log='';
        let C=class {
            [(log+='a','x')](){return 1}
            get [(log+='b',key)](){return 2}
            set [key](v){this.v=v}
            static [(log+='c','name')](){return 3}
            static get count(){return 4}
        };
        let c=new C();c[key]=9;
        let d=Object.getOwnPropertyDescriptor(C.prototype,key);
        log==='abc'&&c.x()===1&&c[key]===2&&c.v===9&&C.name()===3&&C.count===4&&
            !d.enumerable&&d.configurable&&d.get.name==='get [s]'&&d.set.name==='set [s]'&&
            Object.keys(C.prototype).length===0&&C.prototype.x.name==='x';
    )JS");
    boolean("class C {static name(){return 3} static length(){return 4}};C.name()===3&&C.length()===4");
}
TEST_F(JsMvpLmd, CoreClassFieldsAndStaticBlocks) {
    boolean(R"JS(
        let log='',key='x';
        let C=class {
            [(log+='a',key)]=(log+='i',1);
            static [(log+='b','y')]=(log+='s',2);
            static {var hidden=3;log+='t';this.z=hidden;this.self=()=>this}
            constructor(){log+='c';this.x++}
        };
        let before=log,c=new C();
        before==='abst'&&log==='abstic'&&c.x===2&&C.y===2&&C.z===3&&C.self()===C&&typeof hidden==='undefined';
    )JS");
    boolean(R"JS(
        let log='';
        class A {a=(log+='a',1);constructor(){log+='b'}get x(){return this.a}static value(){return 5}}
        class B extends A {b=(log+='c',super.x+1);static v=super.value();static {this.w=super.value()+1}
            constructor(){super();log+='d'}read(){return super.x}}
        let b=new B();
        log==='abcd'&&b.a===1&&b.b===2&&b.read()===1&&B.v===5&&B.w===6&&b instanceof A&&b instanceof B;
    )JS");
    boolean(R"JS(
        function Base(v){this.v=v}Base.prototype.read=function(){return this.v};
        let C=class extends Base {};let c=new C(7);
        class NullBase extends null {constructor(){return Object.create(null)}}
        Object.getPrototypeOf(C)===Base&&Object.getPrototypeOf(C.prototype)===Base.prototype&&
            c.read()===7&&c instanceof C&&c instanceof Base&&Object.getPrototypeOf(new NullBase())===null;
    )JS");
    boolean("class C { f=function(){}; c=class {static n=this.name}; static g=()=>1; static d=class {static n=this.name}}let c=new C();c.f.name==='f'&&c.c.n==='c'&&C.g.name==='g'&&C.d.n==='d'");
    boolean("function Base(v){this.v=v}let C=class extends Base {};new C(7).v===7");
    error("let C=class extends null {};new C();", "TypeError");
}
TEST_F(JsMvpLmd, PrivateClassFieldsAndBrands) {
    boolean(R"JS(
        class C{#x;f(o){let {[#x in o]:v}={true:3,false:7};return v}}
        let c=new C();c.f(c)===3&&c.f({})===7;
    )JS");
    boolean(R"JS(
        class C{'constructor\0x'=3;static 'prototype\0x'(){return 7}}
        new C()['constructor\0x']===3&&C['prototype\0x']()===7;
    )JS");
    boolean(R"JS(
        function check(){let caught=false;try{class C{#f(){}[this.#f]=1}}catch(e){caught=e instanceof TypeError}return caught}
        function keys(){return class{[this.key]=1;[arguments[0]](){return 2}}}
        let C=keys.call({key:'x'},'read'),c=new C();
        check()&&c.x===1&&c.read()===2&&(class{constructor(){}}).name==='';
    )JS");
    boolean(R"JS(
        class C{f=function(){return arguments[0]};g=()=>this.f(3);static{this.s=function(){return arguments[0]}}}
        new C().g()===3&&C.s(4)===4;
    )JS");
    boolean(R"JS(
        class C {
            #x=1; static #s=9;
            get(){return this.#x} set(v){this.#x=v;return this.#x++}
            has(o){return (#x in o)} static get(){return this.#s}
        }
        let c=new C(),other=new C();let failed=false;
        try {C.get.call(class extends C {})}catch(e){failed=e instanceof TypeError}
        c.set(4)===4&&c.get()===5&&other.get()===1&&c.has(c)&&!c.has({})&&
            Object.getOwnPropertyNames(c).length===0&&Object.getOwnPropertySymbols(c).length===0&&
            Object.getOwnPropertyNames(C).indexOf('#s')===-1&&C.get()===9&&failed;
    )JS");
    boolean(R"JS(
        function make(v){return class {#x=v;read(o){return o.#x}has(o){return #x in o}}}
        let A=make(1),B=make(2),a=new A(),b=new B();let failed=false;
        try{a.read(b)}catch(e){failed=e instanceof TypeError}
        a.read(a)===1&&b.read(b)===2&&!a.has(b)&&failed;
    )JS");
    boolean(R"JS(
        class Outer {#x=3;make(){let self=this;return class {#x=7;get(){return this.#x}outer(){return self.#x}}}}
        let O=new Outer(),C=O.make(),c=new C();let failed=false;
        try{c.outer()}catch(e){failed=e instanceof TypeError}
        c.get()===7&&failed;
    )JS");
    error("class C{#x;has(o){return #x in o}}new C().has(1)", "TypeError");
    error("class C{#x;get(){return this.#x}}C.prototype.get()", "TypeError");
    for (int private_fields = 0; private_fields < 2; private_fields++) {
        const int counts[] = {126, 127, 257};
        for (int count : counts) {
            StrBuf* source = strbuf_new();
            strbuf_append_str(source, "class C{");
            for (int i = 0; i < count; i++) strbuf_append_format(source, "%sf%d=%d;", private_fields ? "#" : "", i, i);
            strbuf_append_format(source, "get(){return this.%sf%d}}new C().get()", private_fields ? "#" : "", count - 1);
            numeric(source->str, count - 1);
            strbuf_free(source);
        }
    }
}
TEST_F(JsMvpLmd, PrivateClassMethodsAndAccessors) {
    boolean(R"JS(
        class C {
            #x=this.#m(); #m(){return 4} get #a(){return this.#x}set #a(v){this.#x=v}
            get(){return this.#a}set(v){this.#a=v}method(){return this.#m}
            static #sm(){return 12}static v=this.#sm();static has(o){return #sm in o}
        }
        let c=new C(),d=new C();c.set(8);
        c.get()===8&&d.get()===4&&c.method()===d.method()&&c.method().name==='#m'&&
            C.v===12&&C.has(C)&&!C.has(c)&&Object.keys(c).length===0;
    )JS");
    error("class C{#m(){}set(){this.#m=1}}new C().set()", "TypeError");
    error("class C{set #a(v){}get(){return this.#a}}new C().get()", "TypeError");
    error("class C{get #a(){return 1}set(){this.#a=2}}new C().set()", "TypeError");
    boolean(R"JS(
        class Base{constructor(o){return o}}
        class C extends Base {#x=1;#m(){return this.#x}read(){return this.#m()}static get(o){return o.#x}}
        let o=Object.freeze({});new C(o);let duplicate=false;
        try{new C(o)}catch(e){duplicate=e instanceof TypeError}
        C.get(o)===1&&duplicate;
    )JS");
}
TEST_F(JsMvpLmd, PrivateClassProxyAndEscapedClosures) {
    boolean(R"JS(
        let C;with({'#x':9}){C=class{#x=1;has(v){return #x in v}read(v){return v.#x}}}
        let c=new C();c.has(c)&&!c.has({})&&c.read(c)===1;
    )JS");
    boolean(R"JS(
        let called=0,p=Proxy.revocable({}, {get(){called++;throw 1},set(){called++;throw 2}});
        class B{constructor(o){return o}}
        class C extends B {#x=5;get #a(){return this.#x}static read(o){return o.#a}
            static set(o,v){o.#x=v}static has(o){return #x in o}}
        let o=new C(p.proxy);p.revoke();C.set(o,9);
        C.read(o)===9&&C.has(o)&&!C.has({})&&called===0;
    )JS");
    boolean(R"JS(
        function make(v){class C{#x=v;method(){return function(o){return o.#x}}};let c=new C();return [c,c.method()]}
        let a=make(6),b=make(7);let failed=false;
        try{a[1](b[0])}catch(e){failed=e instanceof TypeError}
        a[1](a[0])===6&&b[1](b[0])===7&&failed;
    )JS");
    boolean(R"JS(
        class B{constructor(o){return o}}class C extends B{#x=8;static read(o){return o.#x}}
        let a=new C([]),f=new C(function(){});C.read(a)===8&&C.read(f)===8;
    )JS");
}
TEST_F(JsMvpLmd, CoreLiteralReservationCodeSize) {
    StrBuf* source = strbuf_new();
    ASSERT_NE(source, nullptr);
    strbuf_append_str(source, "let a=[");
    for (int i = 0; i < 512; i++) strbuf_append_str(source, i ? ",\"value\"" : "\"value\"");
    strbuf_append_str(source, "];a.length===512&&a[0]==='value'&&a[511]==='value'");
    boolean(source->str); strbuf_free(source);
    char* mir = dump("temp/mvp_lmd_reserved_literal_size.mir");
    ASSERT_NE(mir, nullptr);
    EXPECT_EQ(strstr(mir, "\timport\tmvp_lmd_array_store\n"), nullptr);
    mem_free(mir);
    boolean("function make(v){return {child:{value:v},flag:true,text:'value'}}let a=make(7),b=make(8);a.child.value===7&&b.child.value===8&&a!==b&&a.child!==b.child&&a.flag&&a.text==='value'");
    // aligned constant fields prove the direct path; the variant above also exercises required fallbacks.
    boolean("function make(){return {text:'value',child:{value:7},flag:true}}let a=make(),b=make();a.child.value===7&&a!==b&&a.child!==b.child&&a.flag&&a.text==='value'");
    mir = dump("temp/mvp_lmd_reserved_object_size.mir");
    ASSERT_NE(mir, nullptr);
    EXPECT_EQ(strstr(mir, "\timport\tmap_shape_set_text\n"), nullptr);
    mem_free(mir);
}
TEST_F(JsMvpLmd, CoreGlobalBindings) {
    boolean("globalThis.coldBinding=7;coldBinding===7");
    char* mir = dump("temp/mvp_lmd_cold_global.mir");
    ASSERT_NE(mir, nullptr);
    EXPECT_EQ(strstr(mir, "\timport\tmap_shape_field_to_item\n"), nullptr);
    mem_free(mir);
    boolean("globalThis.loopBinding=0;let total=0;for(let i=0;i<12;i++){globalThis.loopBinding=i;total+=loopBinding}total===66");
    mir = dump("temp/mvp_lmd_loop_global.mir");
    ASSERT_NE(mir, nullptr);
    EXPECT_NE(strstr(mir, "\timport\tmap_shape_field_to_item\n"), nullptr);
    mem_free(mir);
    boolean(R"JS(
        globalThis.cacheProbe=1;
        function read(){return cacheProbe}
        let ok=true;
        for(let i=0;i<12;i++){globalThis.cacheProbe=i;ok=ok&&read()===i}
        globalThis.cacheProbe=undefined;ok=ok&&read()===undefined;
        delete globalThis.cacheProbe;
        let log='',present=true;
        Object.setPrototypeOf(globalThis,new Proxy({}, {
            has(t,k){if(k==='cacheProbe'){log+='h';return present}return false},
            get(t,k){if(k==='cacheProbe'){log+='g';return 17}}
        }));
        ok=ok&&read()===17&&log==='hg';
        present=false;log='';let missing=false;
        try{read()}catch(e){missing=e instanceof ReferenceError}
        ok&&missing&&log==='h';
    )JS");
    boolean("var calls=0;Object.defineProperty(this,Symbol.unscopables,{get(){calls++}});this.visible=7;visible===7&&calls===0&&typeof Symbol.unscopables==='symbol'");
    boolean(R"JS(
        var x=1;let lexical=2;const constant=3;function fn(){return x}
        let d=Object.getOwnPropertyDescriptor(globalThis,'x'),f=Object.getOwnPropertyDescriptor(globalThis,'fn');
        globalThis.x=7;globalThis.lexical=8;
        let ok=x===7&&fn()===7&&lexical===2&&constant===3&&d.writable&&d.enumerable&&!d.configurable&&
            f.writable&&f.enumerable&&!f.configurable&&!Object.hasOwn(globalThis,'constant');
        implicit=9;ok=ok&&delete implicit&&typeof implicit==='undefined'&&!(delete x)&&!(delete lexical);
        ok&&this===globalThis&&globalThis.Object===Object&&globalThis.Array===Array;
    )JS");
    boolean(R"JS(
        var Object;let saved=Array;Array=function(){return {value:7}};
        let result=Array();let same=globalThis.Array===Array;Array=saved;
        let old=globalThis.parseInt;globalThis.parseInt=()=>4;let parsed=parseInt('9');globalThis.parseInt=old;
        let d=Object.getOwnPropertyDescriptor(globalThis,'undefined');
        typeof Object==='function'&&result.value===7&&same&&parsed===4&&parseInt===Number.parseInt&&
            parseFloat===Number.parseFloat&&parseFloat('1.2x')===1.2&&isFinite('2')&&!isFinite(undefined)&&isNaN('x')&&
            !d.writable&&!d.enumerable&&!d.configurable;
    )JS");
    boolean(R"JS(
        let log='';Object.defineProperty(globalThis,'observed',{get(){log+='g';return 5},set(v){log+='s'+v},configurable:true});
        let value=observed;observed=7;
        let ok=delete observed;
        value===5&&log==='gs7'&&ok&&typeof observed==='undefined';
    )JS");
    boolean("var Object;var Array;typeof Object==='function'&&Array(2).length===2");
    boolean("var visible=3;this.visible===3&&this.Math===Math");
    boolean("var visible=3;function read(){return this.visible}read()===3");
    boolean("var visible=3;(function(){return this.visible})()===3");
    error("'use strict';globalThis.present=1;Object.defineProperty(globalThis,'present',{writable:false});present=2;", "TypeError");
    error("'use strict';globalThis.x=1;missing=2;", "ReferenceError");
    error("let Object=Object;", "ReferenceError");
}
TEST_F(JsMvpLmd, GlobalReceiverAndLocalComputedKeys) {
    auto check = [&](const char* change, int expected, bool exposed) {
        StrBuf* source = strbuf_new();
        ASSERT_NE(source, nullptr);
        strbuf_append_str(source, R"JS(
            var extent=2;
            function note(){this.unrelatedChecksum=1}
            function Grid(){
                var width=0;
                this.resize=function(v){width=v};
                this.atIndex=function(a){return a[width+1]};
            }
            note();
        )JS");
        strbuf_append_str(source, change);
        strbuf_append_format(source, R"JS(
            let grid=new Grid();grid.resize(extent);
            let a=[3,5,6,7];a.target1=13;
            grid.atIndex(a)===%d;
        )JS", expected);
        boolean(source->str); strbuf_free(source);
        char* mir = dump("temp/mvp_lmd_global_receiver_local_keys.mir");
        ASSERT_NE(mir, nullptr);
        bool semantic_arrays = strstr(mir, "\timport\tmvp_lmd_array_index_property\n") != nullptr;
        EXPECT_EQ(semantic_arrays, exposed) << change;
        mem_free(mir);
    };
    check("", 7, false);
    check("function change(){this.extent='target'}change();", 13, true);
    check("function expose(){return this}expose().extent='target';", 13, true);
    check("function change(){this['ex'+'tent']='target'}change();", 13, true);
    check("function change(){Object.assign(this,{extent:'target'})}change();", 13, true);
    check("function change(){this.valueOf().extent='target'}change();", 13, true);
    check("globalThis.extent='target';", 13, true);
    check("function change(){this.runtimeIndex='target'}change();extent=runtimeIndex;", 13, true);
}
TEST_F(JsMvpLmd, NestedBindingPatterns) {
    boolean(R"JS(
        let [a,,{b:[c=3,...tail]},...rest]=[1,2,{b:[undefined,4,5]},6,7];
        let {x:{y=8}={},...other}={z:9};
        function f([x,{y=2},...tail]=[1,{}],{z=3,...other}={}) {
            return [x,y,z,tail.length,Object.keys(other).length];
        }
        function g(...[x,...tail]) {return x+tail.length;}
        let caught=0;
        try {throw [10,{n:11}]} catch([x,{n}]) {caught=x+n;}
        a===1 && c===3 && tail.join(',')==='4,5' && rest.join(',')==='6,7' &&
            y===8 && other.z===9 && f().join(',')==='1,2,3,0,0' && g(4,5,6)===6 && caught===21;
    )JS");
    boolean(R"JS(
        let closures=[];
        for(let [a,{b=2},...rest] of [[1,{},3],[4,{b:5},6,7]]) closures.push(()=>a+b+rest.length);
        let x='',y='';for([x,y] of ['ab']) {}
        closures[0]()===4 && closures[1]()===11 && x==='a' && y==='b';
    )JS");
    error("let [x]=null;", "TypeError");
    error("let []={length:0};", "TypeError");
    error("[]=1;", "TypeError");
    error("[,]=true;", "TypeError");
    error("[]=Symbol();", "TypeError");
    error("let [x=x]=[];", "ReferenceError");
    error("function f([x=y],y=1){}f([])", "ReferenceError");
    boolean(R"JS(
        let [a=function(){},b=()=>{},c=(function(){}),d=(0,function(){}),e=function explicit(){}]=[];
        let {f=function*(){}}={};
        function g(h=()=>{}) {return h.name}
        let descriptor=Object.getOwnPropertyDescriptor(a,'name');
        a.name==='a' && b.name==='b' && c.name==='c' && d.name==='' && e.name==='explicit' &&
            f.name==='f' && g()==='h' && !descriptor.writable && !descriptor.enumerable && descriptor.configurable;
    )JS");
}
TEST_F(JsMvpLmd, PatternIteratorEffectsAndClose) {
    boolean(R"JS(
        let prototype=Object.getPrototypeOf([].values()),closed=0;
        prototype.return=function(){closed++;return {}};
        function f(){let a;[a]=[1,2];return a;}
        let total=0,map=new Map([[1,2],[3,4]]);
        for(let [key,value] of map)total+=key+value;
        f()===1 && total===10 && closed===3;
    )JS");
    boolean(R"JS(
        let log='',n=0;
        let source={[Symbol.iterator](){log+='i';return {
            next(){log+='n';return {get done(){log+='d';return false},get value(){log+='v';return ++n}}},
            return(){log+='r';return {}}
        }}};
        let [,x]=source;
        let first=log==='indndvr' && x===1;
        log='';let []=source;
        first && log==='ir';
    )JS");
    boolean(R"JS(
        let marker={},close={},count=0,ok=true;
        function fail(){throw marker}
        for(let mode of ['next','done','value','bind']) {
            let source={[Symbol.iterator](){return {
                next(){if(mode==='next')fail();return {
                    get done(){if(mode==='done')fail();return false},
                    get value(){if(mode==='value')fail();return undefined}
                }},return(){count++;throw close}
            }}};
            try {let [x=fail()]=source;ok=false} catch(e){ok=ok && e===marker}
        }
        ok && count===1;
    )JS");
    boolean(R"JS(
        let log='',marker={};
        let inner={[Symbol.iterator](){return {next(){throw marker},return(){log+='i';return {}}}}};
        let outer={[Symbol.iterator](){return {next(){return {value:inner,done:false}},return(){log+='o';return {}}}}};
        try {let [[x]]=outer;} catch(e) {e===marker && log==='o'}
    )JS");
    error("let [x]={[Symbol.iterator](){return {next(){return {value:1}},return(){return 1}}}}", "TypeError");
    boolean(R"JS(
        let closed=0,source={[Symbol.iterator](){return {return(){closed++;return {}}}}};
        function* g(){[...{}[yield 1]]=source;}
        let iterator=g();let first=iterator.next();let last=iterator.return(2);
        first.value===1 && !first.done && last.value===2 && last.done && closed===1;
    )JS");
    boolean(R"JS(
        let receiver=false,called=false;
        Object.defineProperty(Number.prototype,Symbol.iterator,{get(){'use strict';receiver=this===7;
            return function(){'use strict';called=this===7;return {next(){return {value:9,done:false}},return(){return {}}}}}});
        let [x]=7;receiver && called && x===9;
    )JS");
}
TEST_F(JsMvpLmd, PatternAssignmentOrder) {
    boolean(R"JS(
        let log='',target={},n=0;
        function key(){log+='k';return 'x'}
        function fallback(){log+='f';return 5}
        let source={[Symbol.iterator](){return {
            next(){log+='n';return {value:undefined,done:n++>0}},return(){log+='r';return {}}
        }}};
        [target[key()]=fallback()]=source;
        let ok=log==='knfr' && target.x===5;
        log='';n=0;[...target[key()]]=source;
        ok=ok && log==='knn' && target.x.length===1;
        log='';({get a(){log+='g';return undefined}});
        ({a:target[key()]=fallback()}={get a(){log+='g';return undefined}});
        ok && log==='kgf' && target.x===5;
    )JS");
    boolean("let a=0,b=0;let source=[1,2];let value=([a,b]=source);value===source && a===1 && b===2");
    boolean(R"JS(let x,y;[x,y]='\u{1F600}a';x==='\u{1F600}' && y==='a')JS");
    boolean("let x,y;[x,{a:y=3}]=[1,{}];x===1 && y===3");
    boolean(R"JS(
        let saved=[];
        for(let [x,y] of [[1,2],'ab',[3],['c','d']]) saved.push(()=>[x,y]);
        let first=saved[0](),second=saved[1](),third=saved[2](),fourth=saved[3]();
        first[0]===1 && first[1]===2 && second[0]==='a' && second[1]==='b' &&
            third[0]===3 && third[1]===undefined && fourth[0]==='c' && fourth[1]==='d';
    )JS");
    error("for(let [x,y] of [1]);", "TypeError");
    boolean(R"JS(
        let log='',target={},source={[Symbol.iterator](){return {
            next(){log+='n';return {}},return(){log+='r';return {}}
        }}};
        function key(){log+='e';return {toString(){log+='k';return 'x'}}}
        [target[key()]=(log+='f',1)]=source;
        let ok=log==='enfkr' && target.x===1;
        log='';try {[null[key()]]=source} catch(e){ok=ok && e instanceof TypeError}
        ok && log==='enr';
    )JS");
}
TEST_F(JsMvpLmd, AnnexBCallAssignmentTargets) {
    boolean(R"JS(
        let calls=0,rhs=0,converted=0,caught=0;
        function f(){calls++;return {valueOf(){converted++;return 1}}}
        function g(){rhs++;return 1}
        try{f()=g()}catch(e){caught+=e instanceof ReferenceError}
        try{f()+=g()}catch(e){caught+=e instanceof ReferenceError}
        try{f()++}catch(e){caught+=e instanceof ReferenceError}
        try{++f()}catch(e){caught+=e instanceof ReferenceError}
        calls===4&&caught===4&&rhs===0&&converted===0;
    )JS");
    boolean(R"JS(
        let calls=0,caught=0,closed=0,marker={};
        function async(){calls++}
        let values={[Symbol.iterator](){return {next(){return {value:1,done:false}},return(){closed++;return {}}}}};
        try{for(async() in {x:1}){}}catch(e){caught+=e instanceof ReferenceError}
        try{for(async() of values){}}catch(e){caught+=e instanceof ReferenceError}
        function throws(){throw marker}
        try{throws()=1}catch(e){caught+=e===marker}
        calls===2&&caught===3&&closed===1;
    )JS");
}
TEST_F(JsMvpLmd, ObjectRestPropertySemantics) {
    boolean(R"JS(
        let log='',s=Symbol('s'),t=Symbol('s'),source={a:1,b:2,[s]:3,[t]:4};
        Object.defineProperty(source,'hidden',{value:5});
        let proxy=new Proxy(source,{
            ownKeys(o){log+='k';return Reflect.ownKeys(o)},
            getOwnPropertyDescriptor(o,k){log+=typeof k==='symbol'?'s':k;return Reflect.getOwnPropertyDescriptor(o,k)},
            get(o,k,r){log+=typeof k==='symbol'?'S':k.toUpperCase();return Reflect.get(o,k,r)}
        });
        let {a,[s]:symbol,...rest}=proxy;
        let descriptor=Object.getOwnPropertyDescriptor(rest,'b');
        a===1 && symbol===3 && rest.b===2 && rest[t]===4 && !Object.hasOwn(rest,s) &&
            !Object.hasOwn(rest,'a') && !Object.hasOwn(rest,'hidden') &&
            descriptor.writable && descriptor.enumerable && descriptor.configurable && log==='ASkbBhiddensS';
    )JS");
    boolean(R"JS(
        let source={get a(){delete this.b;this.c=3;return 1},b:2};
        let {...rest}=source;
        let {[{toString(){return 'x'}}]:x,...other}={x:1,y:2};
        let {0:first,...chars}='abc';
        rest.a===1 && !Object.hasOwn(rest,'b') && !Object.hasOwn(rest,'c') &&
            x===1 && other.y===2 && !Object.hasOwn(other,'x') && first==='a' && chars[1]==='b';
    )JS");
    boolean(R"JS(
        function source(){};
        Object.defineProperty(source,'x',{enumerable:true,get(){return this===source}});
        let {...rest}=source;rest.x;
    )JS");
}
TEST_F(JsMvpLmd, PatternEarlyErrors) {
    const char* sources[] = {"for ({x:y?.z=42} of [{}]);", "for ({x:y?.z=42} in {});",
        "([x?.y]=[1]);", "({x:y?.z=1}={});", "[null]=[1];", "let [null.x]=[];",
        "[...x,]=[];", "({...x,}={});", "({cl\\u0061ss}={});", "function* f(){[yield]=[]}",
        "'use strict';({x=yield}={});", "'use strict';[...x[yield]]=[];",
        "for([...x,] of [[]]);", "for([...x,,] in {});",
        "for(const [x]=1 of []);", "for(var {x}=1 of []);", "for(var [x]=1 in {});",
        "var C=class{static *gen(){yi\\u0065ld:;}};", "null?.fn\n`hello`;",
        "(null)?.fn`hello`;", "null?.fn()`hello`;", "async function f(){aw\\u0061it:;}",
        "with({}) class C {}", "with({}) let x=1", "with({}) label: function f(){}",
        "with({}) {function f(){'use strict';with({}){}}}", "with({}) let\n[a]=0;",
        "'use strict';function f(){}f()=1", "'use strict';function f(){}++f()",
        "function f(){}f()&&=1", "function f(){}f()||=1", ("function f(){}f()?" "?=1"),
        "()=>({})=1", "async()=>({})=1", "'use strict';()=>eval=1",
        "class C { x=this.#missing }", "class C { #x; x=delete(this.#x) }",
        "class C { #x; f(){return ()=>delete this.#x} }",
        "class C{#x;static #x}", "class C{get #x(){}get #x(){}}",
        "class C{#constructor}", "class C extends (o=>o.#x){#x}",
        "class C{#x;f(){return #x}}", "class C{#x;f(){return (#x) in this}}",
        "class C{#x;f(){return this in #x}}", "class C{#x;f(){return super.#x}}",
        "class C{#x;f(){return {#x:1}}}", "class C{#x;f(){return {[#x]:1}}}",
        "class C{#x;f(){let {#x:a}={}}}", "class C{#x;f(){let {[#x]:a}={}}}",
        "class let{}", "class st\\u0061tic{}", "class C{constructor=1}",
        "class C{'constructor'}", "class C{static prototype}", "class C{static 'prototype'(){}}",
        "class C{x=arguments}", "class C{x=()=>arguments}", "class C{x=()=>super()}",
        "class C{static{arguments}}", "class C{static{()=>arguments}}", "class C{static{super()}}",
        "import.meta=1", "(import.meta)+=1", "++import.meta", "import.meta++",
        "for(import.meta of []);", "for(import.meta in {});",
        "function f(){new.target=1}", "function f(){new.target+=1}",
        "function f(){++new.target}", "function f(){new.target++}",
        "function f(){for(new.target of []);}", "function f(){for(new.target in {});}",
        "([import.meta]=[])", "({x:import.meta}={})",
        "function f(){[new.target]=[]}", "function f(){({x:new.target}={})}",
        "import('')=1", "import('')+=1", "++import('')", "import('')++",
        "for(import('') of []);", "for(import('') in {});"};
    for (const char* source : sources) {
        mvp_lmd_destroy(execution); execution=mvp_lmd_execute(source, strlen(source));
        MvpLmdFailurePhase phase; const char* type=nullptr;
        mvp_lmd_diagnostic(execution,&phase,&type);
        EXPECT_EQ(phase,MVP_LMD_PARSE_FAILURE) << source;
        EXPECT_STREQ(type,"SyntaxError") << source;
    }
    boolean("let x={};[patternGlobal]=[2];({a:x.x}={a:3});patternGlobal===2 && x.x===3");
    boolean("'use strict';function f(){return arguments[0]}f(true)");
    boolean("one:two:;true");
    boolean("let o={tag(){return true}};(o?.tag)`hello`");
    boolean("yield:;true");
    boolean("let\n[a]=[3];a===3");
    boolean("'use strict';aw\\u0061it:;true");
    boolean("let o={tag(){return this===o}};(o?.tag)`hello`&&(o?.tag)()&&(o?.tag)(...[1])");
    boolean("let n=0,ok=false;try{(null?.tag)`${++n}`}catch(e){ok=e instanceof TypeError}ok&&n===1");
    boolean("let n=0,ok=false;try{(null?.tag)(++n)}catch(e){ok=e instanceof TypeError}ok&&n===1");
    boolean("let a=[];(a?.push)(1);a[0]===1");
    boolean("let x=[0];for([x][0][0] of [3]);x[0]===3");
}
TEST_F(JsMvpLmd, ErrorConstructorsAndDescriptors) {
    boolean(R"JS(
        let constructors=[Error,EvalError,RangeError,ReferenceError,SyntaxError,TypeError,URIError];
        let names=['Error','EvalError','RangeError','ReferenceError','SyntaxError','TypeError','URIError'];
        let ok=true;
        for(let i=0;i<constructors.length;i++) {
            let C=constructors[i], cause={index:i}, e=new C('message',{cause:cause});
            let descriptor=Object.getOwnPropertyDescriptor(e,'message');
            let metadata=Object.getOwnPropertyDescriptor(C,'prototype');
            ok=ok && e instanceof C && e instanceof Error && e.constructor===C &&
                e.name===names[i] && e.message==='message' && e.cause===cause &&
                e.toString()===names[i]+': message' && Object.keys(e).length===0 &&
                descriptor.writable && descriptor.configurable && !descriptor.enumerable &&
                !metadata.writable && !metadata.configurable && !metadata.enumerable &&
                Object.prototype.toString.call(e)==='[object Error]' &&
                !Object.hasOwn(C(),'message') && !Object.hasOwn(C(undefined),'message');
            if(i) ok=ok && Object.getPrototypeOf(C)===Error &&
                Object.getPrototypeOf(C.prototype)===Error.prototype;
            try {throw e;} catch(thrown) {ok=ok && thrown===e;}
        }
        ok && TypeError.name==='TypeError' && TypeError.length===1 &&
            Error.prototype.toString.call({name:'',message:'x'})==='x' &&
            Error.prototype.toString.call({name:'x',message:''})==='x' &&
            Error.prototype.toString.call({})==='Error';
    )JS");
}
TEST_F(JsMvpLmd, NativeErrorIdentityAndConstructionOrder) {
    boolean(R"JS(
        let ok=true;
        try {null.x;} catch(e) {ok=e instanceof TypeError && e.constructor===TypeError;}
        try {new Array(-1);} catch(e) {ok=ok && e instanceof RangeError && e.constructor===RangeError;}
        try {missingName;} catch(e) {ok=ok && e instanceof ReferenceError;}
        try {JSON.parse('{');} catch(e) {ok=ok && e instanceof SyntaxError;}
        let log='', cause={};
        function Target(){}
        let target=new Proxy(Target,{get(t,k,r){log+='p';return Reflect.get(t,k,r);}});
        let message={toString(){log+='m';return 'message';}};
        let options=new Proxy({cause:cause},{has(t,k){log+='h';return true;},get(t,k){log+='g';return t[k];}});
        let e=Reflect.construct(TypeError,[message,options],target);
        ok && log==='pmhg' && Object.getPrototypeOf(e)===Target.prototype &&
            e.message==='message' && e.cause===cause && Object.prototype.toString.call(e)==='[object Error]';
    )JS");
    boolean("let marker={};try{new Error({toString(){throw marker}})}catch(e){e===marker}");
    boolean("let marker={};try{Error.prototype.toString.call({get name(){throw marker}})}catch(e){e===marker}");
    error("Error.prototype.toString.call(null)", "TypeError");
}
TEST_F(JsMvpLmd, NativeErrorSubclassConstruction) {
    boolean(R"JS(
        class Base extends TypeError {}
        class Derived extends Base {code=7;constructor(message){super(message);this.extra=9}}
        let e=new Derived('bad');
        e instanceof Derived&&e instanceof Base&&e instanceof TypeError&&e instanceof Error&&
            Object.getPrototypeOf(e)===Derived.prototype&&e.code===7&&e.extra===9&&e.message==='bad'&&
            e.toString()==='TypeError: bad'&&Object.prototype.toString.call(e)==='[object Error]';
    )JS");
    boolean(R"JS(
        let C=class extends AggregateError {field=3};
        let cause={},marker={},e=new C([marker],'many',{cause});
        let caught;try{throw e}catch(v){caught=v}
        caught===e&&e.cause===cause&&e.errors[0]===marker&&e.field===3&&e instanceof C&&
            e instanceof AggregateError&&Object.getOwnPropertyDescriptor(e,'message').enumerable===false;
    )JS");
    boolean(R"JS(
        class E extends Error{};let target={};
        class Returns extends E {constructor(){return target}}
        let log='';function Alternate(){}
        let proxy=new Proxy(Alternate,{get(t,k,r){if(k==='prototype')log+='p';return Reflect.get(t,k,r)}});
        let e=Reflect.construct(E,[{toString(){log+='m';return 'x'}}],proxy);
        new Returns()===target&&Object.getPrototypeOf(e)===Alternate.prototype&&log==='pm'&&
            Object.prototype.toString.call(e)==='[object Error]'&&e.message==='x';
    )JS");
    boolean(R"JS(
        class E extends TypeError{};function Plain(){}
        let plain=Reflect.construct(Plain,[],E),e=new E('x');
        let fake=Reflect.construct(Plain,[],TypeError);
        Object.prototype.toString.call(plain)==='[object Object]'&&plain instanceof E&&
            Object.prototype.toString.call(fake)==='[object Object]'&&fake instanceof TypeError&&
            Object.prototype.toString.call(e)==='[object Error]'&&Reflect.ownKeys(e).join(',')==='message'&&
            Object.prototype.toString.call(Object.create(e))==='[object Object]'&&
            Object.prototype.toString.call(new Proxy(e,{}))==='[object Object]'&&
            Object.prototype.toString.call(Error.prototype)==='[object Object]'&&
            Object.prototype.toString.call(TypeError.prototype)==='[object Object]';
    )JS");
    Item value=run("class E extends RangeError{};new E('x')");
    ASSERT_EQ(get_type_id(value),LMD_TYPE_MAP);
    MvpLmdClass* cls=mvp_lmd_class_record(value);
    ASSERT_NE(cls,nullptr);
    ASSERT_NE(cls->ast,nullptr);
    ASSERT_NE(cls->nominal.base,nullptr);
    EXPECT_TRUE(lambda_nominal_derives_from(&cls->nominal,cls->nominal.base));
    MvpLmdFailurePhase phase;const char* type=nullptr;
    error("class E extends TypeError{};throw new E('x')","Uncaught JavaScript throw");
    mvp_lmd_diagnostic(execution,&phase,&type);
    EXPECT_EQ(phase,MVP_LMD_RUNTIME_FAILURE);EXPECT_STREQ(type,"TypeError");
}
TEST_F(JsMvpLmd, AggregateErrorIterationAndCause) {
    boolean(R"JS(
        let order='', cause={};
        let iterable={[Symbol.iterator](){order+='i';let n=0;return {next(){return {value:++n,done:n>2}}}}};
        let e=AggregateError(iterable,{toString(){order+='m';return 'all'}},{get cause(){order+='c';return cause}});
        let descriptor=Object.getOwnPropertyDescriptor(e,'errors');
        e instanceof Error && e instanceof AggregateError && e.constructor===AggregateError &&
            e.errors.join(',')==='1,2' && e.cause===cause && order==='mci' &&
            e.toString()==='AggregateError: all' && AggregateError.length===2 &&
            descriptor.writable && descriptor.configurable && !descriptor.enumerable;
    )JS");
    error("AggregateError({length:1,0:1})", "TypeError");
}
TEST_F(JsMvpLmd, FailurePhasesRemainDistinct) {
    const char* sources[] = {"let = ;", "async function f(){}", "null.x", "throw new TypeError('x')", "1+1",
        "function f(){with({}){eval('unknown')}}f()", "Function('return unknown')", "class E extends Error{};new E('x')",
        "function f(){try{new RegExp('a','d')}finally{return 42}}f()",
        "function f(){try{null.x}finally{return 42}}f()",
        "function f(){try{new RegExp('a','v')}finally{return 42}}f()"};
    MvpLmdFailurePhase expected[] = {MVP_LMD_PARSE_FAILURE, MVP_LMD_SUCCESS,
        MVP_LMD_RUNTIME_FAILURE, MVP_LMD_RUNTIME_FAILURE, MVP_LMD_SUCCESS,
        MVP_LMD_RUNTIME_FAILURE, MVP_LMD_SUCCESS, MVP_LMD_SUCCESS,
        MVP_LMD_SUCCESS, MVP_LMD_SUCCESS, MVP_LMD_SUCCESS};
    for (size_t i=0;i<sizeof(sources)/sizeof(sources[0]);i++) {
        SCOPED_TRACE(sources[i]);
        mvp_lmd_destroy(execution); execution=mvp_lmd_execute(sources[i], strlen(sources[i]));
        MvpLmdFailurePhase phase; const char* type=nullptr;
        mvp_lmd_diagnostic(execution,&phase,&type);
        EXPECT_EQ(phase,expected[i]);
        if (i==8 || i==9 || i==10) {
            Item value=mvp_lmd_result(execution);
            ASSERT_TRUE(get_type_id(value)==LMD_TYPE_INT || get_type_id(value)==LMD_TYPE_FLOAT);
            EXPECT_EQ(it2d(value),42);
        }
        if(i==0) EXPECT_STREQ(type,"SyntaxError");
        else if(i==2 || i==3) EXPECT_STREQ(type,"TypeError");
        else if(i==5) EXPECT_STREQ(type,"ReferenceError");
        else EXPECT_EQ(type,nullptr);
    }
}
TEST_F(JsMvpLmd, PrimitiveBinaryConversions) {
    boolean(R"JS(
        let events='';
        function add(a,b){return a+b;}
        function less(a,b){return a<b;}
        let left={valueOf(){events+='l';return 2;}}, right={valueOf(){events+='r';return 3;}};
        let values=[2,1.5,'3',left,4n,new Number(2.5),null,undefined];
        let ok=add(values[0],values[1])===3.5 && add(values[2],1)==='31' &&
            add(values[3],right)===5 && events==='lr';
        events='';
        ok=ok && less(values[3],right) && events==='lr' && add(values[4],1n)===5n &&
            add(values[5],1)===3.5 && add(values[6],2)===2 &&
            !less(values[7],2) && !less(NaN,2) && less(2,Infinity) &&
            1/add(-0,-0)===-Infinity;
        let failures=0;
        try{add(values[4],1);}catch(e){if(e instanceof TypeError)failures++;}
        try{less(Symbol(),1);}catch(e){if(e instanceof TypeError)failures++;}
        ok && failures===2;
    )JS");
}
TEST_F(JsMvpLmd, PrimitiveWrapperIdentityAndStorage) {
    boolean(R"JS(
        let s=new String('a\ud83d\ude00'), n=new Number(1.25), b=new Boolean(false);
        let symbol=Symbol('tag'), boxedSymbol=Object(symbol), bigint=Object(123n);
        let descriptor=Object.getOwnPropertyDescriptor(s,'1');
        let length=Object.getOwnPropertyDescriptor(s,'length');
        s instanceof String && s.valueOf()==='a\ud83d\ude00' && s.toString()==='a\ud83d\ude00' &&
            s.length===3 && s[0]==='a' && s[1]==='\ud83d' && s[2]==='\ude00' &&
            Object.keys(s).join(',')==='0,1,2' && descriptor.enumerable &&
            !descriptor.writable && !descriptor.configurable && !length.enumerable && !length.writable &&
            n instanceof Number && n.valueOf()===1.25 && n.toString()==='1.25' &&
            b instanceof Boolean && b.valueOf()===false && b.toString()==='false' && !!b &&
            boxedSymbol.valueOf()===symbol && boxedSymbol.toString()==='Symbol(tag)' &&
            bigint.valueOf()===123n && bigint.toString(16)==='7b' &&
            Object.prototype.toString.call(s)==='[object String]' &&
            Object.prototype.toString.call(n)==='[object Number]' &&
            Object.prototype.toString.call(b)==='[object Boolean]' &&
            Object.getPrototypeOf(5)===Number.prototype &&
            Object.getPrototypeOf('x')===String.prototype &&
            String.prototype.valueOf()==='' && Number.prototype.valueOf()===0 &&
            Boolean.prototype.valueOf()===false;
    )JS");
    error("Number.prototype.valueOf.call({})", "TypeError");
    error("String.prototype.valueOf.call(new Number(1))", "TypeError");
    error("BigInt.prototype.valueOf()", "TypeError");
    error("new BigInt(1)", "TypeError");
    error("'use strict';let s=new String('abc');s[0]='d'", "TypeError");
}
TEST_F(JsMvpLmd, PrimitiveConstructorsAsValues) {
    boolean(R"JS(
        let text=String, number=Number, boolean=Boolean, integer=BigInt;
        text() === '' && text(12)==='12' && text(Symbol('x'))==='Symbol(x)' &&
            number()===0 && number('1.5')===1.5 && number(123n)===123 &&
            boolean()===false && boolean({})===true && boolean(0)===false &&
            integer('0xff')===255n && integer(true)===1n && integer(12)===12n &&
            [1,2].map(text).join(',')==='1,2' && String(Symbol())==='Symbol()' &&
            new Number().valueOf()===0 && new String().valueOf()==='' &&
            Object.prototype.valueOf.call(7).valueOf()===7;
    )JS");
    error("BigInt(1.5)", "RangeError");
    error("BigInt('abc')", "SyntaxError");
    error("BigInt('0x+1')", "SyntaxError");
    error("BigInt('0x 1')", "SyntaxError");
    error("BigInt(undefined)", "TypeError");
    error("String({[Symbol.toPrimitive](){return Symbol('x')}})", "TypeError");
    error("new String(Symbol('x'))", "TypeError");
    error("String(Object(Symbol('x')))", "TypeError");
    boolean("let x={valueOf(){return 17n}};Number(x)===17 && (new Number(x)).valueOf()===17");
    boolean("BigInt('\\u3000 42 \\ufeff')===42n && BigInt('\\u2029')===0n && BigInt(1e100).toString().length===101");
    boolean("Error.prototype.toString.name==='toString' && Number.prototype.valueOf.name==='valueOf' && !Object.getOwnPropertyDescriptor(Number.prototype.valueOf,'name').writable");
}
TEST_F(JsMvpLmd, PrimitiveWrapperJsonAndGenericApply) {
    boolean(R"JS(
        let n=new Number(3),s=new String('x'),b=new Boolean(false), order='';
        n.valueOf=function(){order+='n';return 9};
        s.toString=function(){order+='s';return 'y'};
        b.valueOf=function(){throw 1};
        let json=JSON.stringify([n,s,b,Object(Symbol('x'))]);
        function add(a,b){return this.offset+a+b}
        let list={get length(){order+='l';return 2},get 0(){order+='a';return 3},get 1(){order+='b';return 4}};
        json==='[9,"y",false,{}]' && order==='ns' &&
            add.apply({offset:1},list)===8 && order==='nslab' &&
            Object.prototype.toString.call(BigInt.prototype)==='[object BigInt]' &&
            Object.prototype.toString.call(Symbol.prototype)==='[object Symbol]';
    )JS");
    error("JSON.stringify(Object(1n))", "TypeError");
    error("(function(){}).apply(null,'abc')", "TypeError");
}
TEST_F(JsMvpLmd, Test262HostCompletionOwnership) {
    MvpLmdHost host = {0, nullptr, nullptr, true};
    Item result=run("globalThis.__lambda_test262_async_required=true;globalThis.__lambda_test262_async_done=true;({value:7})", &host);
    ASSERT_EQ(get_type_id(result), LMD_TYPE_MAP);
    const char* source="globalThis.__lambda_test262_async_required=true;globalThis.__lambda_test262_async_done=false;function setTimeout(){};42";
    mvp_lmd_destroy(execution);execution=mvp_lmd_execute(source,strlen(source),nullptr,&host);
    ASSERT_NE(mvp_lmd_diagnostic(execution),nullptr);
    EXPECT_NE(strstr(mvp_lmd_diagnostic(execution),"async test did not call $DONE"),nullptr);
}
TEST_F(JsMvpLmd, BoundFunctionObservableMetadata) {
    boolean(R"JS(
        function target(a,b,c){return this.base+a+b+c}
        let bind=target.bind, normal=target.bind({base:1},2), order='', proto={};
        let correct=normal.name==='bound target' && normal.length===2 && normal(3,4)===10 &&
            !Object.getOwnPropertyDescriptor(normal,'length').writable;
        Object.defineProperty(target,'length',{get(){order+='l';return Infinity},configurable:true});
        Object.defineProperty(target,'name',{get(){order+='n';return 'renamed'},configurable:true});
        let proxy=new Proxy(target,{
            getPrototypeOf(t){order+='p';return proto},
            getOwnPropertyDescriptor(t,k){if(k==='length')order+='h';return Reflect.getOwnPropertyDescriptor(t,k)}
        });
        let bound=bind.call(proxy,null,1);
        correct && order==='phln' && bound.length===Infinity && bound.name==='bound renamed' &&
            Object.getPrototypeOf(bound)===proto;
    )JS");
}
TEST_F(JsMvpLmd, FunctionIntrinsicReflection) {
    boolean(R"JS(
        function named(a,b=1) { /* retained source */ return a+b; }
        let p=Function.prototype, descriptor=Object.getOwnPropertyDescriptor(Function,'prototype');
        let thrower=Object.getOwnPropertyDescriptor(p,'caller');
        let ok=typeof p==='function' && p()===undefined && p.name==='' && p.length===0 &&
            p.constructor===Function && Object.getPrototypeOf(named)===p && Object.getPrototypeOf(Function)===p &&
            Object.getPrototypeOf(p)===Object.prototype && Function.name==='Function' && Function.length===1 &&
            !descriptor.writable && !descriptor.enumerable && !descriptor.configurable &&
            p.call.name==='call' && p.call.length===1 && p.apply.name==='apply' && p.apply.length===2 &&
            p.bind.name==='bind' && p.bind.length===1 && named.name==='named' && named.length===1 &&
            p.toString.call(named)==='function named(a,b=1) { /* retained source */ return a+b; }' &&
            p.toString.call(named.bind(null)).indexOf('[native code]')>=0 &&
            thrower.get===thrower.set && !Object.isExtensible(thrower.get);
        try{p.caller;}catch(e){ok=ok && e instanceof TypeError;}
        ok && Math.pow(2,32)===4294967296 && Math.pow(1,Infinity)!==Math.pow(1,Infinity);
    )JS");
    error("Function.prototype.toString.call({})", "TypeError");
    error("new Function.prototype()", "TypeError");
    boolean(R"JS(
        class Explicit { constructor(){} method(a){return a;} }
        class Implicit {}
        Function.prototype.toString.call(Explicit)==='class Explicit { constructor(){} method(a){return a;} }' &&
        Function.prototype.toString.call(Implicit)==='class Implicit {}' &&
        Function.prototype.toString.call(Explicit.prototype.method)==='method(a){return a;}';
    )JS");
    boolean(R"JS(
        let events='', pow=Math.pow;
        let left={valueOf(){events+='l';return 2.5}}, right={valueOf(){events+='r';return 2}};
        let ignored={valueOf(){throw 7}};
        let ok=pow(left,right,ignored)===6.25 && events==='lr';
        events='';ok && Math.pow(left,right,ignored)===6.25 && events==='lr';
    )JS");
}
TEST_F(JsMvpLmd, ArgumentsMappingAndEscape) {
    boolean(R"JS(
        function make(a,b) {
            let args=arguments;
            a=2.25; let first=args[0]; args[1]=3.5;
            return {args:args, first:first, second:b, read(){return a},write(x){a=x}};
        }
        let value=make(1.25,2.5,9);value.write(4.75);
        let a=value.args;
        let ok=a.length===3 && value.first===2.25 && value.second===3.5 && a[0]===4.75 && a[2]===9 &&
            value.read()===4.75 && Object.prototype.toString.call(a)==='[object Arguments]' &&
            !Array.isArray(a) && a.callee===make && Object.keys(a).join(',')==='0,1,2' &&
            Object.values(a).join(',')==='4.75,3.5,9' && a[Symbol.iterator]===Array.prototype.values &&
            Object.assign({},a)[0]===4.75 && JSON.stringify(a)==='{"0":4.75,"1":3.5,"2":9}';
        let sum=0;for(let v of a)sum+=v;
        a[0]=6.25;ok && sum===17.25 && value.read()===6.25;
    )JS");
    boolean("function f(a,a){arguments[0]=7;let x=a;arguments[1]=9;return x===2&&a===9&&arguments[0]===7;}f(1,2)");
    boolean("function f(a){a=5;return arguments.length===0&&!Object.hasOwn(arguments,'0')}f()");
    boolean("function f(x){return ()=>arguments}let read=f(7);read()===read()&&read()[0]===7");
    boolean("function f(arguments){return arguments}f(9)===9");
    boolean("function f(){var arguments;return arguments.length}f(1,2)===2");
    boolean("function outer(){var arguments=undefined;return (function(a,b){return arguments.length})()}outer()===0");
    boolean("function outer(){return ((arguments)=>()=>arguments)(7)()}outer(1)===7");
    boolean("function outer(){return (()=>{let arguments=9;return ()=>arguments})()()}outer(1)===9");
    boolean("(function arguments(){return typeof arguments})()==='object'");
}
TEST_F(JsMvpLmd, ArgumentsDescriptorsAndUnmapped) {
    boolean(R"JS(
        function f(a,b,c){
            let x=arguments;
            Object.defineProperty(x,'0',{writable:false});a=9;
            Object.defineProperty(x,'1',{get(){return 8}});b=10;
            delete x[2];x[2]=11;c=12;
            return x[0]===1 && x[1]===8 && x[2]===11 && a===9 && b===10 && c===12;
        }
        function frozen(a){Object.freeze(arguments);a=7;return arguments[0]===1;}
        function strict(a){'use strict';a=8;let x=arguments;
            try{x.callee;}catch(e){return e instanceof TypeError && x[0]===1 && !Array.isArray(x);}}
        function defaults(a=2,b=arguments[0]){a=7;return arguments[0]===1 && b===1;}
        f(1,2,3)&&frozen(1)&&strict(1)&&defaults(1);
    )JS");
    boolean("function f(a=()=>arguments){var arguments=7;return a().length===0&&arguments===7}f()");
    boolean("function f(a=()=>arguments){let arguments=7;return a().length===0&&arguments===7}f()");
    boolean("function f(a=arguments.length){function arguments(){return 3}return a===0&&arguments()===3}f()");
    boolean("function f(a=()=>arguments){var arguments;return a()===arguments}f()");
    boolean("function f(a){let x=arguments,k=Symbol('0');x['00']=7;x['-0']=8;x[k]=9;return a===1&&x[0]===1&&x['00']===7&&x['-0']===8&&x[k]===9}f(1)");
}
TEST_F(JsMvpLmd, ArgumentsDescriptorAliasLifetime) {
    boolean(R"JS(
        function f(a) {
            let x=arguments; a=3.25;
            Object.defineProperty(x,'0',{configurable:false});
            let rejected=Reflect.defineProperty(x,'0',{get(){return 9}});
            a=8.25; let ok=!rejected && x[0]===8.25;
            let child=Object.create(x); Reflect.set(x,'0',6.5,child);
            ok=ok && a===8.25 && x[0]===8.25 && child[0]===6.5;
            Object.seal(x); a=9.5;
            ok=ok && x[0]===9.5 && Object.getOwnPropertyDescriptor(x,'0').value===9.5;
            Object.freeze(x); a=10.75;
            return ok && a===10.75 && x[0]===9.5;
        }
        f(1);
    )JS");
}
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
        print('plain',3);
        console.log('host',123,true,null,undefined);
        process.stdout.write(fs.readFileSync('temp/mvp_host_file.txt','utf8'));
        typeof clock==='function' && clock()>=first && first>=0 &&
          process.argv.length===3 && process.argv[2]==='123';
    )JS", &host);
    EXPECT_EQ(result.item, ITEM_TRUE);
    ASSERT_EQ(fclose(output), 0);
    char* bytes = NULL; size_t size = 0;
    ASSERT_TRUE(file_read_all("temp/mvp_host_output.txt", MEM_CAT_TEMP, &bytes, &size));
    const char expected[] = "plain 3\nhost 123 true null undefined\na\0b";
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
TEST_F(JsMvpLmd, NamedFunctionDefaultCapture) {
    boolean(R"JS(
        var self='outer', read, write, body;
        var fn=function self(a=(read=()=>self,write=()=>{self=null})) {
            body=()=>self;
        };
        fn();write();self='changed';
        read()===fn && body()===fn && self==='changed';
    )JS");
    boolean(R"JS(
        'use strict';let read,write,body;
        let fn=function self(a=(read=()=>self,write=()=>{self=null})) {body=()=>self};
        fn();let rejected=false;try{write()}catch(e){rejected=e instanceof TypeError}
        rejected && read()===fn && body()===fn;
    )JS");
    boolean(R"JS(
        let read,write;let fn=function* self(a=(read=()=>self,write=()=>{self=null})) {
            yield self;
        };
        let iterator=fn();let before=read();write();let value=iterator.next();
        before===fn && read()===fn && value.value===fn && !value.done;
    )JS");
    boolean(R"JS(
        let read;let fn=function self(a=(read=()=>self,(()=>{throw 7})())){};
        let threw=false;try{fn()}catch(e){threw=e===7}
        threw && read()===fn;
    )JS");
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
    boolean("isNaN(new Date('unsupported').getTime())");
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
    boolean("function f(){const keys=[0,1];const a=new Uint8Array(2);let i=Math.sqrt(9);"
        "return a[keys[i]]===undefined} f()");
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
        let calls=0,values=[0,-0,5e-324,Infinity,NaN,'2',null,undefined,{valueOf(){calls++;return -0}}];
        function numberAt(a,i){return +a[i]}
        1/numberAt(values,0)===Infinity&&1/numberAt(values,1)===-Infinity&&
          numberAt(values,2)===5e-324&&numberAt(values,3)===Infinity&&
          Number.isNaN(numberAt(values,4))&&numberAt(values,5)===2&&numberAt(values,6)===0&&
          Number.isNaN(numberAt(values,7))&&1/numberAt(values,8)===-Infinity&&calls===1;
    )JS");
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
TEST_F(JsMvpLmd, RecursiveCallsPreserveActivationAndArguments) {
    // JC24 forbids tail-call elimination, including otherwise numeric bodies.
    boolean("function f(n){if(n<=0)return 0;return f(n-1)}let caught=false;try{f(Infinity)}catch(e){caught=e instanceof RangeError}caught");

    numeric("function sum(n,a){if(n===0)return a;return sum(n-1,a+n)} sum(100,0)", 5050.0);
    numeric("function swap(n,a,b){if(n===0)return a*10+b;return swap(n-1,b,a)} swap(101,1,2)", 21);
    numeric("var count=0;function f(n,a){if(n===0)return a;return f(n-1,a+1,count++)} f(100,0)+count", 200);
    numeric("function f(n){var previous=x;var x=n;if(n===0)return previous===undefined?1:0;return f(n-1)} f(100)", 1);
    numeric("var a=[];function f(n){let row=['x'+n];a[0]=row;if(n===0)return row.length;return f(n-1)} f(200)", 1);
    numeric("function f(n){if(n===0)return 1;return f(n-1)+1} f(20)", 21);
    error("function f(n){if(n===0){const x=1;x=2}return f(n-1)} f(10)", "TypeError");
    // exhaustion must be checked independently of the host's available recursion depth.
    error("function f(n){if(n===0)return 0;return f(n+1)+1} f(1)", "Stack overflow");
}
TEST_F(JsMvpLmd, RejectsUnsupportedUnitBeforeExecution) {
    boolean("if(false){({get a(){return 1}})}true");
    boolean("function f(){return ()=>x; var x=1} f()()===undefined");
    numeric("{let x=1; function f(){return x}} f()", 1);
    numeric("function f(a=1){return a} f()", 1);
    boolean("function f(){return this} f()===f()");
    boolean("let a=[1,,2];a.length===3&&!Object.hasOwn(a,1)&&a[1]===undefined");
    numeric("var x=1; with([]){x=2} x", 2);
    numeric("try{1}catch(e){2}", 1);
    numeric("async function f(){return 1} 1", 1);
    numeric("async function* f(){yield 1} 1", 1);
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
    EXPECT_EQ(strstr(source, "mvp_lmd_with_binding"), nullptr);
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
TEST_F(JsMvpLmd, ArrayConstructionDelegatesPreciseOwnership) {
    Item empty = run("new Array()");
    ASSERT_EQ(get_type_id(empty), LMD_TYPE_ARRAY);
    EXPECT_EQ(empty.array->length, 0);
    EXPECT_EQ(empty.array->capacity, 0);
    EXPECT_EQ(empty.array->items, nullptr);
    boolean(R"JS(
        let ok=true, keep=[];
        for(let i=0;i<32;i++) {
            let empty=[], dense=new Array(3), sparse=new Array(1000000);
            ok=ok && empty.length===0 && !Object.hasOwn(dense,'0') && !Object.hasOwn(sparse,'999999');
            empty.push(5e-324,-0,{i}); dense[2]=empty; sparse[999999]=dense;
            keep.push(sparse);
        }
        for(let i=0;i<keep.length;i++) {
            let dense=keep[i][999999], empty=dense[2];
            ok=ok && !Object.hasOwn(dense,'0') && empty[0]===5e-324 &&
                Object.is(empty[1],-0) && empty[2].i===i;
        }
        ok;
    )JS");
}
TEST_F(JsMvpLmd, KeyProjectionAvoidsGettersAndRetainsDescriptorOrder) {
    boolean(R"JS(
        let hits=0, symbol=Symbol('key'), a=[1,,3];
        Object.defineProperty(a,'0',{enumerable:false});
        Object.defineProperty(a,'1',{get(){hits++;return 2},enumerable:true});
        a.named=4; a[symbol]=5;
        Object.defineProperty(a,'hidden',{get(){hits++;return 6}});
        let keys=Object.keys(a), names=Object.getOwnPropertyNames(a), all=Reflect.ownKeys(a);
        let seen='';for(let k in a)seen+=k+',';
        keys.join(',')==='1,2,named' && names.join(',')==='0,1,2,length,named,hidden' &&
        all[all.length-1]===symbol && Object.getOwnPropertySymbols(a)[0]===symbol &&
        seen==='1,2,named,' && hits===0;
    )JS");
    boolean(R"JS(
        let a=[1,2,3], hits=0;
        Object.defineProperty(a,'0',{get(){hits++;Object.defineProperty(a,'1',{enumerable:false});return 7},enumerable:true});
        let keys=Object.keys(a), values=Object.values(a);
        keys.join(',')==='0,1,2' && values.join(',')==='7,3' && hits===1;
    )JS");
    boolean(R"JS(
        let sparse=[];sparse[1000000]=7;sparse.extra=8;
        Object.defineProperty(sparse,'3',{value:9,enumerable:false});
        let typed=new Uint16Array([4,5]);typed.extra=6;
        function keys(a,b){Object.defineProperty(arguments,'1',{enumerable:false});return Object.keys(arguments).join(',')}
        Object.keys(sparse).join(',')==='1000000,extra' &&
        Object.getOwnPropertyNames(sparse).join(',')==='3,1000000,length,extra' &&
        Object.keys(typed).join(',')==='0,1,extra' && keys(1,2)==='0';
    )JS");
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
    boolean(R"JS(
        let m=new Map();m.set(1,1);m.set(2,2);let ok=true;
        for(let i=0;i<40;i++) {
            m.get(1);ok=ok&&m.set(1,5e-324*(i+1))===m;
            let garbage=[];for(let j=0;j<20;j++)garbage.push({j});
            ok=ok&&m.get(1)===5e-324*(i+1);
            m.get(2);m.set(2,i);ok=ok&&m.get(2)===i;
        }
        m.get(1);m.set(1,-0);ok&&1/m.get(1)===-Infinity&&m.size===2;
    )JS");
    boolean(R"JS(
        let m=new Map();m.set('x',1);m.set('y',2);
        function replace(){m.clear();m.set('z',3);return 7}
        m.get('x');let same=m.set('x',replace());
        m.get('x');m.set('x',(m.delete('x'),m.set('x',4),9));
        let key={};m.set(key,1);m.get(key);let value={alive:true};m.set(key,value);
        same===m&&m.get('x')===9&&m.get('z')===3&&m.get(key)===value&&m.size===3;
    )JS");
    boolean(R"JS(
        let source='prefix-key-suffix',m=new Map(),ok=true;
        for(let i=0;i<40;i++){
            let key=source.substring(7,10);
            m.get(key);ok=ok&&m.set(key,5e-324*(i+1))===m;
            let garbage=[];for(let j=0;j<20;j++)garbage.push({j});
            ok=ok&&m.get(source.substring(7,10))===5e-324*(i+1);
        }
        ok&&m.size===1&&m.get('key')===2e-322;
    )JS");
    mir = dump("temp/mvp_lmd_map_same_value.mir");
    ASSERT_NE(mir, nullptr);
    EXPECT_NE(strstr(mir, "\timport\tmvp_lmd_same_value_zero"), nullptr);
    mem_free(mir);
    boolean(R"JS(
        let m=new Map();m.set(NaN,1);m.get(NaN);m.set(0/0,2);
        m.set(0,3);m.get(0);m.set(-0,4);
        m.set(12345678901234567890n,5);m.get(12345678901234567890n);
        m.set(12345678901234567889n+1n,6);
        m.size===3&&m.get(NaN)===2&&m.get(0)===4&&m.get(12345678901234567890n)===6;
    )JS");
    boolean("let m=new Map();m.set('😀',1);m.get('😀');m.set('\\ud83d'+'\\ude00',-0);m.size===1&&1/m.get('😀')===-Infinity");
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
TEST_F(JsMvpLmd, RepeatedPropertyReadsKeepLivePrototypeAndNullableLanes) {
    boolean(R"JS(
        class Node { constructor() { this.child=null; } }
        function read(node) { return node.child; }
        function number(node) { return +node.child; }
        let node=new Node(), child={valueOf(){return 7}}, ok=true;
        for(let i=0;i<12;i++) {
            node.child=child;
            ok=ok && read(node)===child && number(node)===7;
            node.child=null;
            ok=ok && read(node)===null && number(node)===0 && 1/number(node)===Infinity;
        }
        node.child=undefined;
        ok=ok && read(node)===undefined && Number.isNaN(number(node));
        for(let v of [1,2.5,true,false,'3',NaN,-0]) {
            node.child=v;
            ok=ok && Object.is(read(node),v) && Object.is(number(node),+v);
        }
        ok;
    )JS");
    boolean(R"JS(
        const first={value:3}, second={get value(){return this.own+1}};
        let middle=Object.create(first), object=Object.create(middle), ok=true;
        object.own=8;
        function read(o){return o.value;}
        function keys(o){let out='';for(let k in o)out+=k+',';return out;}
        for(let i=0;i<10;i++) {
            Object.setPrototypeOf(middle,first);
            ok=ok && read(object)===3 && keys(object)==='own,value,';
            Object.setPrototypeOf(middle,second);
            ok=ok && read(object)===9 && keys(object)==='own,value,';
        }
        let hits=0, proxy=new Proxy({}, {get(t,k,r){hits++;return r.own+2}});
        Object.setPrototypeOf(middle,proxy);
        ok && read(object)===10 && hits===1;
    )JS");
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
        let p={x:1},q={x:2},o=Object.create(p),ok=true;
        function read(v){return v.x}
        for(let i=0;i<20;i++){p.x=i;ok=ok&&read(o)===i&&read(q)===2}
        Object.setPrototypeOf(o,q);ok=ok&&read(o)===2;
        delete q.x;ok=ok&&read(o)===undefined;
        Object.defineProperty(q,'x',{get(){return this.y},configurable:true});
        o.y=9;ok=ok&&read(o)===9;
        Object.defineProperty(o,'x',{value:7,configurable:true});
        ok&&read(o)===7;
    )JS");
    boolean(R"JS(
        let ok=true;
        for(let parent of [{},[],function(){},null]){
            let object={v:1};Object.setPrototypeOf(object,parent);
            for(let i=0;i<6;i++)ok=ok&&Object.getPrototypeOf(object)===parent;
        }
        ok;
    )JS");
    boolean(R"JS(
        let a={read(){return this.v+1}},b={read(){return this.v+2}},o={v:10},ok=true;
        o.extra=1;
        for(let i=0;i<4;i++)ok=ok&&Object.getPrototypeOf(o)===Object.prototype;
        for(let i=0;i<40;i++){
            let parent=i%2?a:b;
            Object.setPrototypeOf(o,parent);
            ok=ok&&Object.getPrototypeOf(o)===parent&&o.read()===(i%2?11:12);
        }
        Object.setPrototypeOf(o,null);
        ok=ok&&Object.getPrototypeOf(o)===null&&o.read===undefined;
        Object.setPrototypeOf(o,a);
        Object.defineProperty(a,'read',{get(){return function(){return this.v+3}}});
        ok&&o.read()===13;
    )JS");
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
    // an absent numeric key reads the ordinary named attribute, which is missing here.
    boolean(R"JS(let a=new Int32Array(1),index=a[9];a[index]===undefined)JS");
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
    boolean("let o={};for(let k of ['z','4294967295','02','10','2','length'])o[k]=k;Object.values(o).join(',')==='2,10,z,4294967295,02,length'&&Object.keys(o).join(',')===Object.values(o).join(',')");
    boolean("let s=Symbol('0'),o={a:1,[s]:2,0:3};let keys=Reflect.ownKeys(o);keys[0]==='0'&&keys[1]==='a'&&keys[2]===s&&Object.values(o).join(',')==='3,1'");
    boolean("let o={};for(let i=0;i<3;i++)o['k'+i]=i;Object.defineProperty(o,'late',{enumerable:true,get(){delete o.k1;o.extra=9;return 7}});Object.values(o).join(',')==='0,1,2,7'&&Object.keys(o).join(',')==='k0,k2,late,extra'");
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

TEST_F(JsMvpLmd, TypedArraySearchAndRelativeIndices) {
    boolean("let p=Object.getPrototypeOf(Uint8Array.prototype),d=Object.getOwnPropertyDescriptor(p,'copyWithin');d.value.name==='copyWithin'&&d.value.length===2&&d.writable&&!d.enumerable&&d.configurable&&p.set.length===1&&p.reverse.length===0&&p.with.length===2");
    boolean("let a=new Float64Array([2,NaN,-0,2]);a.indexOf(2)===0&&a.lastIndexOf(2)===3&&a.indexOf(2,-2)===3&&a.lastIndexOf(2,-2)===0&&a.indexOf(NaN)===-1&&a.includes(NaN)&&a.includes(0)&&!a.includes('2')");
    boolean("let a=new Int16Array([4,5,6]);a.at(-1)===6&&a.at(Infinity)===undefined&&a.at(-Infinity)===undefined&&a.indexOf(4,Infinity)===-1&&a.lastIndexOf(4,-Infinity)===-1&&a.lastIndexOf(6,Infinity)===2");
    boolean("let a=new BigUint64Array([9007199254740993n,2n,9007199254740993n]);a.indexOf(9007199254740993n)===0&&a.lastIndexOf(9007199254740993n)===2&&!a.includes(9007199254740992)&&a.at(-2)===2n");
    boolean("let reads=0,a=new Uint8Array(0),from={valueOf(){reads++;return 0}};a.indexOf(1,from)===-1&&!a.includes(1,from)&&a.lastIndexOf(1,from)===-1&&reads===0");
    boolean("let a=new Uint8Array([3,4]);let key='index'+'Of',method=a[key];method.call(a,4)===1");
}

TEST_F(JsMvpLmd, TypedArrayBulkMutationAndOverlap) {
    boolean("let a=new Uint8Array([1,2,3,4,5]);let result=a.set(a.subarray(0,4),1);result===undefined&&a[0]===1&&a[1]===1&&a[2]===2&&a[3]===3&&a[4]===4");
    boolean("let buffer=new ArrayBuffer(8),a=new Uint16Array(buffer),b=new Uint8Array(buffer);a.set([258,772,1286,1800]);let expected=[b[0],b[1],b[2],b[3]];a.set(b.subarray(0,4));a[0]===expected[0]&&a[1]===expected[1]&&a[2]===expected[2]&&a[3]===expected[3]");
    boolean("let a=new Int32Array([1,2,3,4,5]);a.copyWithin(1,0,4)===a&&a[0]===1&&a[1]===1&&a[2]===2&&a[3]===3&&a[4]===4&&a.reverse()===a&&a[0]===4&&a[4]===1");
    boolean("let a=new Uint8ClampedArray(3);a.fill(2.5)===a&&a[0]===2&&a[1]===2&&a[2]===2");
    boolean("let a=new BigInt64Array(3);a.fill(18446744073709551615n);a[0]===-1n&&a[1]===-1n&&a[2]===-1n");
    error("new Uint8Array(0).set(new BigInt64Array(0))", "TypeError");
    error("new BigInt64Array(0).set(new Uint8Array(0))", "TypeError");
    boolean("let log='',source={get length(){log+='l';return 0}},a=new Uint8Array(0);try{a.set(source,4294967296)}catch(e){log+=e instanceof RangeError?'r':'x'}log==='lr'");
}

TEST_F(JsMvpLmd, TypedArrayFillAndSetEffectOrder) {
    boolean("let log='',a=new Int16Array(3);a.fill({valueOf(){log+='v';return 7}},{valueOf(){log+='s';return 1}},{valueOf(){log+='e';return 2}});log==='vse'&&a[0]===0&&a[1]===7&&a[2]===0");
    boolean("let log='',a=new Int16Array(0);a.fill({valueOf(){log+='v';return 7}},{valueOf(){log+='s';return 0}},{valueOf(){log+='e';return 0}});log==='vse'");
    boolean("let log='',a=new Int16Array(3);Array.prototype.fill.call(a,{valueOf(){log+='v';return 7}},{valueOf(){log+='s';return 1}},{valueOf(){log+='e';return 2}});log==='sev'&&a[1]===7");
    boolean("let a=new Uint8Array([1,2]),log='',source={get length(){log+='l';return 2},get 0(){log+='a';return {valueOf(){log+='v';return 7}}},get 1(){log+='b';return 8}};a.set(source,{valueOf(){log+='o';return 0}});log==='olavb'&&a[0]===7&&a[1]===8");
}

TEST_F(JsMvpLmd, TypedArraySliceSpeciesAndCopyingMethods) {
    boolean("let a=new Int16Array([1,2,3,4]),b=a.slice(1,-1);b instanceof Int16Array&&b.buffer!==a.buffer&&b.length===2&&b[0]===2&&b[1]===3");
    boolean("let a=new Int16Array([1,2,3]);a.constructor={[Symbol.species]:Float64Array};let b=a.slice(1);b instanceof Float64Array&&b.length===2&&b[0]===2&&b[1]===3");
    boolean("let a=new Float64Array([5e-324,-0,NaN]);Object.defineProperty(a,'constructor',{get(){throw 9}});let b=a.toReversed(),c=a.with(-2,7);b[2]===5e-324&&1/b[1]===-Infinity&&Number.isNaN(b[0])&&c[0]===5e-324&&c[1]===7&&Number.isNaN(c[2])&&1/a[1]===-Infinity");
    boolean("let a=new BigUint64Array([9007199254740993n,2n]);let b=a.with(1,18446744073709551617n),c=a.toReversed();b[0]===9007199254740993n&&b[1]===1n&&c[1]===9007199254740993n&&a[1]===2n");
    boolean("let log='',a=new Uint8Array(1);try{a.with(Infinity,{valueOf(){log+='v';return 3}})}catch(e){log+=e instanceof RangeError?'r':'x'}log==='vr'");
    error("let a=new Int16Array([1]);a.constructor={[Symbol.species]:BigInt64Array};a.slice()", "TypeError");
}

TEST_F(JsMvpLmd, TypedArrayStableSortAndScalarOwnership) {
    boolean("let a=new Float64Array([NaN,0,-0,4,5e-324,-3]);let b=a.toSorted();b[0]===-3&&1/b[1]===-Infinity&&1/b[2]===Infinity&&b[3]===5e-324&&b[4]===4&&Number.isNaN(b[5])&&Number.isNaN(a[0])");
    boolean("let a=new BigInt64Array([9007199254740995n,9007199254740993n,9007199254740994n]);let b=a.toSorted();b[0]===9007199254740993n&&b[1]===9007199254740994n&&b[2]===9007199254740995n&&a[0]===9007199254740995n");
    boolean("let a=new Int32Array([22,11,24,13,26,15]);a.sort((x,y)=>Math.floor(x/10)-Math.floor(y/10))===a&&a[0]===11&&a[1]===13&&a[2]===15&&a[3]===22&&a[4]===24&&a[5]===26");
    boolean("let a=new Float64Array([9e100,5e-324,-9e100,1.5]);let b=a.toSorted((x,y)=>{let garbage=new BigInt64Array([1n,2n,3n]);return {valueOf(){return x-y+Number(garbage[0]-1n)}}});b[0]===-9e100&&b[1]===5e-324&&b[2]===1.5&&b[3]===9e100");
    boolean("let a=new Int32Array([1,2,3]);let b=a.toSorted((x,y)=>{a.fill(9);return y-x});b[0]===3&&b[1]===2&&b[2]===1&&a[0]===9");
    error("new Uint8Array([1]).sort(null)", "TypeError");
    error("new Uint8Array([1,2]).toSorted(()=>1n)", "TypeError");
    boolean("let a=new Int8Array([10,2,1]);Array.prototype.sort.call(a);a[0]===1&&a[1]===10&&a[2]===2");
}

TEST_F(JsMvpLmd, TypedArrayMethodsResizeDuringCoercion) {
    boolean("let buffer=new ArrayBuffer(4,{maxByteLength:8}),a=new Uint8Array(buffer);a.set([1,2,3,4]);let from={valueOf(){buffer.resize(0);return 0}};a.indexOf(undefined,from)===-1");
    boolean("let buffer=new ArrayBuffer(4,{maxByteLength:8}),a=new Uint8Array(buffer);a.set([1,2,3,4]);let from={valueOf(){buffer.resize(0);return 0}};a.includes(undefined,from)");
    boolean("let buffer=new ArrayBuffer(4,{maxByteLength:8}),a=new Uint8Array(buffer);a.set([1,2,3,4]);a.copyWithin({valueOf(){buffer.resize(8);return -2}},0,2);a[2]===1&&a[3]===2&&a[6]===0&&a[7]===0");
    boolean("let buffer=new ArrayBuffer(4,{maxByteLength:8}),a=new Uint8Array(buffer);a.set([1,2,3,4]);let b=a.slice(0,{valueOf(){buffer.resize(8);return 8}});b.length===4&&b[3]===4");
    boolean("let buffer=new ArrayBuffer(2,{maxByteLength:8}),a=new Uint8Array(buffer);a.set([1,2]);let b=a.with(3,{valueOf(){buffer.resize(8);return 7}});b.length===2&&b[0]===1&&b[1]===2");
    boolean("let buffer=new ArrayBuffer(4,{maxByteLength:8}),a=new Uint8Array(buffer);a.set([1,2,3,4]);a.set({get length(){buffer.resize(0);return 4},0:5,1:6,2:7,3:8});a.length===0");
}

// Insert after the fixture; completion assertions inspect the returned Array after the shared job drain.
static void expect_async_flags(Item result, int count) {
    ASSERT_EQ(get_type_id(result), LMD_TYPE_ARRAY);
    ASSERT_EQ(result.array->length, count);
    for (int i = 0; i < count; i++) EXPECT_EQ(result.array->items[i].item, ITEM_TRUE) << i;
}

TEST_F(JsMvpLmd, PromiseMetadataAndConstruction) {
    boolean("let p=new Promise(()=>{});p instanceof Promise&&Object.getPrototypeOf(p)===Promise.prototype&&Object.prototype.toString.call(p)==='[object Promise]'&&Promise.length===1&&Promise.prototype.then.length===2&&Promise[Symbol.species]===Promise");
    boolean("let p=new Promise(()=>{}),calls=0;Object.defineProperty(Promise.prototype,'x',{set(v){calls+=v},configurable:true});p.x=3;calls===3&&!Object.hasOwn(p,'x')");
    boolean("class P extends Promise{}let p=new P(()=>{});p instanceof P&&p instanceof Promise&&Object.getPrototypeOf(p)===P.prototype&&p.then(()=>{}) instanceof P");
    boolean("let p=new Promise(()=>{}),n=0;Object.defineProperty(p,'x',{get(){n++;return this===p},configurable:true});p.x&&n===1&&Object.getOwnPropertyDescriptor(p,'x').get.call(p)");
    error("Promise(()=>{})", "TypeError");
    error("new Promise(1)", "TypeError");
    boolean("let caught=false;try{Promise.prototype.then.call({})}catch(e){caught=e instanceof TypeError}caught");
    error("new Promise.resolve(1)", "TypeError");
}

TEST_F(JsMvpLmd, PromiseResolutionAndJobOrder) {
    expect_async_flags(run(R"JS(
        let out=[],log=[];
        Promise.resolve({get then(){log.push('g');return function(resolve,reject){log.push('t');resolve(5);reject(9);resolve(8)}}})
            .then(v=>{log.push('r');out.push(v===5)});
        log.push('s');Promise.resolve().then(()=>log.push('m'));
        Promise.resolve().then(()=>{}).then(()=>out.push(log.join(',')==='g,s,t,m,r'));
        out
    )JS"),2);
    expect_async_flags(run(R"JS(
        let out=[],resolve,p=new Promise(r=>{resolve=r});resolve(p);
        p.then(()=>out.push(false),e=>out.push(e instanceof TypeError));
        Promise.resolve({get then(){throw 42}}).catch(e=>out.push(e===42));
        new Promise(resolve=>{resolve({then(r){r(7)}});throw 9}).then(v=>out.push(v===7),()=>out.push(false));
        Promise.resolve(3).finally(()=>Promise.resolve(8)).then(v=>out.push(v===3));
        Promise.reject(4).finally(()=>{}).catch(v=>out.push(v===4));
        out
    )JS"),5);
}

TEST_F(JsMvpLmd, PromiseCombinatorsAndSpecies) {
    expect_async_flags(run(R"JS(
        let out=[];
        Promise.all([Promise.resolve(1),2]).then(v=>out.push(v.length===2&&v[0]===1&&v[1]===2));
        Promise.allSettled([Promise.resolve(1),Promise.reject(2)]).then(v=>out.push(v[0].status==='fulfilled'&&v[0].value===1&&v[1].status==='rejected'&&v[1].reason===2));
        Promise.any([Promise.reject(1),Promise.resolve(2)]).then(v=>out.push(v===2));
        Promise.any([Promise.reject(1),Promise.reject(2)]).catch(e=>out.push(e instanceof AggregateError&&e.errors.join(',')==='1,2'));
        Promise.race([Promise.resolve(3),Promise.resolve(4)]).then(v=>out.push(v===3));
        let values=[1,2];values[Symbol.iterator]=function*(){yield 7};
        Promise.all(values).then(v=>out.push(v.length===1&&v[0]===7));
        let cap=Promise.withResolvers();cap.promise.then(v=>out.push(v===9));cap.resolve(9);cap.reject(8);
        out
    )JS"),7);
    expect_async_flags(run(R"JS(
        let out=[],closed=0,iterable={[Symbol.iterator](){return {next(){return {done:false,value:1}},return(){closed++;return {}}}}};
        class P extends Promise {static resolve(){throw 7}}
        P.all(iterable).catch(e=>out.push(e===7&&closed===1));
        let promise=Promise.resolve(1),reads=0;
        promise.constructor={get [Symbol.species](){reads++;return Promise}};
        let next=promise.then(v=>v+1);next.then(v=>out.push(v===2&&reads===1));
        out
    )JS"),2);
}

TEST_F(JsMvpLmd, AsyncAwaitMirAndPreciseOwnership) {
    expect_async_flags(run(R"JS(
        let out=[],order=[];
        async function f(a){order.push('a');let held={n:a};let v=await Promise.resolve(5e-324);let garbage=[];for(let i=0;i<2000;i++)garbage.push({i});order.push('b');return held.n===7&&v===5e-324}
        let p=f(7);order.push('s');p.then(v=>out.push(v&&order.join(',')==='a,s,b'));
        async function g(){try{await Promise.reject(3)}catch(e){return e}finally{await 1}}
        g().then(v=>out.push(v===3));
        async function h(){let x=-0;await 0;return x}h().then(v=>out.push(1/v===-Infinity));
        let arrow=async x=>{await 0;return x+1};arrow(4).then(v=>out.push(v===5));
        out
    )JS"),4);
    expect_async_flags(run(R"JS(
        let out=[],p=Promise.resolve(7),reads=0;
        Object.defineProperty(p, 'constructor', {get(){reads++;return Promise}});
        Object.defineProperty(Promise, Symbol.species, {get(){throw 99},configurable:true});
        async function f(){return await p}
        let result=f();delete Promise[Symbol.species];
        result.then(v=>out.push(v===7&&reads===1),()=>out.push(false));
        out
    )JS"),1);
}

TEST_F(JsMvpLmd, TypedArrayStaticFromAndOf) {
    boolean("let a=Uint8Array.from([1,258,3],(v,i)=>v+i),b=Int16Array.of(4,-5,6);a.length===3&&a[0]===1&&a[1]===3&&a[2]===5&&b.length===3&&b[0]===4&&b[1]===-5&&b[2]===6");
    boolean("let a=BigUint64Array.of(18446744073709551617n,9007199254740993n),b=BigInt64Array.from(a,x=>x+1n);a[0]===1n&&b[0]===2n&&b[1]===9007199254740994n");
    boolean("class Words extends Uint16Array{}let a=Words.from([7,8]),b=Words.of(9,10);a instanceof Words&&b instanceof Words&&a[1]===8&&b[0]===9");
    boolean("let self={offset:3},a=Int32Array.from({length:2,0:5,1:6},function(v,i){return this.offset+v+i},self);a[0]===8&&a[1]===10");
    boolean("let p=Object.getPrototypeOf(Uint8Array),f=Object.getOwnPropertyDescriptor(p,'from'),o=Object.getOwnPropertyDescriptor(p,'of');f.value===Int16Array.from&&f.value.length===1&&f.value.name==='from'&&f.writable&&!f.enumerable&&f.configurable&&o.value.length===0&&o.value.name==='of'");
    error("Uint8Array.from.call(()=>{},[])", "TypeError");
    error("Uint8Array.of.call(()=>{},1)", "TypeError");
    error("Uint8Array.from([],null)", "TypeError");
    error("BigInt64Array.of(1)", "TypeError");
}

TEST_F(JsMvpLmd, TypedArrayStaticIteratorAndConstructionOrder) {
    boolean("let log='',source={get [Symbol.iterator](){log+='i';return function(){log+='c';let n=0;return {get next(){log+='n';return function(){log+='s';return n<2?{value:++n,done:false}:{done:true}}}}}}};function C(n){log+='C';return new Uint8Array(n)}let a=Uint8Array.from.call(C,source,x=>{log+='m';return x+4});log==='icnsssCmm'&&a[0]===5&&a[1]===6");
    boolean("let source=[1,2];function C(n){source[0]=9;return new Uint8Array(n)}let a=Uint8Array.from.call(C,source);a[0]===1&&source[0]===9");
    boolean("let log='',source={get [Symbol.iterator](){log+='i';return null},get length(){log+='l';return 2},get 0(){log+='a';return 1},get 1(){log+='b';return 2}};function C(n){log+='C';return new Uint8Array(n)}let a=Uint8Array.from.call(C,source,x=>{log+='m';return x});log==='ilCambm'&&a[0]===1&&a[1]===2");
    boolean("let a=[1,2];a[Symbol.iterator]=function(){let done=false;return {next(){if(done)return {done:true};done=true;return {done:false,value:7}}}};let b=Uint8Array.from(a);b.length===1&&b[0]===7");
    boolean("let a=new Uint8Array([1,2]);a[Symbol.iterator]=null;a[0]=8;let b=Int16Array.from(a);b.length===2&&b[0]===8&&b[1]===2");
    boolean("let read=0,source={get [Symbol.iterator](){read++;throw 7}};try{Uint8Array.from.call(()=>{},source)}catch(e){}try{Uint8Array.from(source,null)}catch(e){}read===0");
    boolean("let value=0;function C(n){value=n;return new Uint8Array(0)}try{Uint8Array.from.call(C,{length:4294967296})}catch(e){value=e instanceof TypeError?value:-1}value===4294967296");
}

TEST_F(JsMvpLmd, TypedArrayStaticResultValidationAndOwnership) {
    error("Uint8Array.from.call(function(){return {}},[])", "TypeError");
    error("Uint8Array.of.call(function(){return new Uint8Array(0)},1)", "TypeError");
    error("let buffer=new ArrayBuffer(0),a=new Uint8Array(buffer);buffer.transfer();Uint8Array.from.call(function(){return a},[])", "TypeError");
    error("let buffer=new ArrayBuffer(4,{maxByteLength:8}),a=new Uint8Array(buffer,2,2);buffer.resize(0);Uint8Array.of.call(function(){return a})", "TypeError");
    boolean("let source=[5e-324,9e100,-9e100];function C(n){let junk=BigInt64Array.of(1n,2n,3n);return new Float64Array(n+junk.length-3)}let a=Float64Array.of.call(C,source[0],source[1],source[2]);a[0]===5e-324&&a[1]===9e100&&a[2]===-9e100");
    boolean("let source=new BigInt64Array([9007199254740993n,9007199254740994n]),a=BigInt64Array.from(source,(v,i)=>{let junk=new Float64Array([5e-324,9e100]);return v+BigInt(i)+BigInt(junk.length-2)});a[0]===9007199254740993n&&a[1]===9007199254740995n");
}

TEST_F(JsMvpLmd, TypedArrayJoinAndSharedToString) {
    boolean("let a=new Int16Array([1,-2,3]);a.join('|')==='1|-2|3'&&a.toString()==='1,-2,3'&&Uint8Array.prototype.toString===Array.prototype.toString");
    boolean("let a=new BigUint64Array([9007199254740993n,18446744073709551615n]);a.join(':')==='9007199254740993:18446744073709551615'");
    boolean("let buffer=new ArrayBuffer(3,{maxByteLength:6}),a=new Uint8Array(buffer);a.set([1,2,3]);a.join({toString(){buffer.resize(0);return '|'}})==='||'");
    boolean("let a=new Uint8Array([1]);a.join=()=>7;a.toString()===7");
    boolean("let a=new Uint8Array([1]);a.join=null;a.toString()==='[object Uint8Array]'");
    boolean("let reads=0;new Uint8Array(0).join({toString(){reads++;return '|'}})===''&&reads===1");
    error("new Uint8Array([1]).join(Symbol())", "TypeError");
}

TEST_F(JsMvpLmd, TypedArrayLocaleElementsAndResize) {
    boolean("let a=new Int16Array([12,-3]),b=new BigInt64Array([1n,9007199254740993n]);a.toLocaleString()==='12,-3'&&b.toLocaleString()==='1,9007199254740993'");
    boolean("let original=Number.prototype.toLocaleString,seen=[];Number.prototype.toLocaleString=function(){'use strict';seen.push(this);return arguments.length+':'+this};let text=new Int16Array([2,3]).toLocaleString('ignored',{unused:true});Number.prototype.toLocaleString=original;text==='0:2,0:3'&&seen[0]===2&&seen[1]===3");
    boolean("let original=Number.prototype.toLocaleString,buffer=new ArrayBuffer(3,{maxByteLength:6}),a=new Uint8Array(buffer),calls=0;a.set([1,2,3]);Number.prototype.toLocaleString=function(){calls++;buffer.resize(0);return 'x'};let text=a.toLocaleString();Number.prototype.toLocaleString=original;text==='x,,'&&calls===1");
    boolean("let original=BigInt.prototype.toLocaleString,calls=0;BigInt.prototype.toLocaleString=function(){calls++;return {toString(){return 'b'}}};let text=BigInt64Array.of(1n,2n).toLocaleString();BigInt.prototype.toLocaleString=original;text==='b,b'&&calls===2");
}

TEST_F(JsMvpLmd, DynamicInterpreterGlobalCellsAndIdentity) {
    boolean(R"JS(
function check(value) { if (!value) throw new Error('dynamic bridge check'); }
var bridgeGlobal = 7;
let bridgeLexical = 9;
let add = Function('a', 'b', 'return a + b');
check(add(2, 3) === 5);
check(add.length === 2 && add.name === 'anonymous');
check(add.toString().indexOf('return a + b') >= 0);
function localScope() {
  let bridgeGlobal = 99;
  return Function('return bridgeGlobal + bridgeLexical')();
}
check(localScope() === 16);
let counter = Function('start', 'let n = start; return function(delta) { n += delta; return n; }')(10);
check(counter(2) === 12 && counter(3) === 15);
let self = Function('return function named() { return named; }')();
check(self() === self);
let target = {value: 4};
let edit = Function('o', 'f', 'o.value += f(o); return o;');
check(edit(target, function(o) { check(o === target); return 3; }) === target);
check(target.value === 7);
let thrown = {identity: 1};
try { Function('x', 'throw x')(thrown); check(false); } catch (e) { check(e === thrown); }
check(Function('x', 'try { throw x; } catch (e) { return e; }')(thrown) === thrown);
check(Function('"use strict"; const x = 1; try { x = 2; } catch (e) { return e instanceof TypeError; }')());
let C = Function('value', 'this.value = value;');
let instance = new C(42);
check(instance.value === 42 && instance instanceof C && C.prototype.constructor === C);
check(Function('return this')() === globalThis);
check(Function('"use strict"; return this').call(null) === null);
let arrow = Function('"use strict"; return () => this').call(1.25);
check(arrow() === 1.25);
check(Function('return typeof missingDynamicName')() === 'undefined');
let before = bridgeLexical;
check(Function('callback', 'callback(); return bridgeLexical;')(function() { bridgeLexical = 21; }) === 21);
bridgeLexical = before;
let conversions = 0;
let source = {toString: function() { conversions++; return 'return 8'; }};
check(Function(source)() === 8 && conversions === 1);
check((0, eval)('1 + 2') === 3);
check((0, eval)(17) === 17);
check((0, eval)('var bridgePublished = 30; bridgePublished') === 30);
check(globalThis.bridgePublished === 30);
check((0, eval)('bridgeLexical = 44; bridgeLexical') === 44);
check(bridgeLexical === 44);
check((0, eval)('var bridgeGlobal; bridgeGlobal') === 7);
let made = Function('return {x: 2, f: function() { return 3; }}')();
check(made.x === 2 && made.f() === 3);
check(Function('a', 'b', 'return a + b')({valueOf: function() { return 1.25; }}, {valueOf: function() { return 2.5; }}) === 3.75);
check(Function('return 5n * 7n')() === 35n);

true;
)JS");
}

TEST_F(JsMvpLmd, AsyncReceiverOwnsWideScalarAcrossSuspension) {
    expect_async_flags(run(R"JS(
        let out=[],release,wait=new Promise(r=>release=r);
        async function f(){"use strict";await wait;return this}
        function start(){return f.call(5e-324)}
        start().then(v=>out.push(v===5e-324));
        f.call(-0).then(v=>out.push(v===0&&1/v===-Infinity));
        let n=1;for(let i=0;i<10000;i++)n=n/1.0001;
        release();out
    )JS"),2);
}

TEST_F(JsMvpLmd, PromiseRecordsCreateOwnDataProperties) {
    expect_async_flags(run(R"JS(
        let out=[],calls=0;
        for(let key of ['status','value','reason','promise','resolve','reject']){
            let descriptor=Object.create(null);
            descriptor.set=function(v){calls++};descriptor.configurable=true;
            Object.defineProperty(Object.prototype,key,descriptor);
        }
        let cap=Promise.withResolvers();
        out.push(Object.hasOwn(cap,'promise')&&Object.hasOwn(cap,'resolve')&&Object.hasOwn(cap,'reject')&&calls===0);
        Promise.allSettled([1,Promise.reject(2)]).then(v=>{
            out.push(v[0].status==='fulfilled'&&v[0].value===1&&Object.hasOwn(v[0],'status')&&Object.hasOwn(v[0],'value'));
            out.push(v[1].status==='rejected'&&v[1].reason===2&&Object.hasOwn(v[1],'status')&&Object.hasOwn(v[1],'reason')&&calls===0);
        });out
    )JS"),3);
}

TEST_F(JsMvpLmd, PromiseCustomSpeciesSettlesInReactionJob) {
    expect_async_flags(run(R"JS(
        let out=[],log=[],created,release;
        function C(executor){
            created=this;
            executor(function(v){"use strict";log.push('resolve');out.push(this===undefined&&v===8)},
                function(e){out.push(false)});
        }
        let p=new Promise(r=>release=r);p.constructor={[Symbol.species]:C};
        let next=p.then(v=>{log.push('handler');return v+1});
        out.push(next===created);release(7);
        Promise.resolve().then(()=>{log.push('other');out.push(log.join(',')==='handler,resolve,other')});
        out
    )JS"),3);
    expect_async_flags(run(R"JS(
        let out=[],value={then:null};let p=Promise.resolve(value);
        value.then=resolve=>resolve(4);
        p.then().then(v=>out.push(v===4));
        Promise.resolve(2).then(function(v){"use strict";out.push(this===undefined&&v===2)});
        Promise.reject(3).catch(function(v){"use strict";out.push(this===undefined&&v===3)});
        function C(executor){executor(function(v){"use strict";out.push(this===undefined&&v===5)},
            function(v){"use strict";out.push(this===undefined&&v===6)})}
        Promise.resolve.call(C,5);Promise.reject.call(C,6);
        out
    )JS"),5);
}

TEST_F(JsMvpLmd, GenericLocaleStringSharedKernel) {
    boolean("['',''].toLocaleString()===','&&['x',null,undefined,2,3n].toLocaleString()==='x,,,2,3'");
    boolean("Array.prototype.toLocaleString.call('ab')==='a,b'&&'x'.toLocaleString()==='x'&&new String('xy').toLocaleString()==='xy'");
    boolean("let a=[{toLocaleString(){a.push(9);return 'x'}},2];a.toLocaleString()==='x,2'&&a.length===3");
    boolean("let a=[{toLocaleString(){a.length=1;return 'x'}},2,3];a.toLocaleString()==='x,,'");
    boolean("let a=[{toLocaleString(){return arguments.length}}];a.toLocaleString('en',{style:'currency'})==='0'");
    boolean("let a=new Uint8Array([1,2]);a.toLocaleString()==='1,2'&&a.toString()===a.join(',')&&Uint8Array.prototype.toString===Array.prototype.toString");
    error("Array.prototype.toLocaleString.call(null)", "TypeError");
    error("Array.prototype.toLocaleString.call(undefined)", "TypeError");
}

TEST_F(JsMvpLmd, GenericLocaleStringGettersAndReceiver) {
    boolean("let log='',element={get toLocaleString(){log+='g';return function(){log+=this===element?'t':'f';return {toString(){log+='s';return 'x'}}}}},a={get length(){log+='l';return 2},get 0(){log+='a';return element},get 1(){log+='b';return null}};Array.prototype.toLocaleString.call(a)==='x,'&&log==='lagtsb'");
    boolean("let obj={get toString(){return function(){return this===obj?7:9}}};Object.prototype.toLocaleString.call(obj)===7");
    boolean("let sentinel={},obj={get toString(){throw sentinel}},caught=false;try{Object.prototype.toLocaleString.call(obj)}catch(e){caught=e===sentinel}caught");
    boolean("let sentinel={},obj={get toLocaleString(){throw sentinel}},caught=false;try{[obj].toLocaleString()}catch(e){caught=e===sentinel}caught");
    error("Object.prototype.toLocaleString.call({toString:1})", "TypeError");
    error("Object.prototype.toLocaleString.call(null)", "TypeError");
    error("[{toLocaleString:1}].toLocaleString()", "TypeError");
}

TEST_F(JsMvpLmd, PromiseIteratorObjectsAndSuspendedCallOwnership) {
    expect_async_flags(run(R"JS(
        let out=[],iterator=new Promise(()=>{}),step=new Promise(()=>{}),index=0;
        step.value=11;step.done=false;
        iterator.next=()=>index++?{done:true}:step;
        iterator[Symbol.iterator]=function(){return this};
        Promise.all(iterator).then(v=>out.push(v.length===1&&v[0]===11));
        out
    )JS"),1);
    expect_async_flags(run(R"JS(
        let out=[],release,wait=new Promise(r=>release=r);
        async function parked(){await wait;return Promise.resolve(7)}
        function* sequence(){yield 1;yield Promise.resolve(9)}
        let iterator;
        Promise.resolve().then(()=>{
            iterator=sequence();out.push(iterator.next().value===1);
            return parked();
        }).then(v=>out.push(v===7));
        Promise.resolve().then(()=>{}).then(()=>{
            iterator.next().value.then(v=>out.push(v===9));release();
        });out
    )JS"),3);
}

TEST_F(JsMvpLmd, DynamicInterpreterClassesAndArrays) {
    boolean(R"JS(
function check(value) { if (!value) throw new Error('dynamic class/array bridge'); }
let make = Function('Base', 'return class Dynamic extends Base {}');
let D = make(Uint8Array);
let a = new D(3);
check(a instanceof D && a instanceof Uint8Array && a.length === 3);
a[1] = 42;
check(a[1] === 42 && Object.getPrototypeOf(a) === D.prototype);
check(Object.getPrototypeOf(D) === Uint8Array);
check(Object.getPrototypeOf(D.prototype) === Uint8Array.prototype);
check(D.name === 'Dynamic' && D.length === 0);
check(D.toString() === 'class Dynamic extends Base {}');
check(Reflect.ownKeys(D).join(',') === 'length,name,prototype');
check(Object.getOwnPropertyNames(D).join(',') === 'length,name,prototype');
let E = make(D);
let b = new E(2);
check(b instanceof E && b instanceof D && b instanceof Uint8Array && b.length === 2);
check(make(Uint8Array) !== D);
let C = Function('return class {}')();
check(new C() instanceof C);
try { C(); check(false); } catch (e) { check(e instanceof TypeError); }
try { make({}); check(false); } catch (e) { check(e instanceof TypeError); }
try { Function('return class C extends C {}')(); check(false); } catch (e) { check(e instanceof ReferenceError); }
let names = Function('let C = class {}; return C.name')();
check(names === 'C');
let values = Function('x', 'return [x, , undefined, 1.25]')(7);
check(values.length === 4 && values[0] === 7 && !(1 in values));
check(2 in values && values[2] === undefined && values[3] === 1.25);
let P = make(Promise);
let promise = new P(function(resolve) { resolve(9); });
check(promise instanceof P && promise instanceof Promise);
check(Object.getPrototypeOf(promise) === P.prototype);
check(Object.getPrototypeOf(Object.create(promise)) === promise);
let resolver;
let p = new Promise(function(resolve) { resolver = resolve; });
let same = Function('x', 'return x')(resolver);
check(same === resolver);
check(Reflect.ownKeys(resolver).indexOf('MVP [[HostWrapper]]') === -1);
resolver(1);
true
)JS");
}

TEST_F(JsMvpLmd, PromiseNativeCarrierMemberDispatch) {
    expect_async_flags(run("let out=[];Promise.resolve(1).then(x=>out.push(x===1));out"),1);
    boolean("let p=Promise.resolve(1);p.extra=7;let k='extra';p[k]===7&&Object(p)===p");
    boolean("let p=Promise.resolve(1),hits=0;Object.defineProperty(p,'x',{get(){hits++;return this===p},configurable:true});p.x&&hits===1");
    boolean("let p=Promise.resolve(1);p.test=function(){return this===p};p.test()");
}

TEST_F(JsMvpLmd, PromiseNativeCarrierExtensibility) {
    boolean("let p=Promise.resolve(1);Object.isExtensible(p)&&Reflect.defineProperty(p,'x',{value:2,writable:true,configurable:true})&&p.x===2");
    boolean("let p=Promise.resolve(1);p.x=2;Object.preventExtensions(p);p.x=3;p.y=4;!Object.isExtensible(p)&&p.x===3&&!Object.hasOwn(p,'y')&&!Reflect.defineProperty(p,'z',{value:5})");
    boolean("let p=Promise.resolve(1);p.x=2;Object.freeze(p);p.x=3;Object.isFrozen(p)&&Object.isSealed(p)&&p.x===2");
    boolean("let p=Promise.resolve(1);Object.defineProperty(p,'x',{get(){return this===p},configurable:true});Object.seal(p);Object.isSealed(p)&&p.x&&!Reflect.deleteProperty(p,'x')");
    error("'use strict';let p=Promise.resolve(1);Object.preventExtensions(p);p.x=2", "TypeError");
    error("let p=Promise.resolve(1);Object.preventExtensions(p);Object.defineProperty(p,'x',{value:2})", "TypeError");
}

TEST_F(JsMvpLmd, PromiseConstructorObjectIdentityAndPrototype) {
    boolean("let p=Promise.resolve(1);function F(){return p}new F()===p");
    boolean("let p=Promise.resolve(1);class B{}class D extends B{constructor(){return p}}new D()===p");
    boolean("let p=Promise.resolve(1);let F=Function('p','return p');new F(p)===p");
    boolean("let p=Promise.resolve(1);function F(){}F.prototype=p;Object.getPrototypeOf(new F())===p");
}

TEST_F(JsMvpLmd, LocaleOverridesBoxSloppyPrimitiveReceiver) {
    boolean("let calls=[];BigInt.prototype.toLocaleString=function(){calls.push(this);return 'x'};let a=new BigInt64Array([42n,0n]);a.toLocaleString()==='x,x'&&calls.length===2&&typeof calls[0]==='object'&&calls[0].valueOf()===42n&&new BigInt64Array(calls)[1]===0n");
    boolean("let calls=[];BigInt.prototype.toLocaleString=function(){'use strict';calls.push(this);return 'x'};new BigInt64Array([42n]).toLocaleString()==='x'&&typeof calls[0]==='bigint'&&calls[0]===42n");
    boolean("let calls=[];Number.prototype.toLocaleString=function(){calls.push(this);return 'x'};new Float64Array([5e-324,-0]).toLocaleString()==='x,x'&&typeof calls[0]==='object'&&calls[0].valueOf()===5e-324&&1/calls[1].valueOf()===-Infinity");
    boolean("let calls=[];Number.prototype.toLocaleString=function(){'use strict';calls.push(this);return 'x'};new Uint8Array([42]).toLocaleString()==='x'&&typeof calls[0]==='number'&&calls[0]===42");
    boolean("function f(){return typeof this==='object'&&this.valueOf()}f.call(42n)===42n&&f.call('x')==='x'&&f.call(true)===true");
}

TEST_F(JsMvpLmd, PromiseObjectPrototypeClassification) {
    boolean("let p=Promise.resolve(1);Promise.prototype.isPrototypeOf(p)&&Object.prototype.isPrototypeOf(p)");
    boolean("let p=Promise.resolve(1),q={};p.__proto__=q;Object.getPrototypeOf(p)===q");
    boolean("let p=Promise.resolve(1),q={};q.__proto__=p;Object.getPrototypeOf(q)===p");
}

TEST_F(JsMvpLmd, PromiseObjectCoercionClassification) {
    boolean("String(Promise.resolve(1))==='[object Promise]'");
    boolean("let p=Promise.resolve(1);p.valueOf=function(){return 7};Number(p)===7");
    boolean("let p=Promise.resolve(1),log='';let o={valueOf(){log+='v';return p},toString(){log+='s';return '7'}};Number(o)===7&&log==='vs'");
    boolean("let p=Promise.resolve(1),log='';let o={toString(){log+='s';return p},valueOf(){log+='v';return 7}};String(o)==='7'&&log==='sv'");
    error("let p=Promise.resolve(1),o={toString(){return p},valueOf(){return p}};String(o)", "TypeError");
    error("let p=Promise.resolve(1);let o={[Symbol.toPrimitive](){return p}};Number(o)", "TypeError");
}

TEST_F(JsMvpLmd, ObjectIsSharedSameValueScalars) {
    boolean("Object.is()&&Object.is(undefined)&&Object.is(null,null)&&!Object.is(null,undefined)&&Object.is(NaN,NaN)&&!Object.is(NaN,0)");
    boolean("Object.is(0,0)&&Object.is(-0,-0)&&!Object.is(0,-0)&&!Object.is(-0,0)&&Object.is(Infinity,Infinity)&&!Object.is(Infinity,-Infinity)");
    boolean("Object.is(5e-324,5e-324)&&Object.is(9e100,9e100)&&!Object.is(5e-324,0)&&!Object.is(9e100,-9e100)");
    boolean("Object.is(9007199254740993n,9007199254740992n+1n)&&!Object.is(9007199254740993n,9007199254740992n)&&!Object.is(1n,1)");
    boolean("Object.is('ab','a'+'b')&&!Object.is('1',1)&&Object.is(true,true)&&!Object.is(true,1)");
}

TEST_F(JsMvpLmd, ObjectIsIdentityAndNoConversion) {
    boolean("let a={},b={},x=[],y=[],f=function(){},g=function(){};Object.is(a,a)&&!Object.is(a,b)&&Object.is(x,x)&&!Object.is(x,y)&&Object.is(f,f)&&!Object.is(f,g)");
    boolean("let a=Symbol('x'),b=Symbol('x');Object.is(a,a)&&!Object.is(a,b)&&!Object.is(a,'x')&&Object.is(Symbol.iterator,Symbol.iterator)");
    boolean("let a=Promise.resolve(1),b=Promise.resolve(1);Object.is(a,a)&&!Object.is(a,b)");
    boolean("let count=0,o={valueOf(){count++;return 1},toString(){count++;return '1'}};!Object.is(o,1)&&Object.is(o,o)&&count===0");
    boolean("let count=0,target={},p=new Proxy(target,{get(){count++;throw 1}});Object.is(p,p)&&!Object.is(p,target)&&count===0");
    boolean("let r=Proxy.revocable({},{}),p=r.proxy;r.revoke();Object.is(p,p)");
}

TEST_F(JsMvpLmd, ObjectIsMetadataAndBorrowedCalls) {
    boolean("let d=Object.getOwnPropertyDescriptor(Object,'is'),n=Object.getOwnPropertyDescriptor(Object.is,'name'),l=Object.getOwnPropertyDescriptor(Object.is,'length');d.value===Object.is&&d.writable&&!d.enumerable&&d.configurable&&n.value==='is'&&!n.writable&&!n.enumerable&&n.configurable&&l.value===2&&!l.writable&&!l.enumerable&&l.configurable");
    boolean("let is=Object.is;is.call(null,NaN,NaN)&&is.apply({},[-0,-0])&&!is.apply(null,[0,-0])&&Object['is'](1,1)");
    boolean("let O=Object,is=O.is;O.is=function(){return 7};Object.is(1,2)===7&&is(1,1)");
    boolean("let m=new Map();m.set(-0,7);Object.is(m.get(0),7)&&!Object.is(-0,0)&&[NaN].includes(NaN)");
    error("new Object.is(0,0)", "TypeError");
}

TEST_F(JsMvpLmd, DynamicInterpretedArgumentsMapping) {
    boolean(R"JS(
function check(value) { if (!value) throw new Error('interpreted arguments mapping'); }
let f=Function('a','b','arguments[0]=7; b=9; return a===7 && arguments[1]===9 && arguments.length===3 && arguments[2]===3 && arguments.callee===f');
globalThis.f=f;
check(f(1,2,3));
check(Function('a','a','arguments[0]=7; let old=a; arguments[1]=9; return old===2 && a===9 && arguments[0]===7')(1,2));
check(Function('a','a','a=8; return arguments[0]===1 && arguments.length===1 && !Object.hasOwn(arguments,"1")')(1));
check(Function('a','a=5; return arguments.length===0 && !Object.hasOwn(arguments,"0")')());
check(Function('return typeof arguments==="object" && !Array.isArray(arguments) && delete arguments===false')());
check(Function('arguments','return arguments')(9)===9);
check(Function('var arguments; return arguments.length')(1,2)===2);
check(Function('return (function arguments(){return typeof arguments})()')()==='object');
true;
    )JS");
}

TEST_F(JsMvpLmd, DynamicInterpretedArgumentsDetach) {
    boolean(R"JS(
function check(value) { if (!value) throw new Error('interpreted arguments detach'); }
check(Function('a','arguments[0]=4; delete arguments[0]; a=5; return a===5 && !("0" in arguments)')(1));
check(Function('a','arguments[0]=4; delete arguments[0]; return a')(1)===4);
check(Function('a','arguments[0]=4; Object.defineProperty(arguments,"0",{writable:false}); a=5; return a===5 && arguments[0]===4')(1));
check(Function('a','Object.defineProperty(arguments,"0",{value:4,writable:false}); a=5; return a===5 && arguments[0]===4')(1));
check(Function('a','arguments[0]=4; Object.defineProperty(arguments,"0",{get:function(){return 99}}); return a===4 && arguments[0]===99')(1));
check(Function('a','let saved=arguments; arguments=null; saved[0]=4; a=5; return saved[0]===5 && a===5 && arguments===null')(1));
check(Function('a','Object.freeze(arguments); a=7; return arguments[0]===1 && a===7')(1));
true;
    )JS");
}

TEST_F(JsMvpLmd, DynamicInterpretedArgumentsUnmapped) {
    boolean(R"JS(
function check(value) { if (!value) throw new Error('interpreted unmapped arguments'); }
check(Function('a','"use strict"; arguments[0]=7; a=9; return arguments[0]===7 && a===9')(1));
check(Function('"use strict"; try { arguments.callee; } catch(e) { return e instanceof TypeError; }')());
check(Function('a=2','b=arguments[0]','a=7; return arguments[0]===1 && b===1')(1));
check(Function('a=arguments.length','return a')(undefined,7)===2);
check(Function('a=b','b=1','return a') instanceof Function);
let tdz=false;
try { Function('a=b','b=1','return a')(); } catch(e) { tdz=e instanceof ReferenceError; }
check(tdz);
check(Function('a=()=>arguments','var arguments=7; return a().length===0 && arguments===7')());
check(Function('a=()=>arguments','let arguments=7; return a().length===0 && arguments===7')());
check(Function('a=arguments.length','function arguments(){return 3} return a===0 && arguments()===3')());
check(Function('a=()=>arguments','var arguments; return a()===arguments')());
true;
    )JS");
}

TEST_F(JsMvpLmd, DynamicInterpretedArgumentsClosures) {
    boolean(R"JS(
function check(value) { if (!value) throw new Error('interpreted arguments escaped cells'); }
let make=Function('a','let saved=arguments; return [()=>a,function(v){a=v},saved,function(){return arguments.length+a}]');
let value=make(5e-324);
let read=value[0],write=value[1],args=value[2],nested=value[3];
check(read()===5e-324 && nested(1,2)===2);
args[0]=1e-323;
check(read()===1e-323);
write(5e-324);
check(args[0]===5e-324);
let old=read();
args[0]=1e-323;
let garbage=[];
for(let i=0;i<2000;i++) garbage.push({i:i});
check(old===5e-324 && read()===1e-323 && args[0]===1e-323);
delete args[0];
write(-0);
check(1/read()===-Infinity && !Object.hasOwn(args,'0'));
let arrows=Function('return ()=>arguments')(4);
check(arrows()===arrows() && arrows()[0]===4);
let detached=Function('a','let saved=arguments; return [()=>a,function(){arguments=null},saved]')(1);
detached[1](); detached[2][0]=7;
check(detached[0]()===7);
true;
    )JS");
}

TEST_F(JsMvpLmd, DynamicInterpretedArgumentsParameterReentry) {
    boolean(R"JS(
// the read must outlive a second operand writing the same mapped cell.
let f=Function('a','let saved=arguments; return a + (saved[0]=1e-323,0)');
if(f(5e-324)!==5e-324) throw new Error('interpreted mapped scalar read aliases mutable cell');
let closure=Function('x','return function(){return x+(x=1e-323,0)}')(5e-324);
if(closure()!==5e-324) throw new Error('interpreted lexical scalar read aliases mutable slot');
true;
    )JS");
}

TEST_F(JsMvpLmd, MathSharedCatalogSurface) {
    boolean("Math.log(1)===0&&Math.log10(100)===2&&Math.log2(8)===3&&Math.exp(0)===1&&Math.tan(0)===0");
    boolean("Math.asin(0)===0&&Math.acos(1)===0&&Math.atan(0)===0&&Math.atan2(0,1)===0&&Math.cbrt(27)===3");
    boolean("Math.sinh(0)===0&&Math.cosh(0)===1&&Math.tanh(0)===0&&Math.asinh(0)===0&&Math.acosh(1)===0&&Math.atanh(0)===0&&Math.expm1(0)===0&&Math.log1p(0)===0");
    boolean("Math.clz32('1')===31&&Math.imul('4294967295','5')===-5&&Math.fround(1.337)===1.3370000123977661&&Math.sign(-2)===-1");
    boolean("let names=['log','log10','log2','exp','tan','asin','acos','atan','atan2','cbrt','fround','sinh','cosh','tanh','asinh','acosh','atanh','expm1','log1p'];let ok=true;for(let n of names){let x=Math[n]();ok=ok&&x!==x}ok&&Math.clz32()===32&&Math.imul()===0&&Math.hypot()===0");
    boolean("Math.E>2.718&&Math.E<2.719&&Math.LN2>0.693&&Math.LN10>2.302&&Math.LOG2E>1.442&&Math.LOG10E>0.434&&Math.SQRT2>1.414&&Math.SQRT1_2>0.707");
}

TEST_F(JsMvpLmd, MathSharedCoercionAndAbruptOrder) {
    boolean("let marker={},seen='';let a={valueOf(){seen+='a';return '3'}},b={valueOf(){seen+='b';return 4}};Math.hypot(a,b)===5&&seen==='ab'");
    boolean("let marker={},seen='',a={valueOf(){seen+='a';return 2}},b={valueOf(){seen+='b';throw marker}};let caught=false;try{Math.imul(a,b)}catch(e){caught=e===marker}caught&&seen==='ab'");
    boolean("let marker={},seen='',x={valueOf(){seen+='x';throw marker}};let caught=0;for(let method of ['min','max','hypot']){try{Math[method](NaN,Infinity,x)}catch(e){if(e===marker)caught++}}caught===3&&seen==='xxx'");
    boolean("let marker={},count=0,x={valueOf(){count++;throw marker}},caught=0;for(let method of ['abs','sqrt','floor','ceil','round','trunc','sin','cos','log','atan','cbrt','clz32','fround']){try{Math[method](x)}catch(e){if(e===marker)caught++}}caught===13&&count===13");
    boolean("let count=0,ignored={valueOf(){count++;throw 7}};Math.log(1,ignored)===0&&Math.imul(2,3,ignored)===6&&count===0");
    error("Math.log(Symbol('x'))", "TypeError");
    error("Math.clz32(1n)", "TypeError");
}

TEST_F(JsMvpLmd, MathHypotUnboundedAndExtremeValues) {
    boolean("let values=[];for(let i=0;i<80;i++)values.push(0);values[79]=7;Math.hypot(...values)===7&&Math.hypot.apply(null,values)===7");
    boolean("let marker={},values=[];for(let i=0;i<70;i++)values.push(0);values[69]={valueOf(){throw marker}};let caught=false;try{Math.hypot(...values)}catch(e){caught=e===marker}caught");
    boolean("let x=Math.hypot(3e200,4e200),y=Math.hypot(3e-200,4e-200);x/5e200>0.99999999999999&&x/5e200<1.00000000000001&&y/5e-200>0.99999999999999&&y/5e-200<1.00000000000001");
    boolean("Math.hypot(NaN,Infinity)===Infinity&&Math.hypot(-Infinity,NaN)===Infinity&&Math.hypot(NaN)!==Math.hypot(NaN)&&1/Math.hypot(-0)===Infinity");
}

TEST_F(JsMvpLmd, MathNamespaceMetadataAndOldLanes) {
    boolean("let names=['E','LN2','LN10','LOG2E','LOG10E','PI','SQRT2','SQRT1_2'],ok=true;for(let n of names){let d=Object.getOwnPropertyDescriptor(Math,n);ok=ok&&!d.writable&&!d.enumerable&&!d.configurable}ok");
    boolean("let d=Object.getOwnPropertyDescriptor(Math,'hypot'),n=Object.getOwnPropertyDescriptor(Math.hypot,'name'),l=Object.getOwnPropertyDescriptor(Math.hypot,'length');d.writable&&!d.enumerable&&d.configurable&&n.value==='hypot'&&!n.writable&&!n.enumerable&&n.configurable&&l.value===2&&!l.writable&&!l.enumerable&&l.configurable");
    boolean("let d=Object.getOwnPropertyDescriptor(Math,Symbol.toStringTag);d.value==='Math'&&!d.writable&&!d.enumerable&&d.configurable&&Object.prototype.toString.call(Math)==='[object Math]'");
    boolean("let keys=Object.keys(Math),p=Object.getPrototypeOf(Math);keys.length===0&&p===Object.prototype&&Math.hypot.call(null,3,4)===5");
    boolean("Math.sqrt(9)===3&&Math.sin(0)===0&&Math.floor(1.9)===1&&Math.trunc(-1.9)===-1&&Math.abs(-4)===4&&Math.min(3,2)===2&&Math.max(3,2)===3&&Math.ceil(1.1)===2&&Math.cos(0)===1&&Math.round(1.5)===2&&Math.pow(2,3)===8");
    boolean("let M=Math;1/M.ceil(-0.1)===-Infinity&&1/M.round(-0.5)===-Infinity&&1/M.min(0,-0)===-Infinity&&1/M.max(-0,0)===Infinity&&M.floor(1e100)===1e100");
    error("new Math.hypot(3,4)", "TypeError");
}

TEST_F(JsMvpLmd, UriSharedEncodingDecodingAndMetadata) {
    boolean("encodeURI('https://x/y?q=a b#c')==='https://x/y?q=a%20b#c'&&encodeURIComponent('a/b?c=d&x= y')==='a%2Fb%3Fc%3Dd%26x%3D%20y'");
    boolean("decodeURI('%3f%2f%23%20')==='%3f%2f%23 '&&decodeURIComponent('%3f%2f%23%20')==='?/# '");
    boolean("let text='é中😀';decodeURI(encodeURI(text))===text&&decodeURIComponent(encodeURIComponent(text))===text&&encodeURIComponent(text)==='%C3%A9%E4%B8%AD%F0%9F%98%80'");
    boolean("encodeURI()==='undefined'&&encodeURIComponent(null)==='null'&&decodeURI(123)==='123'&&decodeURIComponent(true)==='true'&&decodeURI('plain')==='plain'");
    boolean("let funcs=[encodeURI,decodeURI,encodeURIComponent,decodeURIComponent],names=['encodeURI','decodeURI','encodeURIComponent','decodeURIComponent'],ok=true;for(let i=0;i<4;i++){let f=funcs[i],n=Object.getOwnPropertyDescriptor(f,'name'),l=Object.getOwnPropertyDescriptor(f,'length'),d=Object.getOwnPropertyDescriptor(globalThis,names[i]);ok=ok&&n.value===names[i]&&!n.writable&&!n.enumerable&&n.configurable&&l.value===1&&!l.writable&&!l.enumerable&&l.configurable&&d.value===f&&d.writable&&!d.enumerable&&d.configurable}ok");
    error("new encodeURI('x')", "TypeError");
}

TEST_F(JsMvpLmd, UriMalformedAndThrownIdentity) {
    boolean("let count=0;for(let f of [decodeURI,decodeURIComponent])for(let s of ['%','%GG','%80','%C0%AF','%ED%A0%80','%F4%90%80%80']){try{f(s)}catch(e){if(e instanceof URIError)count++}}count===12");
    boolean("let count=0;for(let f of [encodeURI,encodeURIComponent])for(let s of ['\\uD800','\\uDC00']){try{f(s)}catch(e){if(e instanceof URIError)count++}}count===4");
    boolean("let marker={},count=0;for(let f of [encodeURI,decodeURI,encodeURIComponent,decodeURIComponent]){try{f({toString(){throw marker}})}catch(e){if(e===marker)count++}}count===4");
    boolean("let marker={},count=0,o={[Symbol.toPrimitive](hint){if(hint==='string')count++;throw marker}},caught=0;for(let f of [encodeURI,decodeURI,encodeURIComponent,decodeURIComponent]){try{f(o)}catch(e){if(e===marker)caught++}}count===4&&caught===4");
    boolean("let first,second;try{decodeURI('%')}catch(e){first=e}try{decodeURI('%')}catch(e){second=e}first instanceof URIError&&second instanceof URIError&&first!==second");
    error("decodeURIComponent(Symbol('x'))", "TypeError");
}

TEST_F(JsMvpLmd, UriWithoutInterpreterRealmCaches) {
    boolean("decodeURIComponent('%41')==='A'&&decodeURI('%20')===' '&&decodeURIComponent('%3F')==='?'");
    boolean("decodeURIComponent('%00').length===1&&decodeURIComponent('%00').charCodeAt(0)===0");
    boolean("decodeURI('%F0%9F%98%80')==='😀'&&decodeURIComponent('%F0%9F%98%80')==='😀'");
    boolean("let caught=false;try{decodeURIComponent('%')}catch(e){caught=e instanceof URIError}caught");
}

TEST_F(JsMvpLmd, DynamicSynchronousTemplates) {
    boolean(R"JS(
function check(v){if(!v)throw new Error('dynamic template');}
let render=Function('value','return `left:${value}:right`');
check(render(7)==='left:7:right');
let hints=[];
let object={[Symbol.toPrimitive](hint){hints.push(hint);return 'ok'}};
check(render(object)==='left:ok:right'&&hints.join(',')==='string');
let marker={};let caught=false;
try{render({toString(){throw marker}})}catch(e){caught=e===marker}
check(caught);
caught=false;try{render(Symbol('x'))}catch(e){caught=e instanceof TypeError}
check(caught);
check(Function('return `line1\nline2`')()==='line1\nline2');
check(Function('v','return `${v}${v+1}`')(2)==='23');
true;
    )JS");
}

TEST_F(JsMvpLmd, DynamicSynchronousSpread) {
    boolean(R"JS(
function check(v){if(!v)throw new Error('dynamic spread');}
let array=Function('source','return [0,...source,3]');
let a=array([1,2]);check(a.length===4&&a.join(',')==='0,1,2,3');
let holes=Function('return [...new Array(2)]')();check(holes.length===2&&0 in holes&&1 in holes);
let chars=Function('return [..."A\u{1f600}B"]')();check(chars.length===3&&chars[1]==='😀');
let trace='';let source={
 [Symbol.iterator](){trace+='i';let n=0;return {
  get next(){trace+='n';return function(){trace+='s';return n<2?{done:false,value:++n}:{done:true}}},
  return(){trace+='r';return {}}
 }}
};
a=array(source);check(a.join(',')==='0,1,2,3'&&trace==='insss');
trace='';
let call=Function('fn','source','return fn(0,...source,3)');
check(call(function(){return Array.prototype.join.call(arguments,',')},source)==='0,1,2,3'&&trace==='insss');
function C(a,b){this.total=a+b;}
let create=Function('C','source','return new C(...source)');
let instance=create(C,[4,5]);check(instance instanceof C&&instance.total===9);
let bad=false;try{array(null)}catch(e){bad=e instanceof TypeError}check(bad);
true;
    )JS");
}

TEST_F(JsMvpLmd, DynamicSynchronousDestructuring) {
    boolean(R"JS(
function check(v){if(!v)throw new Error('dynamic destructuring');}
let unpack=Function('[a,b=2,...rest]','{x,...other}','return [a,b,rest,other]');
let data=unpack([1,undefined,3,4],{x:1,y:5});
check(data[0]===1&&data[1]===2&&data[2].join(',')==='3,4'&&data[3].y===5&&!('x' in data[3]));
let rest=Function('a','...tail','a=7; return [tail,arguments[0]]')(1,5e-324,3);
check(rest[0][0]===5e-324&&rest[0][1]===3&&rest[1]===1);
let assign=Function('source','let a,b; [a,...b]=source; return [a,b]');
data=assign([1,2,3]);check(data[0]===1&&data[1].join(',')==='2,3');
let trace='',iterator;
let source={ [Symbol.iterator](){trace+='i';let n=0;iterator={
 get next(){trace+='n';return function(){trace+='s';Object.defineProperty(iterator,'next',{value:function(){throw 91},configurable:true});return n<3?{value:++n,done:false}:{done:true}}},
 return(){trace+='r';return {}}
 };return iterator}};
data=assign(source);check(data[0]===1&&data[1].join(',')==='2,3'&&trace==='inssss');
let reads=0,closed=0;
let holeSource={ [Symbol.iterator](){return {next(){return {done:false,get value(){reads++;throw 92}}},return(){closed++;return {}}}}};
check(Function('source','let [,]=source; return true')(holeSource));
check(reads===0&&closed===1);
let tdz=false;try{Function('[a=b,b]','return a')([undefined,2])}catch(e){tdz=e instanceof ReferenceError}check(tdz);
true;
    )JS");
}

TEST_F(JsMvpLmd, DynamicSynchronousObjectCopy) {
    boolean(R"JS(
function check(v){if(!v)throw new Error('dynamic object copy');}
let symbol=Symbol('kept'),omit=Symbol('omit'),reads=0;
let source={get x(){reads++;return 1},y:2,[symbol]:3,[omit]:4};
let spread=Function('source','return {...source}');
let copied=spread(source);check(reads===1&&copied.x===1&&copied.y===2&&copied[symbol]===3&&copied[omit]===4);
let cut=Function('source','omit','let {x,[omit]:unused,...rest}=source; return rest');
let rest=cut(source,omit);check(reads===2&&!('x' in rest)&&!(omit in rest)&&rest.y===2&&rest[symbol]===3);
check(Function('return {...null,...undefined,..."ab"}')()[1]==='b');
let promise=Promise.resolve(1);promise.y=2;promise[symbol]=3;promise[omit]=4;
Object.defineProperty(promise,'x',{enumerable:true,get(){reads++;return 7}});
copied=spread(promise);check(reads===3&&copied.x===7&&copied.y===2&&copied[symbol]===3);
rest=cut(promise,omit);check(reads===4&&!('x' in rest)&&!(omit in rest)&&rest.y===2&&rest[symbol]===3);
true;
    )JS");
}

TEST_F(JsMvpLmd, DynamicSynchronousForOf) {
    boolean(R"JS(
function check(v){if(!v)throw new Error('dynamic for-of');}
let collect=Function('source','let out=[]; for(let x of source){out.push(()=>x)} return out');
let out=collect([1,2,3]);check(out[0]()===1&&out[1]()===2&&out[2]()===3);
check(Function('source','let total=0;for(let [a,b] of source){total+=a+b}return total')([[1,2],[3,4]])===10);
let closed=0,steps=0;
let source={[Symbol.iterator](){return {next(){steps++;return {value:steps,done:false}},return(){closed++;return {}}}}};
check(Function('source','for(let x of source){break}return true')(source));check(closed===1&&steps===1);
check(Function('source','for(let x of source){return x}')(source)===2);check(closed===2);
let original={},replacement={},seen;
let abrupt={[Symbol.iterator](){return {next(){return {value:1,done:false}},return(){closed++;throw replacement}}}};
try{Function('source','marker','for(let x of source){throw marker}')(abrupt,original)}catch(e){seen=e}
check(seen===original&&closed===3);
true;
    )JS");
}

TEST_F(JsMvpLmd, DynamicSynchronousIteratorEffects) {
    boolean(R"JS(
function check(v){if(!v)throw new Error('dynamic iterator effect order');}
let trace='',source={ [Symbol.iterator](){trace+='i';return {
 get next(){trace+='n';return function(){trace+='s';return {get done(){trace+='d';return false},get value(){trace+='v';return 5}}}},
 return(){trace+='r';return {}}
}}};
let target={set key(v){trace+='w';this.seen=v}};
check(Function('source','target','key','[target[key()]]=source; return target.seen')(source,target,function(){trace+='k';return 'key'})===5);
check(trace==='inksdvwr');
let nextGets=0,emptyCloses=0;
let emptySource={[Symbol.iterator](){return {get next(){nextGets++;return 0},return(){emptyCloses++;return {}}}}};
check(Function('source','let []=source; return true')(emptySource));
check(nextGets===1&&emptyCloses===1);
let closes=0,marker={};
let failNext={[Symbol.iterator](){return {next(){throw marker},return(){closes++;return {}}}}};
let caught;try{Function('source','let [x]=source;return x')(failNext)}catch(e){caught=e}
check(caught===marker&&closes===0);
let failDone={[Symbol.iterator](){return {next(){return {get done(){throw marker}}},return(){closes++;return {}}}}};
caught=undefined;try{Function('source','let [x]=source;return x')(failDone)}catch(e){caught=e}
check(caught===marker&&closes===0);
let failValue={[Symbol.iterator](){return {next(){return {done:false,get value(){throw marker}}},return(){closes++;return {}}}}};
caught=undefined;try{Function('source','let [x]=source;return x')(failValue)}catch(e){caught=e}
check(caught===marker&&closes===0);
let failTarget={set key(v){throw marker}};
caught=undefined;try{Function('source','target','[target.key]=source')(source,failTarget)}catch(e){caught=e}
check(caught===marker&&trace.slice(-1)==='r');
true;
    )JS");
}

TEST_F(JsMvpLmd, DynamicInterpretedUnaryScalarOwnership) {
    boolean(R"JS(
// unary + must not return a pointer into dynamic_unary's dead local home.
let unary = Function('x', 'return +x');
if (unary(5e-324) !== 5e-324) throw new Error('dynamic unary local scalar home');
true;
    )JS");
}

TEST_F(JsMvpLmd, DynamicInterpretedToNumericScalarOwnership) {
    boolean(R"JS(
// postfix update preserves the pre-update ToNumeric result across a store.
let increment = Function('x', 'let previous = x++; return previous');
if (increment(5e-324) !== 5e-324) throw new Error('dynamic ToNumeric local scalar home');
true;
    )JS");
}

TEST_F(JsMvpLmd, DynamicInterpretedGlobalScalarReentry) {
    boolean(R"JS(
// the first operand must keep its value when the second overwrites its owner.
let dynamicWideGlobal = 5e-324;
let readThenMutate = Function('mutate', 'return dynamicWideGlobal + mutate()');
let result = readThenMutate(function () { dynamicWideGlobal = 1e-323; return 0; });
if (result !== 5e-324 || dynamicWideGlobal !== 1e-323)
    throw new Error('dynamic global lexical scalar borrowed across reentry');
true;
    )JS");
}

TEST_F(JsMvpLmd, AsyncGeneratorPrototypeFamily) {
    boolean(R"JS(
        async function* g(a){} async function* h(){}
        let f=Object.getPrototypeOf(g),p=f.prototype,i=Object.getPrototypeOf(p),c=f.constructor;
        let d=Object.getOwnPropertyDescriptor(g,'prototype'),m=Object.getOwnPropertyDescriptor(p,'next');
        Object.getPrototypeOf(h)===f&&Object.getPrototypeOf(f)===Function.prototype&&
        Object.getPrototypeOf(c)===Function&&c.name==='AsyncGeneratorFunction'&&c.length===1&&
        c.prototype===f&&p.constructor===f&&Object.getPrototypeOf(g.prototype)===p&&
        !Object.hasOwn(g.prototype,'constructor')&&Object.getPrototypeOf(g())===g.prototype&&
        i[Symbol.asyncIterator].call(7)===7&&d.writable&&!d.enumerable&&!d.configurable&&
        m.value.length===1&&m.value.name==='next'&&m.writable&&!m.enumerable&&m.configurable&&
        Object.prototype.toString.call(g())==='[object AsyncGenerator]'
    )JS");
    boolean("async function* g(){}let prototype={};g.prototype=prototype;let i=g();let caught=false;try{new g()}catch(e){caught=e instanceof TypeError}Object.getPrototypeOf(i)===prototype&&caught");
    boolean("async function* g(){}let expected=Object.getPrototypeOf(g).prototype;g.prototype=7;Object.getPrototypeOf(g())===expected");
    boolean("let C=Object.getPrototypeOf(async function*(){}).constructor;class Derived extends C{}Object.getPrototypeOf(Derived)===C&&Object.getPrototypeOf(Derived.prototype)===C.prototype");
}
TEST_F(JsMvpLmd, AsyncGeneratorSerialRequestsAndReentry) {
    expect_async_flags(run(R"JS(
        let out=[],steps=[];
        async function* g(){steps.push(1);let n=yield 2;steps.push(n);yield 3;return 4}
        let i=g(),a=i.next(99),b=i.next(7),c=i.next();
        Promise.all([a,b,c]).then(r=>out.push(r[0].value===2&&!r[0].done&&r[1].value===3&&!r[1].done&&r[2].value===4&&r[2].done&&steps.join(',')==='1,7'));
        out
    )JS"),1);
    expect_async_flags(run(R"JS(
        let out=[],nested,i;
        async function* g(){nested=i.next();yield 1;yield 2}
        i=g();i.next().then(r=>out.push(r.value===1&&!r.done));
        nested.then(r=>out.push(r.value===2&&!r.done));out
    )JS"),2);
}
TEST_F(JsMvpLmd, AsyncGeneratorAwaitAndCompletionOwnership) {
    expect_async_flags(run(R"JS(
        let out=[],release,wait=new Promise(r=>release=r);
        async function* g(){'use strict';await wait;let garbage=[];for(let i=0;i<2000;i++)garbage.push({i});yield this;return this}
        let a=g.call(5e-324),b=g.call(-0);
        Promise.all([a.next(),a.next(),b.next(),b.next()]).then(r=>out.push(r[0].value===5e-324&&r[1].value===5e-324&&r[1].done&&1/r[2].value===-Infinity&&1/r[3].value===-Infinity&&r[3].done));
        release();out
    )JS"),1);
    expect_async_flags(run(R"JS(
        let out=[],ran=0;
        async function* g(){ran++;yield 1}
        let a=g(),b=g();
        Promise.all([a.return(Promise.resolve(5)),b.throw(7).catch(e=>e),a.next(),b.next()]).then(r=>out.push(ran===0&&r[0].value===5&&r[0].done&&r[1]===7&&r[2].done&&r[3].done));out
    )JS"),1);
}
TEST_F(JsMvpLmd, AsyncGeneratorReturnAwaitsBeforeFinally) {
    expect_async_flags(run(R"JS(
        let out=[],log=[];
        async function* g(){try{return {then(resolve){log.push('then');resolve(8)}}}finally{log.push('finally')}}
        g().next().then(r=>out.push(r.value===8&&r.done&&log.join(',')==='then,finally'));
        async function* h(){try{return Promise.reject(7)}catch(e){yield e}}
        h().next().then(r=>out.push(r.value===7&&!r.done));out
    )JS"),2);
    expect_async_flags(run(R"JS(
        let out=[],order=[];
        Promise.resolve().then(()=>order.push('t1')).then(()=>order.push('t2')).then(()=>out.push(order.join(',')==='t1,a,b,t2,c'));
        async function* a(){} async function* b(){return} async function* c(){return undefined}
        a().next().then(()=>order.push('a'));b().next().then(()=>order.push('b'));c().next().then(()=>order.push('c'));out
    )JS"),1);
    expect_async_flags(run(R"JS(
        let out=[];
        async function* g(){try{yield 1}finally{yield 2}}
        let i=g();Promise.all([i.next(),i.return(5),i.next()]).then(r=>out.push(r[0].value===1&&r[1].value===2&&!r[1].done&&r[2].value===5&&r[2].done));out
    )JS"),1);
}
TEST_F(JsMvpLmd, AsyncGeneratorMethodsRejectIncompatibleReceivers) {
    expect_async_flags(run(R"JS(
        let out=[];async function* g(){}function* s(){}
        let p=Object.getPrototypeOf(g).prototype;
        Promise.all([p.next.call({}),p.return.call(s()),p.throw.call(g)].map(p=>p.then(()=>false,e=>e instanceof TypeError))).then(r=>out.push(r[0]&&r[1]&&r[2]));
        let caught=false;try{s().next.call(g())}catch(e){caught=e instanceof TypeError}out.push(caught);out
    )JS"),2);
}
TEST_F(JsMvpLmd, AsyncGeneratorDelegationAndForwardedRequests) {
    expect_async_flags(run(R"JS(
        let out=[],calls=[];let source={
            [Symbol.asyncIterator](){return this},next(v){calls.push(v);return {value:1,done:false}},
            return(v){calls.push(v);return Promise.resolve({value:v+1,done:true})}
        };
        async function* g(){yield* source}
        let i=g();Promise.all([i.next(99),i.return(Promise.resolve(5))]).then(r=>out.push(r[0].value===1&&!r[0].done&&r[1].value===6&&r[1].done&&calls.length===2&&calls[0]===undefined&&calls[1]===5));out
    )JS"),1);
    expect_async_flags(run(R"JS(
        let out=[],reads=0;let source={[Symbol.asyncIterator](){return this},next(){return {value:1,done:false}}};
        async function* g(){yield* source}
        let i=g();i.next();i.return({get then(){reads++}}).then(r=>out.push(r.done&&reads===2));out
    )JS"),1);
    expect_async_flags(run(R"JS(
        let out=[],p=Promise.resolve(7),source={[Symbol.asyncIterator](){return this},next(){return {value:p,done:false}}};
        async function* g(){yield* source}
        g().next().then(r=>out.push(r.value===p&&!r.done));out
    )JS"),1);
}
TEST_F(JsMvpLmd, AsyncFromSyncDelegationAndRejectionClose) {
    expect_async_flags(run(R"JS(
        let out=[];async function* g(){yield* [Promise.resolve(3),4]}
        let i=g();Promise.all([i.next(),i.next(),i.next()]).then(r=>out.push(r[0].value===3&&r[1].value===4&&r[2].done));out
    )JS"),1);
    expect_async_flags(run(R"JS(
        let out=[],closed=0,source={[Symbol.iterator](){return this},next(){return {value:Promise.reject(9),done:false}},return(){closed++;throw 2}};
        async function* g(){yield* source}
        let i=g();i.next().then(()=>out.push(false),e=>out.push(e===9&&closed===1));i.next().then(r=>out.push(r.done));out
    )JS"),2);
    expect_async_flags(run(R"JS(
        let out=[],closed=0,source={[Symbol.iterator](){return this},next(){return {value:1,done:false}},throw(){return {value:Promise.reject(9),done:false}},return(){closed++;return {done:true}}};
        async function* g(){yield* source}
        let i=g();i.next();i.throw(7).then(()=>out.push(false),e=>out.push(e===9&&closed===1));out
    )JS"),1);
}
TEST_F(JsMvpLmd, ForAwaitUsesCachedNextAndOriginalReceiver) {
    expect_async_flags(run(R"JS(
        let out=[],reads=0,calls=0,valid=true,source={
            [Symbol.asyncIterator](){return this},
            get next(){reads++;return function(){valid=valid&&this===source&&arguments.length===0;calls++;return {value:calls,done:calls>2}}}
        };
        async function f(){let values=[];for await(let value of source)values.push(value);out.push(valid&&reads===1&&calls===3&&values.join(',')==='1,2')}
        f();out
    )JS"),1);
    expect_async_flags(run(R"JS(
        let out=[],p=Promise.resolve(7),calls=0,source={[Symbol.asyncIterator](){return this},next(){return {value:p,done:calls++>0}}};
        async function f(){for await(let value of source)out.push(value===p);let values=[];for await(let value of [Promise.resolve(3),4])values.push(value);out.push(values.join(',')==='3,4')}
        f();out
    )JS"),2);
}
TEST_F(JsMvpLmd, ForAwaitCloseAwaitsAndPreservesThrow) {
    expect_async_flags(run(R"JS(
        let out=[],closed=0,finished=0,source={[Symbol.asyncIterator](){return this},next(){return {value:1,done:false}},return(){closed++;return Promise.resolve().then(()=>{finished++;return {done:true}})}};
        async function f(){for await(let value of source)break;out.push(closed===1&&finished===1)}f();out
    )JS"),1);
    expect_async_flags(run(R"JS(
        let out=[],closed=0,source={[Symbol.asyncIterator](){return this},next(){return {value:1,done:false}},return(){closed++;return Promise.reject(2)}};
        async function f(){try{for await(let value of source)throw 1}catch(e){out.push(e===1&&closed===1)}}f();out
    )JS"),1);
    expect_async_flags(run(R"JS(
        let out=[],source={[Symbol.asyncIterator](){return this},next(){return {value:1,done:false}},return(){return Promise.resolve(3)}};
        async function f(){try{for await(let value of source)break}catch(e){out.push(e instanceof TypeError)}}f();out
    )JS"),1);
}
TEST_F(JsMvpLmd, ForAwaitRejectsStepWithoutClosing) {
    expect_async_flags(run(R"JS(
        let out=[],closed=0,source={[Symbol.asyncIterator](){return this},next(){return {done:false,get value(){throw 8}}},return(){closed++;return {done:true}}};
        async function f(){try{for await(let value of source){}}catch(e){out.push(e===8&&closed===0)}}f();out
    )JS"),1);
    expect_async_flags(run(R"JS(
        let out=[],closed=0,source={[Symbol.asyncIterator](){return this},next(){return Promise.resolve(7)},return(){closed++;return {done:true}}};
        async function f(){try{for await(let value of source){}}catch(e){out.push(e instanceof TypeError&&closed===0)}}f();out
    )JS"),1);
}
TEST_F(JsMvpLmd, AsyncGeneratorForAwaitAndNestedSuspension) {
    expect_async_flags(run(R"JS(
        let out=[];async function* inner(){yield 1;await 0;yield 2}
        async function* outer(){for await(let value of inner())yield value*2}
        async function f(){let values=[];for await(let value of outer())values.push(value);out.push(values.join(',')==='2,4')}
        Promise.resolve().then(()=>f());out
    )JS"),1);
}
TEST_F(JsMvpLmd, AsyncIteratorResultUsesOwnDataProperties) {
    expect_async_flags(run(R"JS(
        let out=[],calls=0,d=Object.create(null);d.set=function(){calls++};d.configurable=true;
        Object.defineProperty(Object.prototype,'value',d);Object.defineProperty(Object.prototype,'done',d);
        async function* g(){yield 7}g().next().then(r=>{delete Object.prototype.value;delete Object.prototype.done;out.push(calls===0&&r.value===7&&!r.done&&Object.hasOwn(r,'value')&&Object.hasOwn(r,'done'))});out
    )JS"),1);
}

TEST_F(JsMvpLmd, DynamicTaggedIdentity) {
    boolean(R"JS(
function check(v){if(!v)throw new Error('dynamic tagged identity');}
function tag(strings){return strings;}
let call=Function('tag','return tag`same`');
let first=call(tag);check(first===call(tag));
let other=Function('tag','return tag`same`');check(other(tag)!==first);
let sites=Function('tag','return [tag`same`,tag`same`]')(tag);check(sites[0]!==sites[1]);
let make=Function('return tag=>tag`closed`');
let one=make(),two=make();check(one(tag)===two(tag));
let evalOne=(0,eval)('(tag)=>tag`eval-site`');
let evalTwo=(0,eval)('(tag)=>tag`eval-site`');
check(evalOne(tag)===evalOne(tag)&&evalOne(tag)!==evalTwo(tag));
let original=tag`same`;check(original!==first);
true;
    )JS");
}

TEST_F(JsMvpLmd, DynamicTaggedDescriptors) {
    boolean(R"JS(
function check(v){if(!v)throw new Error('dynamic tagged descriptors');}
let capture=Function('tag','return tag`line\\n${7}end`');
let value,raw,argument;
capture(function(strings,v){value=strings;raw=strings.raw;argument=v;});
check(value.length===2&&raw.length===2&&value[0]==='line\n'&&raw[0]==='line\\n'&&argument===7);
check(Object.isFrozen(value)&&Object.isFrozen(raw));
let d=Object.getOwnPropertyDescriptor(value,'raw');
check(d.value===raw&&!d.writable&&!d.enumerable&&!d.configurable);
d=Object.getOwnPropertyDescriptor(value,'0');check(d.enumerable&&!d.writable&&!d.configurable);
d=Object.getOwnPropertyDescriptor(value,'length');check(!d.enumerable&&!d.writable&&!d.configurable);
let invalid=Function('tag','return tag`\\unicode`')(function(strings){return strings});
check(invalid[0]===undefined&&invalid.raw[0]==='\\unicode');
let failed=false;try{Function('return `\\unicode`')}catch(e){failed=e instanceof SyntaxError}check(failed);
true;
    )JS");
}

TEST_F(JsMvpLmd, DynamicTaggedOrder) {
    boolean(R"JS(
function check(v){if(!v)throw new Error('dynamic tagged effects');}
let trace=[];let owner={get tag(){trace.push('get');return function(strings,v){trace.push('call');check(this===owner);return v;}}};
let invoke=Function('owner','trace','return owner.tag`a${trace.push("sub")}b`');
check(invoke(owner,trace)===2&&trace.join(',')==='get,sub,call');
trace=[];let bad={get tag(){trace.push('get');return 0;}};
let caught=false;try{invoke(bad,trace)}catch(e){caught=e instanceof TypeError}
check(caught&&trace.join(',')==='get,sub');
let marker={};trace=[];let abrupt={get tag(){trace.push('get');throw marker}};
caught=false;try{invoke(abrupt,trace)}catch(e){caught=e===marker}check(caught&&trace.join(',')==='get');
let value={toString(){throw marker}};
check(Function('tag','value','return tag`${value}`')(function(strings,v){return v},value)===value);
let symbol=Symbol('x');check(Function('tag','v','return tag`${v}`')(function(s,v){return v},symbol)===symbol);
true;
    )JS");
}

TEST_F(JsMvpLmd, DynamicTaggedReentry) {
    boolean(R"JS(
function check(v){if(!v)throw new Error('dynamic tagged roots');}
let capture=Function('tag','allocate','return tag`a${allocate()}b`');
let remembered;
function tag(strings,value){if(remembered)check(strings===remembered);remembered=strings;return value;}
function allocate(){let garbage=[];for(let i=0;i<2000;i++)garbage.push({value:i});return 5e-324;}
check(capture(tag,allocate)===5e-324);check(capture(tag,allocate)===5e-324);
let depth=0,outer;
function recursive(strings){if(depth===0){outer=strings;depth++;check(capture(recursive,allocate)===strings);depth--;}else check(outer===strings);return strings;}
check(capture(recursive,allocate)===remembered);
let nested=Function('tag','return tag`first``second`');
let seen=[];
function next(strings){seen.push(strings);return next;}
check(nested(next)===next&&seen.length===2&&seen[0]!==seen[1]);
true;
    )JS");
}

TEST_F(JsMvpLmd, DynamicNativeMetadataResolvers) {
    boolean(R"JS(
function check(v){if(!v)throw new Error('native resolving wrapper metadata');}
let resolve,reject;
let promise=new Promise(function(a,b){resolve=a;reject=b;});
check(resolve.length===1&&reject.length===1&&resolve.name===''&&reject.name==='');
check(resolve!==reject&&Function('f','return f')(resolve)===resolve);
let d=Object.getOwnPropertyDescriptor(resolve,'length');
check(d.value===1&&!d.writable&&!d.enumerable&&d.configurable);
d=Object.getOwnPropertyDescriptor(reject,'length');
check(d.value===1&&!d.writable&&!d.enumerable&&d.configurable);
resolve(1);reject(2);check(resolve.length===1&&reject.length===1);
true;
    )JS");
}

TEST_F(JsMvpLmd, DynamicNativeMetadataCapabilities) {
    boolean(R"JS(
function check(v){if(!v)throw new Error('native capability executor metadata');}
let executors=[];
function C(executor){executors.push(executor);executor(function(){},function(){});}
Promise.resolve.call(C,1);
Promise.reject.call(C,2);
Promise.all.call(C,[]);
Promise.race.call(C,[]);
check(executors.length===4);
for(let i=0;i<executors.length;i++){
 let f=executors[i];check(f.length===2&&f.name==='');
 let d=Object.getOwnPropertyDescriptor(f,'length');
 check(d.value===2&&!d.writable&&!d.enumerable&&d.configurable);
 if(i)check(f!==executors[i-1]);
}
let f=Function('a','b=1','return a+b');check(f.length===1&&f(2)===3);
true;
    )JS");
}

TEST_F(JsMvpLmd, HostTimersJobOrder) {
    MvpLmdHost host = {0, NULL, NULL};
    expect_async_flags(run(R"JS(
        let out=[],log=['sync'];
        Promise.resolve().then(()=>{log.push('promise');setTimeout(()=>{
            log.push('late');out.push(log.join(',')==='sync,promise,micro,timer,task,late');
        },0)});
        setTimeout(()=>{log.push('timer');queueMicrotask(()=>log.push('task'))},0);
        queueMicrotask(()=>log.push('micro'));out
    )JS", &host), 1);
}
TEST_F(JsMvpLmd, HostTimersClearAndReentry) {
    MvpLmdHost host = {0, NULL, NULL};
    expect_async_flags(run(R"JS(
        let out=[],count=0,cancelled=setTimeout(()=>out.push(false),0);
        clearInterval(cancelled);clearTimeout(cancelled);
        let interval=setInterval(()=>{count++;if(count===2){clearTimeout(interval);
            setTimeout(()=>out.push(count===2&&typeof interval==='number'),0)}},0);
        out
    )JS", &host), 1);
}
TEST_F(JsMvpLmd, HostTimerCallbackArgumentsAndDelayCoercion) {
    MvpLmdHost host = {0, NULL, NULL};
    expect_async_flags(run(R"JS(
        let out=[],converted=0,receiver=globalThis;
        setTimeout(function(a,b){'use strict';out.push(this===receiver&&arguments.length===2&&
            a===5e-324&&1/b===-Infinity&&converted===1)},
            {valueOf(){converted++;for(let i=0;i<300;i++){let n=i+0.25;let a=[n,n+0.5]}return '0'}},5e-324,-0);
        queueMicrotask(function(){'use strict';out.push(this===undefined&&arguments.length===0)},99);
        out
    )JS", &host), 2);
}
TEST_F(JsMvpLmd, HostTimersValidation) {
    MvpLmdHost host = {0, NULL, NULL};
    Item result=run(R"JS(
        let count=0,converted=0,delay={valueOf(){converted++;return 0}};
        try{setTimeout(1,delay)}catch(e){if(e instanceof TypeError)count++}
        try{setTimeout(()=>{},Symbol())}catch(e){if(e instanceof TypeError)count++}
        try{setInterval(()=>{},1n)}catch(e){if(e instanceof TypeError)count++}
        try{queueMicrotask({})}catch(e){if(e instanceof TypeError)count++}
        clearTimeout(NaN);clearInterval(Infinity);clearTimeout(-1);
        count===4&&converted===0&&setTimeout.length===2&&clearTimeout.length===1&&queueMicrotask.length===1
    )JS", &host);
    EXPECT_EQ(result.item,ITEM_TRUE);
}
TEST_F(JsMvpLmd, HostTasksRetainFirstThrownPayload) {
    MvpLmdHost host = {0, NULL, NULL};
    const char* sources[]={
        "queueMicrotask(()=>{throw {value:5e-324}});queueMicrotask(()=>{for(let i=0;i<200;i++){let a=[i+0.25]}throw 9});0",
        "setTimeout(()=>{throw {value:5e-324}},0);setTimeout(()=>{for(let i=0;i<200;i++){let a=[i+0.25]}throw 9},0);0",
        "setInterval(()=>{throw {value:5e-324}},0);0"
    };
    for(const char* source:sources){
        mvp_lmd_destroy(execution);execution=mvp_lmd_execute(source,strlen(source),NULL,&host);
        ASSERT_NE(mvp_lmd_diagnostic(execution),nullptr)<<source;
        Item result=mvp_lmd_result(execution);ASSERT_EQ(get_type_id(result),LMD_TYPE_ERROR);
        Item thrown=mvp_lmd_caught_value(mvp_lmd_program_library(),result);
        ASSERT_EQ(get_type_id(thrown),LMD_TYPE_MAP);
        Item value=mvp_lmd_property_get(thrown,Item{.item=s2it(heap_create_name("value",5))},0);
        ASSERT_EQ(get_type_id(value),LMD_TYPE_FLOAT);EXPECT_EQ(it2d(value),nextafter(0.0,1.0));
    }
}
TEST_F(JsMvpLmd, HostAsyncCompletionDrainsRealTimers) {
    MvpLmdHost host = {0, NULL, NULL, true};
    expect_async_flags(run(R"JS(
        let out=[];
        globalThis.__lambda_test262_async_required=true;
        globalThis.__lambda_test262_async_done=false;
        globalThis.$DONE=function(error){if(globalThis.__lambda_test262_async_done)throw 1;
            globalThis.__lambda_test262_async_done=true;if(error)throw error};
        new Promise(resolve=>setTimeout(resolve,0,7)).then(value=>{out.push(value===7);$DONE()});
        if(typeof setTimeout!=='function'&&!globalThis.__lambda_test262_async_done)throw 2;
        out
    )JS", &host),1);
}
TEST_F(JsMvpLmd, HostTimersCleanupAfterAbruptEntry) {
    MvpLmdHost host = {0, NULL, NULL};
    const char* source="setTimeout(()=>{throw 'stale timer'},100000);throw 'entry error'";
    mvp_lmd_destroy(execution);execution=mvp_lmd_execute(source,strlen(source),NULL,&host);
    ASSERT_NE(mvp_lmd_diagnostic(execution),nullptr);
    expect_async_flags(run("let out=[];setTimeout(()=>out.push(true),0);out",&host),1);
}

TEST_F(JsMvpLmd, RegExpTestUsesObservableExec) {
    boolean("let r=/a/,seen='',token={},answer={};r.exec=function(s){seen+=s;return answer};r.test({toString(){seen+='s';return 'x'}})&&seen==='sx'");
    boolean("let seen='',r={get exec(){seen+='e';return function(s){seen+=s;return null}}};!RegExp.prototype.test.call(r,{toString(){seen+='s';return 'x'}})&&seen==='sex'");
    boolean("let marker={},r={get exec(){throw marker}},caught=false;try{RegExp.prototype.test.call(r,'x')}catch(e){caught=e===marker}caught");
    boolean("let p=Promise.resolve(1);RegExp.prototype.test.call({exec(){return p}},'x')");
    error("RegExp.prototype.test.call({exec(){return 1}},'x')", "TypeError");
    error("RegExp.prototype.test.call({},'x')", "TypeError");
    boolean("let seen=0,caught=false;try{RegExp.prototype.test.call(null,{toString(){seen++;return ''}})}catch(e){caught=e instanceof TypeError}caught&&seen===0");
}

TEST_F(JsMvpLmd, RegExpSearchPreservesIndexIdentityAndLastIndex) {
    boolean("let marker={},r={lastIndex:1.25,exec(s){this.lastIndex=99.5;return {index:marker}}};RegExp.prototype[Symbol.search].call(r,'x')===marker&&r.lastIndex===1.25");
    boolean("let r={lastIndex:-0,exec(){this.lastIndex=4;return {index:'found'}}};RegExp.prototype[Symbol.search].call(r,'x')==='found'&&1/r.lastIndex===-Infinity");
    boolean("let marker={},state=4,seen='',r={get lastIndex(){seen+='g';return state},set lastIndex(v){seen+='s'+v;state=v},exec(s){seen+='e'+s;state=2;return {get index(){seen+='i';return marker}}}};RegExp.prototype[Symbol.search].call(r,'x')===marker&&state===4&&seen==='gs0exgs4i'");
    boolean("let marker={},r={lastIndex:7,exec(){this.lastIndex=3;throw marker}},caught=false;try{RegExp.prototype[Symbol.search].call(r,'x')}catch(e){caught=e===marker}caught&&r.lastIndex===3");
    boolean("let r=/a/g;r.lastIndex=4;'ba'.search(r)===1&&r.lastIndex===4&&'bc'.search(r)===-1&&r.lastIndex===4");
}

TEST_F(JsMvpLmd, RegExpGenericFlagsAndToStringOrder) {
    boolean("let get=Object.getOwnPropertyDescriptor(RegExp.prototype,'flags').get,seen='',o={};for(let name of ['hasIndices','global','ignoreCase','multiline','dotAll','unicode','unicodeSets','sticky'])Object.defineProperty(o,name,{get(){seen+=name+',';return true}});get.call(o)==='dgimsuvy'&&seen==='hasIndices,global,ignoreCase,multiline,dotAll,unicode,unicodeSets,sticky,'");
    boolean("let get=Object.getOwnPropertyDescriptor(RegExp.prototype,'flags').get;function f(){}f.global=1;get.call(f)==='g'&&get.call([])===''");
    boolean("let seen='',o={get source(){seen+='s';return {toString(){seen+='S';return 'a'}}},get flags(){seen+='f';return {toString(){seen+='F';return 'g'}}}};RegExp.prototype.toString.call(o)==='/a/g'&&seen==='sSfF'");
    boolean("let marker={},seen='',o={get source(){return {toString(){throw marker}}},get flags(){seen+='f';return ''}},caught=false;try{RegExp.prototype.toString.call(o)}catch(e){caught=e===marker}caught&&seen===''");
    error("Object.getOwnPropertyDescriptor(RegExp.prototype,'flags').get.call(1)", "TypeError");
    error("RegExp.prototype.toString.call('x')", "TypeError");
}

TEST_F(JsMvpLmd, RegExpInternalSlotsAndDescriptorMetadata) {
    boolean("let r=/a/gim,keys=Object.getOwnPropertyNames(r),d=Object.getOwnPropertyDescriptor(r,'lastIndex');keys.length===1&&keys[0]==='lastIndex'&&d.value===0&&d.writable&&!d.enumerable&&!d.configurable&&Object.keys(r).length===0");
    boolean("let r=/a/gi,get=Object.getOwnPropertyDescriptor(RegExp.prototype,'source').get;Object.defineProperty(r,'source',{value:'fake'});get.call(r)==='a'&&r.source==='fake'&&RegExp.prototype.source==='(?:)'&&RegExp.prototype.global===undefined&&RegExp.prototype.flags===''");
    boolean("let r=/a/g,get=Object.getOwnPropertyDescriptor(RegExp.prototype,'global').get;Object.defineProperty(r,'global',{value:false});get.call(r)===true&&r.flags===''");
    boolean("let get=Object.getOwnPropertyDescriptor(RegExp.prototype,'global').get,caught=false;try{get.call(Object.create(RegExp.prototype))}catch(e){caught=e instanceof TypeError}caught");
    boolean("let m=RegExp.prototype[Symbol.search],d=Object.getOwnPropertyDescriptor(RegExp.prototype,Symbol.search),n=Object.getOwnPropertyDescriptor(m,'name'),l=Object.getOwnPropertyDescriptor(m,'length'),g=Object.getOwnPropertyDescriptor(RegExp.prototype,'flags');d.writable&&!d.enumerable&&d.configurable&&n.value==='[Symbol.search]'&&!n.writable&&!n.enumerable&&n.configurable&&l.value===1&&!l.writable&&!l.enumerable&&l.configurable&&g.get.name==='get flags'&&g.get.length===0&&g.set===undefined&&!g.enumerable&&g.configurable");
    error("new RegExp.prototype[Symbol.match]('x')", "TypeError");
}

TEST_F(JsMvpLmd, RegExpStrictLastIndexWrites) {
    boolean("let count=0;for(let method of ['exec','test']){let r=/a/g;Object.defineProperty(r,'lastIndex',{writable:false});try{r[method]('a')}catch(e){if(e instanceof TypeError)count++}}count===2");
    boolean("let r=/a/g;Object.defineProperty(r,'lastIndex',{value:5,writable:false});let caught=false;try{r.exec('b')}catch(e){caught=e instanceof TypeError}caught&&r.lastIndex===5");
    boolean("let r=/a/;Object.defineProperty(r,'lastIndex',{value:8,writable:false});r.test('a')&&r.lastIndex===8");
    boolean("let r={lastIndex:1,exec(){return null}};Object.defineProperty(r,'lastIndex',{writable:false});let caught=false;try{RegExp.prototype[Symbol.search].call(r,'a')}catch(e){caught=e instanceof TypeError}caught");
    boolean("let r=/a/g;r.lastIndex={valueOf(){return Infinity}};r.exec('a')===null&&r.lastIndex===0");
}

TEST_F(JsMvpLmd, RegExpMatchCustomExecAndEmptyUnicodeProgress) {
    boolean("let calls=0,r=/a/g;r.exec=function(){return ++calls===1?{0:'custom'}:null};let out='a'.match(r);out.length===1&&out[0]==='custom'&&calls===2");
    boolean("let seen='',calls=0,r={get flags(){seen+='f';return 'g'},get global(){seen+='g';return true},get unicode(){seen+='u';return false},lastIndex:4,exec(s){seen+='e';return ++calls===1?{0:{toString(){seen+='s';return 'x'}}}:null}};let out=RegExp.prototype[Symbol.match].call(r,'x');out[0]==='x'&&seen==='fguese'&&r.lastIndex===0");
    boolean("let calls=0,indices=[],r={flags:'gu',global:true,unicode:true,lastIndex:0,exec(){indices.push(this.lastIndex);return calls++<2?{0:''}:null}};let out=RegExp.prototype[Symbol.match].call(r,'😀');out.length===2&&indices.join(',')==='0,2,3'");
    boolean("let marker={},r={get flags(){throw marker},get global(){throw 9}},caught=false;try{RegExp.prototype[Symbol.match].call(r,'x')}catch(e){caught=e===marker}caught");
}

TEST_F(JsMvpLmd, RegExpMutationInvalidatesOrdinaryProof) {
    boolean("let r=/a/g,calls=0;RegExp.prototype.exec=function(){return ++calls===1?{0:'changed'}:null};let out='a'.match(r);out[0]==='changed'&&calls===2");
    boolean("let r=/a/g,marker={},seen=0;Object.defineProperty(RegExp.prototype,'flags',{get(){seen++;throw marker}});let caught=false;try{'a'.match(r)}catch(e){caught=e===marker}caught&&seen===1");
    boolean("let r=/a/g,marker={};Object.defineProperty(RegExp.prototype,'global',{get(){throw marker}});let caught=false;try{RegExp.prototype[Symbol.match].call(r,'a')}catch(e){caught=e===marker}caught");
    boolean("let r=/a/g,marker={};RegExp.prototype[Symbol.match]=function(s){return marker};'a'.match(r)===marker");
    boolean("let r=/a/g,calls=0;Object.defineProperty(r,'global',{value:false});r.exec=function(){calls++;return {0:'x'}};let out='a'.match(r);out[0]==='x'&&calls===1");
}

TEST_F(JsMvpLmd, StringMatchSearchProtocolBeforeReceiverConversion) {
    boolean("let marker={},seen='',receiver={toString(){seen+='s';throw 9}},pattern={get [Symbol.match](){seen+='m';return function(x){return this===pattern&&x===receiver?marker:null}}};String.prototype.match.call(receiver,pattern)===marker&&seen==='m'");
    boolean("let marker={},receiver={toString(){throw 9}},pattern={[Symbol.search](x){return this===pattern&&x===receiver?marker:null}};String.prototype.search.call(receiver,pattern)===marker");
    boolean("let seen='',pattern={get [Symbol.search](){seen+='m';return null},toString(){seen+='p';return 'a'}},receiver={toString(){seen+='s';return 'ba'}};String.prototype.search.call(receiver,pattern)===1&&seen==='msp'");
    boolean("let seen=0,pattern={[Symbol.match]:1},receiver={toString(){seen++;return 'x'}},caught=false;try{String.prototype.match.call(receiver,pattern)}catch(e){caught=e instanceof TypeError}caught&&seen===0");
    boolean("let r=/a/g,receiver={toString(){r.exec=function(){return null};return 'a'}};String.prototype.match.call(receiver,r)===null");
    boolean("'abc'.search()===0&&'abc'.match()[0]===''&&'abc'.search('b')===1");
}

TEST_F(JsMvpLmd, RegExpCaptureRecordsOwnDataAndNullGroups) {
    boolean("let calls=0;Object.defineProperty(Object.prototype,'index',{set(){calls++},configurable:true});Object.defineProperty(Object.prototype,'input',{set(){calls++},configurable:true});Object.defineProperty(Object.prototype,'groups',{set(){calls++},configurable:true});let m=/(?<x>a)(b)?/.exec('za');m.index===1&&m.input==='za'&&m[0]==='a'&&m[1]==='a'&&m[2]===undefined&&m.groups.x==='a'&&Object.getPrototypeOf(m.groups)===null&&calls===0");
    boolean("let m=/(?<__proto__>a)/.exec('a');Object.getPrototypeOf(m.groups)===null&&Object.prototype.hasOwnProperty.call(m.groups,'__proto__')&&m.groups.__proto__==='a'");
}


TEST_F(JsMvpLmd, DynamicSpreadStableArgumentStorage) {
    boolean(R"JS(
let call=Function('fn','source','return fn(0,...source,3)');
let answer=call(function(){return [arguments[0],arguments[1],arguments[2],arguments[3],arguments.length]},[1,2]);
if(answer.join(',')!=='0,1,2,3,4')throw new Error('spread arguments storage');
let wide=call(function(){return [arguments[1],arguments[2]]},[5e-324,9007199254740991]);
if(wide[0]!==5e-324||wide[1]!==9007199254740991)throw new Error('spread scalar storage');
true;
    )JS");
}

TEST_F(JsMvpLmd, DynamicTaggedStableArgumentStorage) {
    boolean(R"JS(
let call=Function('tag','value','return tag`left${value}right`');
let answer=call(function(){return [arguments[0][0],arguments[0][1],arguments[1],arguments.length]},5e-324);
if(answer[0]!=='left'||answer[1]!=='right'||answer[2]!==5e-324||answer[3]!==2)throw new Error('tagged arguments storage');
true;
    )JS");
}



TEST_F(JsMvpLmd, DynamicParameterAndBodyEnvironments) {
    boolean(R"JS(
function check(value){if(!value)throw new Error('parameter environment');}
check(Function('a=()=>arguments','var arguments=7; return a().length===0&&arguments===7')());
check(Function('a=()=>arguments','var arguments; return a()===arguments')());
let missing=false;
try{Function('read=()=>hidden','var hidden=7; return read()')()}catch(e){missing=e instanceof ReferenceError}
check(missing);
let cells=Function('a=1','read=()=>a','write=(v)=>a=v','var a; let before=a; a=2; write(3); return [before,a,read()]')();
check(cells.join(',')==='1,2,3');
check(Function('a=1','read=()=>a','write=(v)=>a=v','a=2; write(3); return a===3&&read()===3')());
check(Function('a=1','read=()=>a','function a(){return 2} return read()===1&&a()===2')());
let retained=Function('a=5e-324','read=()=>a','var a=9; return [read,()=>a]')();
check(retained[0]()===5e-324&&retained[1]()===9);
check(Function('a','var a; a=8; return arguments[0]===8')(1));
true;
    )JS");
}

TEST_F(JsMvpLmd, DynamicComputedParameterEnvironment) {
    boolean(R"JS(
function check(value){if(!value)throw new Error('computed parameter environment');}
let state={};
let bind=Function('state','{[(state.read=()=>arguments,"x")]:a}','var arguments=7; return a===4&&state.read().length===2&&state.read()[0]===state&&arguments===7');
check(bind(state,{x:4}));
let withoutBindings=Function('{["x"]:{}}','return 17');
check(withoutBindings({x:{}})===17);
check(Function('a=0','var sum=0; for(var n of [1,2,3])sum+=n; return sum===6&&n===3')());
true;
    )JS");
}

TEST_F(JsMvpLmd, HostTimerArgumentsSurviveDefaultsAndReentry) {
    MvpLmdHost host = {0, NULL, NULL};
    expect_async_flags(run(R"JS(
        let out=[],marker={value:7};
        function allocate(){for(let i=0;i<200;i++){let a=[i+0.25]}return 1}
        setTimeout(function(a=allocate(),b,c){out.push(a===1&&b===5e-324&&c===marker)},0,
            undefined,5e-324,marker);
        setTimeout(function(a,b){
            queueMicrotask(()=>out.push(a===marker&&1/b===-Infinity));
            allocate();out.push(a===marker&&1/b===-Infinity);
        },0,marker,-0);
        out
    )JS", &host),3);
}

TEST_F(JsMvpLmd, WeakCollectionsUseSharedNativeStorage) {
    boolean("let k={},p=Promise.resolve(1),m=new WeakMap(),s=new WeakSet();m.set(k,3.25).set(p,7);s.add(k).add(p);m.get(k)===3.25&&m.get(p)===7&&m.has(k)&&s.has(p)&&m.delete(k)&&!m.delete(k)&&m.get(k)===undefined&&s.delete(p)&&!s.has(p)");
    boolean("let f=function(){},a=[],s=Symbol('s'),m=new WeakMap([[f,1],[a,2],[s,3]]),w=new WeakSet([f,a,s]);m.get(f)===1&&m.get(a)===2&&m.get(s)===3&&w.has(s)&&w.has(a)");
    boolean("let key={},m=new WeakMap([[key,9]]);m.extra=4;Object.defineProperty(m,'value',{get(){return this.extra+1}});m.value===5&&Object.keys(m).join(',')==='extra'&&Object.getOwnPropertyNames(m).length===2&&m.get(key)===9");
}

TEST_F(JsMvpLmd, WeakCollectionsRejectInvalidKeysAndReceivers) {
    boolean("let m=new WeakMap(),s=new WeakSet(),ok=true;for(let value of [undefined,null,true,1,'s',1n])ok=ok&&!m.has(value)&&!m.delete(value)&&m.get(value)===undefined&&!s.has(value)&&!s.delete(value);ok");
    error("new WeakMap().set(1,2)", "TypeError");
    error("new WeakSet().add(null)", "TypeError");
    error("WeakMap.prototype.get.call(new Map(),{})", "TypeError");
    error("WeakSet.prototype.add.call(new WeakMap(),{})", "TypeError");
    error("WeakMap.prototype.has.call(Object.create(WeakMap.prototype),{})", "TypeError");
    error("WeakMap.prototype.has.call(new Proxy(new WeakMap(),{}),{})", "TypeError");
    error("WeakMap()", "TypeError");
    error("WeakSet()", "TypeError");
}

TEST_F(JsMvpLmd, WeakCollectionsConstructorOrderAndAbruptClose) {
    boolean("let seen='',token={},old=WeakMap.prototype.set;Object.defineProperty(WeakMap.prototype,'set',{get(){seen+='a';return function(k,v){seen+='s';throw token}},configurable:true});let input={[Symbol.iterator](){seen+='i';return {next(){seen+='n';return {value:[{},1],done:false}},return(){seen+='r';throw 3}}}},caught=false;try{new WeakMap(input)}catch(e){caught=e===token}caught&&seen==='ainsr'");
    boolean("let token={},seen='',input={[Symbol.iterator](){return {next(){return {get done(){seen+='d';throw token}}},return(){seen+='r';return {}}}}},caught=false;try{new WeakSet(input)}catch(e){caught=e===token}caught&&seen==='d'");
    boolean("let seen='',token={},input={[Symbol.iterator](){return {next(){return {done:false,value:{get 0(){seen+='k';throw token},get 1(){seen+='v';return 1}}}},return(){seen+='r';return {}}}}},caught=false;try{new WeakMap(input)}catch(e){caught=e===token}caught&&seen==='kr'");
}

TEST_F(JsMvpLmd, WeakCollectionsMetadataAndPrototypeBrand) {
    boolean("let m=WeakMap.prototype.set,d=Object.getOwnPropertyDescriptor(WeakMap.prototype,'set'),t=Object.getOwnPropertyDescriptor(WeakMap.prototype,Symbol.toStringTag);m.name==='set'&&m.length===2&&!d.enumerable&&d.writable&&d.configurable&&t.value==='WeakMap'&&!t.writable&&!t.enumerable&&t.configurable&&Object.keys(WeakMap.prototype).length===0&&Object.prototype.toString.call(new WeakSet())==='[object WeakSet]'");
    error("new WeakMap.prototype.set({},1)", "TypeError");
    boolean("WeakMap.length===0&&WeakSet.length===0&&WeakMap.prototype.constructor===WeakMap&&Object.getPrototypeOf(WeakMap.prototype)===Object.prototype");
}

TEST_F(JsMvpLmd, WeakCollectionsSubclassAndNewTarget) {
    boolean("class W extends WeakMap{get first(){return this.get(this.key)}}let key={},w=new W([[key,3]]);w.key=key;w instanceof W&&w instanceof WeakMap&&w.first===3&&Object.getPrototypeOf(w)===W.prototype");
    boolean("let key={},proto={marker:7};function Target(){}Target.prototype=proto;let m=Reflect.construct(WeakMap,[null],Target);Object.getPrototypeOf(m)===proto&&WeakMap.prototype.set.call(m,key,2)===m&&WeakMap.prototype.get.call(m,key)===2");
}

TEST_F(JsMvpLmd, WeakCollectionsFreezeDoesNotFreezeInternalSlots) {
    boolean("let key={},m=Object.freeze(new WeakMap()),s=Object.seal(new WeakSet());m.set(key,8);s.add(key);Object.isFrozen(m)&&Object.isSealed(s)&&m.get(key)===8&&s.has(key)&&m.delete(key)&&s.delete(key)");
}

TEST_F(JsMvpLmd, CollectionsForEachLiveMutationAndReceiver) {
    boolean("let m=new Map([[1,1.25],[2,2.5]]),seen='',self=null;m.forEach(function(v,k,o){'use strict';self=this;seen+=k+':'+v+';';if(k===1){o.delete(2);o.set(3,3.75)}},null);self===null&&seen==='1:1.25;3:3.75;'");
    boolean("let m=new Map([[1,2]]),seen='';m.forEach((v,k,o)=>{seen+=k;if(k===1){o.clear();o.set(2,3)}});seen==='12'&&m.size===1&&m.has(2)");
    boolean("let s=new Set([1,2]),seen='';s.forEach((v,k,o)=>{seen+=v+':'+k+';';if(v===1){o.delete(2);o.add(3)}});seen==='1:1;3:3;'");
    boolean("let m=new Map([[1,2]]),token={},caught=false;try{m.forEach(()=>{throw token})}catch(e){caught=e===token}m.clear();m.set(3,4);caught&&m.size===1&&m.get(3)===4");
    error("Map.prototype.forEach.call(new Set(),()=>{})", "TypeError");
}

TEST_F(JsMvpLmd, SetOperationsUseSharedAlgorithms) {
    boolean("let a=new Set([1,2,3]),b=new Set([3,4]);[...a.intersection(b)].join(',')==='3'&&[...a.union(b)].join(',')==='1,2,3,4'&&[...a.difference(b)].join(',')==='1,2'&&[...a.symmetricDifference(b)].join(',')==='1,2,4'&&new Set([3]).isSubsetOf(a)&&a.isSupersetOf(new Set([1,2]))&&a.isDisjointFrom(new Set([7]))&&!a.isDisjointFrom(b)");
    boolean("let a=new Set([-0,NaN,1n]),b=new Set([NaN,1n]);let r=a.intersection(b);r.size===2&&r.has(NaN)&&r.has(1n)&&1/[...a.difference(b)][0]===Infinity");
    boolean("let s=new Set([1]);s.intersection(new Map([[1,9],[2,8]])).has(1)&&s.isSubsetOf(new Map([[1,9]]))");
}

TEST_F(JsMvpLmd, SetLikeRecordOrderAndBareIterator) {
    boolean("let seen='',o={get size(){seen+='s';return {valueOf(){seen+='n';return 1.9}}},get has(){seen+='h';return function(k){seen+='H';return k===2}},get keys(){seen+='k';return function(){seen+='K';let n=0;return {next(){seen+='N';return n++?{done:true}:{value:2,done:false}}}}}};let r=new Set([1,2]).intersection(o);[...r].join(',')==='2'&&seen==='snhkKNN'");
    boolean("let seen='',o={get size(){seen+='s';return -1},get has(){seen+='h';return ()=>true}},caught=false;try{new Set().union(o)}catch(e){caught=e instanceof RangeError}caught&&seen==='s'");
    boolean("let seen='',o={get size(){seen+='s';return NaN},get has(){seen+='h';return ()=>true}},caught=false;try{new Set().union(o)}catch(e){caught=e instanceof TypeError}caught&&seen==='s'");
    boolean("let o={size:-0.5,has(){return false},keys(){return {next(){return {done:true}}}}};new Set([1]).difference(o).has(1)");
}

TEST_F(JsMvpLmd, SetOperationsSnapshotAndLiveReceiverRules) {
    boolean("let s=new Set([1,2]),seen='',o={size:9,has(v){seen+=v;if(v===1){s.clear();s.add(3)}return false},keys(){throw 9}};[...s.difference(o)].join(',')==='1,2'&&seen==='12'&&[...s].join(',')==='3'");
    boolean("let s=new Set([1,2]),seen='',o={size:9,has(v){seen+=v;if(v===1){s.clear();s.add(3)}return true},keys(){throw 9}};[...s.intersection(o)].join(',')==='1,3'&&seen==='13'");
    boolean("let s=new Set([1,2,3]),o={size:1,has(){throw 9},keys(){let n=0;return {next(){if(n++===0){s.delete(2);s.add(4);return {done:false,value:2}}return {done:true}}}}};[...s.difference(o)].join(',')==='1,3'&&[...s].join(',')==='1,3,4'");
}

TEST_F(JsMvpLmd, SetOperationsIteratorCloseAndThrowIdentity) {
    boolean("let closed=0,o={size:1,has(){return false},keys(){return {next(){return {value:7,done:false}},return(){closed++;return {}}}}};!new Set([1]).isSupersetOf(o)&&closed===1");
    boolean("let token={},closed=0,o={size:1,has(){return false},keys(){return {next(){throw token},return(){closed++;return {}}}}},caught=false;try{new Set().union(o)}catch(e){caught=e===token}caught&&closed===0");
    boolean("let token={},o={size:1,has(){return false},keys(){return {next(){return {value:7,done:false}},return(){throw token}}}},caught=false;try{new Set([1]).isSupersetOf(o)}catch(e){caught=e===token}caught");
}

TEST_F(JsMvpLmd, CollectionMetadataAndSubclassInternalSlots) {
    boolean("let d=Object.getOwnPropertyDescriptor(Set.prototype,'intersection'),f=Set.prototype.intersection,g=Object.getOwnPropertyDescriptor(Map,Symbol.species).get;d.writable&&!d.enumerable&&d.configurable&&f.name==='intersection'&&f.length===1&&g.name==='get [Symbol.species]'&&g.length===0&&g.call(7)===7&&Set.prototype.keys===Set.prototype.values&&Set.prototype[Symbol.iterator]===Set.prototype.values&&Map.prototype[Symbol.iterator]===Map.prototype.entries");
    boolean("class S extends Set{}let s=new S([1,2]);let result=s.union(new Set([3]));s instanceof S&&s.size===2&&[...s].join(',')==='1,2'&&result instanceof Set&&!(result instanceof S)&&[...result].join(',')==='1,2,3'");
    boolean("class M extends Map{x=4}let m=new M([[1,2]]);m.x===4&&m.get(1)===2&&m.size===1&&[...m.keys()][0]===1");
    error("new Set.prototype.union(new Set())", "TypeError");
    error("Set.prototype.union.call(new WeakSet(),new Set())", "TypeError");
}

TEST_F(JsMvpLmd, DynamicDirectEvalLiveCells) {
    boolean(R"JS(
function check(value){if(!value)throw new Error('direct eval live cells');}
let seen=[];
let run=Function('cb','let x=1; const read=()=>x; eval("x=2; cb(read)"); return [x,read]');
let result=run(function(read){seen.push(read())});
check(result[0]===2&&result[1]()===2&&seen[0]===2);
check(Function('a','eval("a=7"); return arguments[0]===7')(1));
check(Function('a','eval("arguments[0]=9"); return a===9')(1));
check(Function('a=1','eval("a=7"); return arguments[0]===undefined&&a===7')());
let object={};check(Function('value','return eval("value")')(object)===object);
let marker={};let identity=false;try{Function('marker','eval("throw marker")')(marker)}catch(e){identity=e===marker}check(identity);
let escaped=Function('let x=1; return eval("(()=>++x)")')();
check(escaped()===2&&escaped()===3);
true;
    )JS");
}

TEST_F(JsMvpLmd, DynamicDirectEvalDeclarations) {
    boolean(R"JS(
function check(value){if(!value)throw new Error('direct eval declarations');}
check(Function('eval("var created=2; function read(){return created}"); created=3; return read()===3')());
let read=Function('eval("var late=4"); return ()=>late')();check(read()===4);
check(Function('eval("var x=1; eval(\\"x=3; var y=4\\")"); return x===3&&y===4')());
check(Function('var fixed=1; eval("var added=2"); return eval("delete added")&&typeof added==="undefined"&&!eval("delete fixed")')());
globalThis.mvpEvalOrder=1;
let order=Function('mvpEvalOrder=eval("var mvpEvalOrder=2; 3"); return [mvpEvalOrder,globalThis.mvpEvalOrder]')();
check(order[0]===2&&order[1]===3);delete globalThis.mvpEvalOrder;
check(Function('eval("var n=5e-324"); const read=()=>n; eval("n=1.25"); return read()===1.25')());
true;
    )JS");
}

TEST_F(JsMvpLmd, DynamicDirectEvalDispatch) {
    boolean(R"JS(
function check(value){if(!value)throw new Error('direct eval dispatch');}
check(Function('eval','return eval(3)')(function(value){return value+1})===4);
check(Function('let eval=(value)=>value+2; return eval(3)')()===5);
globalThis.mvpEvalDispatch='global';
check(Function('let mvpEvalDispatch="local"; return eval("mvpEvalDispatch")')()==='local');
check(Function('let mvpEvalDispatch="local"; return (0,eval)("mvpEvalDispatch")')()==='global');
check(Function('let mvpEvalDispatch="local"; return eval?.("mvpEvalDispatch")')()==='global');
check(Function('let mvpEvalDispatch="local"; let alias=eval; return alias("mvpEvalDispatch")')()==='global');
check(Function('value','return eval(value)')(5e-324)===5e-324);
let object={};check(Function('value','return eval(value)')(object)===object);
check(Function('return eval()')()===undefined);
delete globalThis.mvpEvalDispatch;
true;
    )JS");
}

TEST_F(JsMvpLmd, DynamicDirectEvalStrictParameters) {
    boolean(R"JS(
function check(value){if(!value)throw new Error('direct eval strict parameters');}
check(Function('"use strict"; let x=1; eval("x=2; var hidden=3"); return x===2&&typeof hidden==="undefined"')());
check(Function('eval("\\"use strict\\"; var hidden=3"); return typeof hidden==="undefined"')());
check(Function('let x=1; try{eval("var x");return false}catch(e){return e instanceof SyntaxError&&x===1}')());
check(Function('const x=1; try{eval("x=2");return false}catch(e){return e instanceof TypeError&&x===1}')());
check(Function('try{eval("x");return false}catch(e){return e instanceof ReferenceError} let x=1')());
check(Function('a=1','b=eval("a=3; a")','return a===3&&b===3')());
let clash=false;try{Function('a=eval("var a")','return a')()}catch(e){clash=e instanceof SyntaxError}check(clash);
check(Function('a=()=>eval("arguments")','var arguments=7; return a().length===0&&arguments===7')());
true;
    )JS");
}

TEST_F(JsMvpLmd, DynamicEvalGlobalDeclarationPolicy) {
    boolean(R"JS(
function check(value){if(!value)throw new Error('direct eval globals');}
let mvpEvalLexical=1,failed=false;
try{(0,eval)('var mvpEvalLexical')}catch(e){failed=e instanceof SyntaxError}check(failed);
failed=false;try{(0,eval)('eval("var mvpEvalLexical")')}catch(e){failed=e instanceof SyntaxError}check(failed);
(0,eval)('{function mvpEvalLexical(){return 2}}');check(mvpEvalLexical===1);
let reads=0;
Object.defineProperty(globalThis,'mvpEvalFunction',{get(){reads++;return 9},configurable:true});
(0,eval)('function mvpEvalFunction(){return 3}');
let descriptor=Object.getOwnPropertyDescriptor(globalThis,'mvpEvalFunction');
check(reads===0&&descriptor.writable&&descriptor.enumerable&&descriptor.configurable&&descriptor.value()===3);
Object.defineProperty(globalThis,'mvpEvalBlocked',{value:1,writable:false,enumerable:true,configurable:false});
failed=false;try{(0,eval)('function mvpEvalBlocked(){}')}catch(e){failed=e instanceof TypeError}check(failed&&mvpEvalBlocked===1);
(0,eval)('var mvpEvalOuter=1; eval("mvpEvalOuter=3");');check(globalThis.mvpEvalOuter===3);
(0,eval)('var mvpEvalDeletable=1');check(delete globalThis.mvpEvalDeletable);
Object.preventExtensions(globalThis);
(0,eval)('{function mvpEvalNoExtension(){}}');check(!Object.hasOwn(globalThis,'mvpEvalNoExtension'));
true;
    )JS");
}

TEST_F(JsMvpLmd, DynamicDirectEvalCallContext) {
    boolean(R"JS(
function check(value){if(!value)throw new Error('direct eval context');}
let object={value:7};check(Function('return eval("this.value")').call(object)===7);
let F=Function('return eval("new.target")');check(F()===undefined&&new F()===F);
let Arrow=Function('return (()=>eval("new.target"))()');check(Arrow()===undefined&&new Arrow()===Arrow);
let Default=Function('a=eval("new.target")','return a');check(Default()===undefined&&new Default()===Default);
let failed=false;try{(0,eval)('new.target')}catch(e){failed=e instanceof SyntaxError}check(failed);
failed=false;try{(0,eval)('eval("new.target")')}catch(e){failed=e instanceof SyntaxError}check(failed);
true;
    )JS");
}


TEST_F(JsMvpLmd, DynamicArgumentsOwnScalarStorage) {
    boolean(R"JS(
function check(value){if(!value)throw new Error('arguments scalar owner');}
let escaped=Function('arguments=5e-324; return ()=>arguments')();
for(let i=0;i<40;i++)Function('value','return [value]')(i+0.25);
check(escaped()===5e-324);
let retained=Function('arguments=5e-324; let old=arguments+(arguments=1.25,0); return [old,arguments]')();
check(retained[0]===5e-324&&retained[1]===1.25);
let throughEval=Function('eval("arguments=5e-324"); return ()=>eval("arguments")')();
check(throughEval()===5e-324);
true;
    )JS");
}

TEST_F(JsMvpLmd, DynamicSuspendedConstructorFamilies) {
    boolean(R"JS(
        let functions=[function*(){},async function(){},async function*(){}];
        let names=['GeneratorFunction','AsyncFunction','AsyncGeneratorFunction'];
        for(let i=0;i<3;i++){
            let p=Object.getPrototypeOf(functions[i]),C=p.constructor;
            if(C.name!==names[i]||C.length!==1||C.prototype!==p||Object.getPrototypeOf(C)!==Function)throw 1;
            if(Object.getPrototypeOf(p)!==Function.prototype||p[Symbol.toStringTag]!==names[i])throw 2;
            let d=Object.getOwnPropertyDescriptor(C,'prototype');
            if(d.writable||d.enumerable||d.configurable)throw 3;
            let f=C('a','return a');
            if(f.name!=='anonymous'||f.length!==1||Object.getPrototypeOf(f)!==p)throw 4;
            let caught=false;try{new f()}catch(e){caught=e instanceof TypeError}if(!caught)throw 5;
            class Derived extends C{}let g=new Derived('return 3');
            if(Object.getPrototypeOf(g)!==Derived.prototype||Object.getPrototypeOf(Derived.prototype)!==p)throw 6;
        }
        true
    )JS");
}
TEST_F(JsMvpLmd, DynamicAsyncPrefixAwaitAndThenables) {
    expect_async_flags(run(R"JS(
        let out=[],order='';
        let C=Object.getPrototypeOf(async function(){}).constructor;
        let f=C('x','order += "a"; let y=await x; order += "b"; return y+1');
        f({then(resolve){order+='t';resolve(6)}}).then(v=>out.push(v===7&&order==='actb'));
        order+='c';out
    )JS"),1);
}
TEST_F(JsMvpLmd, DynamicAsyncParameterRejectionAndClosureCells) {
    expect_async_flags(run(R"JS(
        let out=[],marker={};let C=Object.getPrototypeOf(async function(){}).constructor;
        let f=C('a=(()=>{throw marker})()','return a');
        f().then(()=>out.push(false),e=>out.push(e===marker));
        let make=Function('return async function(a){await 0;return ()=>++a}')();
        make(4).then(next=>out.push(next()===5&&next()===6));out
    )JS"),2);
}
TEST_F(JsMvpLmd, DynamicSuspendedArgumentsAndParameters) {
    expect_async_flags(run(R"JS(
        let out=[];let A=Object.getPrototypeOf(async function(){}).constructor;
        A('a','arguments[0]=5;await 0;a++;return arguments[0]')(1).then(v=>out.push(v===6));
        A('a=()=>arguments','var arguments=7;await 0;return a().length===0&&arguments===7')().then(v=>out.push(v));
        let G=Object.getPrototypeOf(function*(){}).constructor;
        let g=G('a','arguments[0]=4;yield a;a++;return arguments[0]')(1);
        out.push(g.next().value===4&&g.next().value===5);
        let h=G('a=()=>arguments','var arguments=7;yield a().length===0;return arguments===7')();
        out.push(h.next().value&&h.next().value);out
    )JS"),4);
}
TEST_F(JsMvpLmd, DynamicSuspendedWideReceiverAndArguments) {
    expect_async_flags(run(R"JS(
        let out=[];let A=Object.getPrototypeOf(async function(){}).constructor;
        let a=A('x','"use strict";await 0;return this===5e-324&&1/x===-Infinity');
        a.call(5e-324,-0).then(v=>out.push(v));
        let G=Object.getPrototypeOf(function*(){}).constructor;
        let g=G('x','"use strict";yield this===5e-324;return 1/x===-Infinity').call(5e-324,-0);
        out.push(g.next().value);for(let i=0;i<100;i++){let z=[i+0.25]};out.push(g.next().value);out
    )JS"),3);
}
TEST_F(JsMvpLmd, DynamicGeneratorResumeAbruptAndPrototype) {
    boolean(R"JS(
        let G=Object.getPrototypeOf(function*(){}).constructor;
        let g=G('try{yield 1}catch(e){yield e}finally{yield 3}return 4')();
        if(g.next().value!==1||g.throw(2).value!==2||g.return(8).value!==3)throw 1;
        let last=g.next();if(!last.done||last.value!==8)throw 2;
        let before=G('throw 1')();if(!before.return(9).done)throw 3;
        let marker={},caught=false;try{G('yield 1')().throw(marker)}catch(e){caught=e===marker}if(!caught)throw 4;
        let next=G.prototype.prototype.next;
        let replacement={},f=G('x=(f.prototype=replacement)','yield x'),it=f();
        Object.getPrototypeOf(it)===replacement&&next.call(it).value===replacement
    )JS");
}
TEST_F(JsMvpLmd, DynamicGeneratorDelegationCachesNextAndReceiver) {
    boolean(R"JS(
        let G=Object.getPrototypeOf(function*(){}).constructor;
        let gets=0,calls=0,receiver=true,args='';
        let iterator={get next(){gets++;return function(v){receiver=receiver&&this===iterator;args+=arguments.length+':'+v+',';return calls++?{done:true,value:7}:{done:false,value:3}}}};
        let iterable={[Symbol.iterator](){return iterator}};
        let g=G('source','return yield* source')(iterable),first=g.next();
        Object.defineProperty(iterator,'next',{value(){throw 9}});
        let last=g.next(4);
        first.value===3&&!first.done&&last.done&&last.value===7&&gets===1&&receiver&&args==='1:undefined,1:4,'
    )JS");
}
TEST_F(JsMvpLmd, DynamicGeneratorDelegationAbruptClose) {
    boolean(R"JS(
        let G=Object.getPrototypeOf(function*(){}).constructor;let closed=0,receiver=false;
        let iterator={next(){return {value:1,done:false}},return(){closed++;receiver=this===iterator&&arguments.length===0;return {done:true}}};
        let g=G('source','try{yield* source}catch(e){return e instanceof TypeError}')({[Symbol.iterator](){return iterator}});
        g.next();let result=g.throw(9);if(!result.done||!result.value||closed!==1||!receiver)throw 1;
        let marker={},other={next(){return {value:1,done:false}},get return(){throw marker}};
        let h=G('source','try{yield* source}catch(e){return e}')({[Symbol.iterator](){return other}});
        h.next();if(h.return(3).value!==marker)throw 2;
        let attempts=0,outer,reentrant={next(){return {value:1,done:false}},get return(){
            try{outer.next()}catch(e){if(e instanceof TypeError)attempts++}
            return function(){try{outer.throw(0)}catch(e){if(e instanceof TypeError)attempts++}return {done:true,value:8}}
        }};
        outer=G('source','yield* source')({[Symbol.iterator](){return reentrant}});
        outer.next();let final=outer.return(4);final.done&&final.value===8&&attempts===2
    )JS");
}
TEST_F(JsMvpLmd, DynamicAsyncGeneratorQueueAndFinally) {
    expect_async_flags(run(R"JS(
        let out=[];let C=Object.getPrototypeOf(async function*(){}).constructor;
        let g=C('try{yield await 1;yield 2}finally{yield 3}')();
        g.next().then(r=>out.push(r.value===1&&!r.done));
        g.return(8).then(r=>out.push(r.value===3&&!r.done));
        g.next().then(r=>out.push(r.value===8&&r.done));out
    )JS"),3);
}
TEST_F(JsMvpLmd, DynamicAsyncGeneratorAndForAwait) {
    expect_async_flags(run(R"JS(
        let out=[];let A=Object.getPrototypeOf(async function(){}).constructor;
        let G=Object.getPrototypeOf(async function*(){}).constructor;
        let g=G('source','return yield* source')([Promise.resolve(2),3]);
        A('source','let sum=0;for await(let value of source){sum+=value}return sum')(g).then(v=>out.push(v===5));
        let closed=0,iterator={next(){return {value:Promise.resolve(7),done:false}},return(){closed++;return {done:true}}};
        A('source','for await(let value of source){return value}')({[Symbol.iterator](){return iterator}})
            .then(v=>out.push(v===7&&closed===1));out
    )JS"),2);
}
TEST_F(JsMvpLmd, DynamicSuspendedProfileInterleaving) {
    expect_async_flags(run(R"JS(
        let out=[],globalCount=0;let make=Function('return function*(n){let local=n;yield ++local;globalCount+=local;return ()=>++local}')();
        let a=make(1),b=make(10);out.push(a.next().value===2&&b.next().value===11);
        function ordinary(x){return {value:x+1}}
        out.push(ordinary(5).value===6);
        let finishA=a.next().value,finishB=b.next().value;
        out.push(globalCount===13&&finishA()===3&&finishB()===12);
        let f=Function('callback','return async function(n){await 0;let v=callback(n);await 0;return v.value}')(ordinary);
        f(8).then(v=>out.push(v===9&&globalCount===13));out
    )JS"),4);
}
TEST_F(JsMvpLmd, DynamicSuspendedEarlyErrorsAndNewTarget) {
    boolean(R"JS(
        let G=Object.getPrototypeOf(function*(){}).constructor,A=Object.getPrototypeOf(async function(){}).constructor;
        let failures=0;try{G('yield','return 1')}catch(e){if(e instanceof SyntaxError)failures++}
        try{A('await','return 1')}catch(e){if(e instanceof SyntaxError)failures++}
        let g=G('yield new.target')();failures===2&&g.next().value===undefined
    )JS");
}

TEST_F(JsMvpLmd, CollectionGroupByStreamsCallbacksAndKeys) {
    boolean("let seen='',input={[Symbol.iterator](){let i=0;return {next(){seen+='n';return i<3?{value:++i,done:false}:{done:true}}}}};let m=Map.groupBy(input,(v,i)=>{seen+='c'+i;return v%2});seen==='nc0nc1nc2n'&&m.size===2&&m.get(1).join(',')==='1,3'&&m.get(0).join(',')==='2'");
    boolean("let a={},b={},s=Symbol('s'),m=Map.groupBy([a,b,a,s],x=>x);m.size===3&&m.get(a).length===2&&m.get(b)[0]===b&&m.get(s)[0]===s");
    boolean("let m=Map.groupBy([-0,0,NaN,NaN,1n,1n],x=>x);m.size===3&&1/[...m.keys()][0]===Infinity&&m.get(0).length===2&&m.get(NaN).length===2&&m.get(1n).length===2");
}

TEST_F(JsMvpLmd, CollectionGroupByClosesOnCallbackAndKeyFailure) {
    boolean("let token={},seen='',input={[Symbol.iterator](){return {next(){seen+='n';return {value:1,done:false}},return(){seen+='r';throw 3}}}},caught=false;try{Map.groupBy(input,()=>{seen+='c';throw token})}catch(e){caught=e===token}caught&&seen==='ncr'");
    boolean("let token={},closed=0,input={[Symbol.iterator](){return {next(){throw token},return(){closed++;return {}}}}},caught=false;try{Map.groupBy(input,x=>x)}catch(e){caught=e===token}caught&&closed===0");
    boolean("let token={},closed=0,input={[Symbol.iterator](){return {next(){return {value:1,done:false}},return(){closed++;return {}}}}},caught=false;try{Object.groupBy(input,()=>({toString(){throw token}}))}catch(e){caught=e===token}caught&&closed===1");
    boolean("let seen=0,o={get [Symbol.iterator](){seen++;return null}},caught=false;try{Map.groupBy(o,1)}catch(e){caught=e instanceof TypeError}caught&&seen===0");
}

TEST_F(JsMvpLmd, ObjectGroupByUsesNullPrototypeOwnProperties) {
    boolean("let s=Symbol('s'),result=Object.groupBy([1,2,3],v=>v===2?s:'__proto__');Object.getPrototypeOf(result)===null&&result.__proto__.join(',')==='1,3'&&result[s][0]===2&&Object.getOwnPropertySymbols(result)[0]===s");
    boolean("let seen=0;Object.defineProperty(Array.prototype,'0',{set(){seen++},configurable:true});let r=Object.groupBy([1,2],()=> 'x');r.x[0]===1&&r.x[1]===2&&seen===0");
    boolean("let seen='',r=Object.groupBy([1,2],v=>({toString(){seen+=v;return 'x'}})),d=Object.getOwnPropertyDescriptor(r,'x');seen==='12'&&d.writable&&d.enumerable&&d.configurable&&r.x.join(',')==='1,2'");
}

TEST_F(JsMvpLmd, CollectionGroupByIntrinsicAndMetadata) {
    boolean("let seen=0;Map.prototype.set=function(){seen++;throw 9};let result=Map.groupBy.call(function(){throw 7},[1,2],x=>x%2);result.size===2&&result.get(1)[0]===1&&seen===0");
    boolean("let f=Map.groupBy,d=Object.getOwnPropertyDescriptor(Map,'groupBy');f.name==='groupBy'&&f.length===2&&!d.enumerable&&d.writable&&d.configurable&&Object.groupBy.length===2");
    error("new Map.groupBy([],x=>x)", "TypeError");
}

TEST_F(JsMvpLmd, SetSnapshotOrderAroundIteratorAcquisition) {
    boolean("let s=new Set([1,2,3]),seen='',o={size:1,has(){throw 9},keys(){seen+='k';s.delete(1);s.add(4);return {get next(){seen+='n';s.delete(2);s.add(5);return function(){seen+='N';return {done:true}}}}}};let r=s.union(o);[...r].join(',')==='3,4,5'&&[...s].join(',')==='3,4,5'&&seen==='knN'");
    boolean("let s=new Set([1,2,3]),seen='',o={size:1,has(){throw 9},keys(){seen+='k';s.delete(1);s.add(4);return {get next(){seen+='n';s.delete(2);s.add(5);return function(){seen+='N';return {done:true}}}}}};let r=s.symmetricDifference(o);[...r].join(',')==='3,4,5'&&[...s].join(',')==='3,4,5'&&seen==='knN'");
    boolean("let s=new Set([1,2,3]),seen='',o={size:1,has(){throw 9},keys(){seen+='k';s.delete(1);s.add(4);return {get next(){seen+='n';s.delete(2);s.add(5);return function(){seen+='N';return {done:true}}}}}};let r=s.difference(o);[...r].join(',')==='1,2,3'&&[...s].join(',')==='3,4,5'&&seen==='knN'");
}

TEST_F(JsMvpLmd, SymbolRegistryIdentityAndPropertyRoundTrip) {
    boolean("let a=Symbol.for('shared'),b=Symbol.for('shared'),u=Symbol('shared'),o={[a]:7};a===b&&a!==u&&Symbol.keyFor(a)==='shared'&&Symbol.keyFor(u)===undefined&&Object.getOwnPropertySymbols(o)[0]===a&&Reflect.ownKeys(o)[0]===a&&o[b]===7");
    boolean("let key='';for(let i=0;i<1500;i++)key+='x';let symbol=Symbol.for(key);Symbol.keyFor(symbol)===key&&symbol.description===key&&Symbol.for(key+'y')!==symbol");
    boolean("Symbol.for()===Symbol.for(undefined)&&Symbol.keyFor(Symbol.for())==='undefined'&&Symbol.keyFor(Symbol.for(null))==='null'&&Symbol.keyFor(Symbol.iterator)===undefined");
}

TEST_F(JsMvpLmd, SymbolRegistryCoercionAndAbruptIdentity) {
    boolean("let seen='',key={toString(){seen+='s';return 'registry'},valueOf(){throw 9}};Symbol.for(key)===Symbol.for('registry')&&seen==='s'");
    boolean("let token={},caught=false;try{Symbol.for({toString(){throw token}})}catch(e){caught=e===token}caught");
    error("Symbol.for(Symbol('x'))", "TypeError");
    error("Symbol.keyFor(Object(Symbol('x')))", "TypeError");
    error("Symbol.keyFor(1)", "TypeError");
}

TEST_F(JsMvpLmd, RegisteredSymbolsAreNotWeakKeys) {
    boolean("let s=Symbol.for('weak'),w=new WeakMap(),q=new WeakSet();!w.has(s)&&w.get(s)===undefined&&!w.delete(s)&&!q.has(s)&&!q.delete(s)");
    error("new WeakMap().set(Symbol.for('weak'),1)", "TypeError");
    error("new WeakSet().add(Symbol.for('weak'))", "TypeError");
    boolean("let u=Symbol('unique'),w=new WeakMap([[u,9]]),q=new WeakSet([u]);w.get(u)===9&&q.has(u)");
}

TEST_F(JsMvpLmd, SymbolRegistryMetadataAndRetention) {
    boolean("let f=Symbol.for,k=Symbol.keyFor,d=Object.getOwnPropertyDescriptor(Symbol,'for');f.name==='for'&&k.name==='keyFor'&&f.length===1&&k.length===1&&d.writable&&!d.enumerable&&d.configurable");
    boolean("let first=Symbol.for('keep'),o={[first]:42};for(let i=0;i<80;i++){Symbol.for('key'+i);let a=[i,{i}]}Symbol.for('keep')===first&&Object.getOwnPropertySymbols(o)[0]===first&&Symbol.keyFor(first)==='keep'");
    error("new Symbol.for('x')", "TypeError");
    error("new Symbol.keyFor(Symbol('x'))", "TypeError");
}

TEST_F(JsMvpLmd, CollectionIteratorMetadataFamilies) {
    boolean("let m=new Map([[1,2]]).keys(),s=new Set([1]).values(),mp=Object.getPrototypeOf(m),sp=Object.getPrototypeOf(s);mp!==sp&&mp.next.name==='next'&&sp.next.name==='next'&&mp.next!==sp.next&&mp[Symbol.toStringTag]==='Map Iterator'&&sp[Symbol.toStringTag]==='Set Iterator'&&Object.prototype.toString.call(m)==='[object Map Iterator]'&&Object.prototype.toString.call(s)==='[object Set Iterator]'");
    boolean("let p=Object.getPrototypeOf(new Map().values()),d=Object.getOwnPropertyDescriptor(p,Symbol.toStringTag),n=Object.getOwnPropertyDescriptor(p.next,'name');!d.writable&&!d.enumerable&&d.configurable&&n.value==='next'&&!n.writable&&!n.enumerable&&n.configurable&&!Object.prototype.hasOwnProperty.call(p,'constructor')");
    boolean("let a=new Map().keys(),b=new Map().entries(),c=new Set().keys(),d=new Set().entries();Object.getPrototypeOf(a)===Object.getPrototypeOf(b)&&Object.getPrototypeOf(c)===Object.getPrototypeOf(d)&&a[Symbol.iterator]()===a&&c[Symbol.iterator]()===c");
}

TEST_F(JsMvpLmd, CollectionIteratorPhysicalBrandAndRetention) {
    boolean("let m=new Map([[1,2]]).values(),s=new Set([3]).values(),mn=m.next,sn=s.next,caught=0;try{mn.call(s)}catch(e){if(e instanceof TypeError)caught++}try{sn.call(m)}catch(e){if(e instanceof TypeError)caught++}Object.setPrototypeOf(m,null);mn.call(m).value===2&&mn.call(m).done&&mn.call(m).done&&caught===2");
    boolean("let m=new Map([[1,2]]).values(),s=new Set([3]).values(),mn=m.next;m.next();m.next();let caught=false;try{mn.call(s)}catch(e){caught=e instanceof TypeError}caught&&mn.call(m).done");
    boolean("let m=new Map([[1,1.25]]).values(),next=m.next,p=Object.getPrototypeOf(m);for(let i=0;i<90;i++){let a=[i,{i}]}next.call(m).value===1.25&&Object.getPrototypeOf(m)===p&&p[Symbol.toStringTag]==='Map Iterator'");
}

TEST_F(JsMvpLmd, ClosedGeneratorFamilyIsOptional) {
    boolean("function* unused(){}true");
    MvpLmdLibraryState* library = mvp_lmd_program_library();
    ASSERT_NE(library, nullptr);
    EXPECT_FALSE(library->mutable_prototypes);
    EXPECT_EQ(library->generator_function_values, nullptr);
    boolean("function* g(){yield 7}let it=g();let a=it.next();a.value===7&&!a.done&&it.next().done");
    library = mvp_lmd_program_library();
    ASSERT_NE(library, nullptr);
    EXPECT_FALSE(library->mutable_prototypes);
    EXPECT_EQ(library->generator_function_values, nullptr);
    boolean("function* g(){}let prototype=Object.getPrototypeOf(g);prototype.constructor.prototype===prototype&&Object.getPrototypeOf(prototype)===Function.prototype");
    library = mvp_lmd_program_library();
    ASSERT_NE(library, nullptr);
    EXPECT_TRUE(library->mutable_prototypes);
    EXPECT_NE(library->generator_function_values, nullptr);
}

TEST_F(JsMvpLmd, SymbolDescriptionUsesSharedSymbolIdentity) {
    boolean("Symbol().description===undefined&&Symbol('').description===''&&Symbol('hello').description==='hello'&&Symbol.for('registered').description==='registered'");
    boolean("let d=Object.getOwnPropertyDescriptor(Symbol.prototype,'description'),s=Symbol('boxed');d.get.name==='get description'&&d.get.length===0&&d.set===undefined&&!d.enumerable&&d.configurable&&d.get.call(Object(s))==='boxed'");
    error("Object.getOwnPropertyDescriptor(Symbol.prototype,'description').get.call(null)", "TypeError");
    error("Object.getOwnPropertyDescriptor(Symbol.prototype,'description').get.call(Symbol.prototype)", "TypeError");
    error("Object.getOwnPropertyDescriptor(Symbol.prototype,'description').get.call(Object.create(Object(Symbol('x'))))", "TypeError");
    error("Object.getOwnPropertyDescriptor(Symbol.prototype,'description').get.call(new Proxy(Object(Symbol('x')),{}))", "TypeError");
}

TEST_F(JsMvpLmd, CollectionIteratorEscapedNextBrand) {
    boolean("let m=new Map([[1,2]]).values(),s=new Set([3]).values(),mn=m.next;m.next();m.next();let caught=false;try{mn.call(s)}catch(e){caught=e instanceof TypeError}caught&&mn.call(m).done");
    boolean("function key(){return 'next'}let m=new Map().keys(),s=new Set().values(),next=m[key()],caught=false;try{next.call(s)}catch(e){caught=e instanceof TypeError}caught&&next.call(m).done");
    boolean("let m=new Map([[1,2]]).values();m.next().value===2&&m.next().done");
}

TEST_F(JsMvpLmd, DynamicClassPublic) {
    boolean(R"JS(
function check(x){if(!x)throw new Error('hosted class public');}
let C=Function('return class C { x=2; constructor(v=3){this.y=v} sum(){return this.x+this.y} get value(){return this.y} set value(v){this.y=v} static answer=7; static read(){return this.answer} }')();
let c=new C(5);check(c.sum()===7&&c.value===5&&C.read()===7);c.value=11;check(c.sum()===13);
let f=c.sum;check(f.call({x:20,y:2})===22);check(!Object.getOwnPropertyDescriptor(C.prototype,'sum').enumerable);
check(C.prototype.constructor===C&&c instanceof C&&C.length===0&&C.name==='C');
let d=Object.getOwnPropertyDescriptor(C,'prototype');check(!d.writable&&!d.enumerable&&!d.configurable);
check(C.toString().indexOf('class C')===0);
true;
    )JS");
}

TEST_F(JsMvpLmd, DynamicClassSuper) {
    boolean(R"JS(
function check(x){if(!x)throw new Error('hosted class super');}
let classes=Function('class A { constructor(x){this.x=x} get value(){return this.x} set value(x){this.x=x} plus(v){return this.x+v} static base(){return this.tag} } class B extends A { y=4; constructor(x=3){super(x);this.z=this.y+1} sum(){super.value=7;return super.plus(this.y)} arrow(){return ()=>super.plus(this.y)} static tag=9; static read(){return super.base()} } return [A,B]')();
let A=classes[0],B=classes[1],b=new B();check(b.x===3&&b.y===4&&b.z===5&&b.sum()===11&&b.arrow()()===11&&B.read()===9);
check(b instanceof A&&b instanceof B&&Object.getPrototypeOf(B)===A&&Object.getPrototypeOf(B.prototype)===A.prototype);
let D=Function('A','return class extends A { y=8 }')(A);check(new D(6).x===6&&new D(6).y===8);
true;
    )JS");
}

TEST_F(JsMvpLmd, DynamicClassConstructorReturns) {
    boolean(R"JS(
function check(x){if(!x)throw new Error('hosted class constructor completion');}
let result=Function('let count=0; class A { constructor(){count++;this.x=1} } class B extends A { constructor(){super();try{super()}catch(e){if(!(e instanceof ReferenceError))throw e}} } let b=new B(); let R=class extends A { constructor(){return {ok:1}} }; let bad=false;try{new(class extends A {constructor(){return 1}})}catch(e){bad=e instanceof TypeError} let missing=false;try{new(class extends A {constructor(){}})}catch(e){missing=e instanceof ReferenceError} return [count,b.x,new R().ok,bad,missing,new(class {constructor(){return 3}}) instanceof Object]')();
check(result[0]===2&&result[1]===1&&result[2]===1&&result[3]&&result[4]&&result[5]);
true;
    )JS");
}

TEST_F(JsMvpLmd, DynamicClassComputedOrder) {
    boolean(R"JS(
function check(x){if(!x)throw new Error('hosted class key order');}
let order=[];let C=Function('order','return class C { [(order.push("k1"),this.key)](){return 1} static a=(order.push("s1"),C); [(order.push("k2"),"x")]=2; static {order.push("s2")} }').call({key:'method'},order);
check(order.join(',')==='k1,k2,s1,s2'&&C.a===C&&new C().method()===1);
check(Function('try{(class C {[C](){}})}catch(e){return e instanceof ReferenceError}return false')());
check(Function('try{class C {static x=outside()}function outside(){return C}}catch(e){return e instanceof ReferenceError}return false')());
true;
    )JS");
}

TEST_F(JsMvpLmd, DynamicClassPrivate) {
    boolean(R"JS(
function check(x){if(!x)throw new Error('hosted class private');}
let C=Function('return class C { #x=1; #method(){return this.#x} get #value(){return this.#x} set #value(v){this.#x=v} read(){return this.#method()} write(v){this.#value=v;return this.#value} has(o){return #x in o} static #s=10; static #f(){return this.#s} static read(){return this.#f()} }')();
let c=new C();check(c.read()===1&&c.write(6)===6&&c.has(c)&&!c.has({})&&C.read()===10);
check(Reflect.ownKeys(c).length===0);let fail=false;try{c.read.call({})}catch(e){fail=e instanceof TypeError}check(fail);
let C2=Function('return class {#x=2;has(o){return #x in o}}')();check(!new C2().has(c));
true;
    )JS");
}

TEST_F(JsMvpLmd, DynamicClassPrivateEval) {
    boolean(R"JS(
function check(x){if(!x)throw new Error('hosted class private eval');}
let C=Function('return class C { #x=3; read(){return eval("this.#x")} write(v){return eval("this.#x=v")} has(o){return eval("#x in o")} nested(){return ()=>eval("this.#x")} static #s=8;static read(){return eval("this.#s")} }')();
let c=new C();check(c.read()===3&&c.write(7)===7&&c.has(c)&&!c.has({})&&c.nested()()===7&&C.read()===8);
let result=Function('class A { constructor(){this.x=4} f(){return this.x} } class B extends A { constructor(){eval("super()")} f(){return eval("super.f()")+1} } return new B().f()')();check(result===5);
true;
    )JS");
}

TEST_F(JsMvpLmd, DynamicClassStaticScope) {
    boolean(R"JS(
function check(x){if(!x)throw new Error('hosted class static scope');}
let result=Function('var v="outer";class C {static {var v="inner";this.first=v;this.target=new.target;this.read=()=>this} static {this.second=v} static x=new.target}return [C,v]')();
let C=result[0];check(C.first==='inner'&&C.second==='outer'&&C.target===undefined&&C.x===undefined&&C.read()===C&&result[1]==='outer');
let D=Function('return class {x=new.target;field=()=>this;static field=()=>this}')();let d=new D();check(d.x===undefined&&d.field()===d&&D.field()===D);
true;
    )JS");
}

TEST_F(JsMvpLmd, DynamicClassFieldNames) {
    boolean(R"JS(
function check(x){if(!x)throw new Error('hosted class field names');}
let result=Function('let seen=[];let k=Symbol("key");class C { f=function(){};a=()=>1; nested=class {static {seen.push(this.name)} static name="kept"};[k]=function(){};static nested=class {static {seen.push(this.name)}}; #f=function(){};read(){return this.#f.name}}return [new C(),C,seen,k]')();
let c=result[0],C=result[1];check(c.f.name==='f'&&c.a.name==='a'&&c.nested.name==='kept'&&c[result[3]].name==='[key]'&&c.read()==='#f');check(result[2].join(',')==='nested,nested'&&C.nested.name==='nested');
true;
    )JS");
}

TEST_F(JsMvpLmd, DynamicClassNativeNewtarget) {
    boolean(R"JS(
function check(x){if(!x)throw new Error('hosted class native newTarget');}
let T=Function('return class extends Uint8Array { x=4; constructor(n){super(n);this.y=5} }')();let t=new T(3);check(t.length===3&&t.x===4&&t.y===5&&t instanceof T&&t instanceof Uint8Array);
let A=Function('return class {x=1;constructor(){this.target=new.target}}')();function Other(){};let o=Reflect.construct(A,[],Other);check(Object.getPrototypeOf(o)===Other.prototype&&o.x===1&&o.target===Other);
let P=Function('return class extends Promise {x=9;constructor(f){super(f)}}')();let p=new P(function(resolve){resolve(2)});check(p.x===9&&p instanceof P&&p instanceof Promise);
true;
    )JS");
}

TEST_F(JsMvpLmd, DynamicClassEvalRestrictions) {
    boolean(R"JS(
function check(x){if(!x)throw new Error('hosted class eval context');}
let results=Function('class A{}class B extends A { static bad(){try{eval("super()")}catch(e){return e instanceof SyntaxError}} field=(()=>{try{eval("arguments")}catch(e){return e instanceof SyntaxError}})(); static block;static {try{eval("arguments")}catch(e){this.block=e instanceof SyntaxError}} }return [B.bad(),new B().field,B.block]')();check(results[0]&&results[1]&&results[2]);
let bad=false;try{(0,eval)('super.x')}catch(e){bad=e instanceof SyntaxError}check(bad);
true;
    )JS");
}

TEST_F(JsMvpLmd, DynamicClassReentry) {
    boolean(R"JS(
function check(x){if(!x)throw new Error('hosted class reentry');}
let make=Function('id','return class { #id=id; #method(){return this.#id} get(){return this.#method()} has(o){return #id in o} constructor(){this.copy=()=>this.#id} }');
let keep=[];for(let i=0;i<30;i++){let C=make(i);let c=new C();keep.push([C,c,c.copy,c.get]);}
for(let i=0;i<keep.length;i++){let r=keep[i];check(r[2]()===i&&r[3].call(r[1])===i&&r[1].has(r[1]));if(i)check(!r[1].has(keep[i-1][1]));}
true;
    )JS");
}

TEST_F(JsMvpLmd, RegExpReplaceSymbolAndStringHandoff) {
    boolean("let seen='',s={toString(){seen+='s';return 'abc'}},p={[Symbol.replace](v,r){return v===s&&r===9&&seen===''}};String.prototype.replace.call(s,p,9)");
    boolean("let p={[Symbol.replace](){return 27}};'x'.replace(p,'y')===27");
    boolean("let token={},caught=false;try{'x'.replace({get [Symbol.replace](){throw token}},'y')}catch(e){caught=e===token}caught");
    boolean("'aab'.replace(/a/g,'x')==='xxb'&&'abc'.replace('b','x')==='axc'&&'ab'.replace('', '-')==='-ab'");
    boolean("let r=/a/g,n=0;r.exec=function(){return n++<2?{0:'a',length:1,index:n-1}:null};r[Symbol.replace]('aa',x=>x.toUpperCase())==='AA'&&n===3");
    error("RegExp.prototype[Symbol.replace].call({exec(){return 1}},'x',()=> 'a')", "TypeError");
}
TEST_F(JsMvpLmd, RegExpReplaceCaptureOwnershipAndSubstitution) {
    boolean("let r={flags:'',global:false,exec(){return {0:'x',1:null,2:undefined,length:3,index:0}}};RegExp.prototype[Symbol.replace].call(r,'x','$1:$2')==='null:'");
    boolean("let r={flags:'',global:false,exec(){return {0:'x',1:null,length:2,index:0,groups:{n:4}}}};RegExp.prototype[Symbol.replace].call(r,'x','$<n>-$1')==='4-null'");
    boolean("let match={0:'x',length:71,index:0};for(let i=1;i<71;i++)match[i]='c'+i;let r={global:false,exec(){return match}},seen=0;RegExp.prototype[Symbol.replace].call(r,'x',function(){seen=arguments.length;return arguments[70]})==='c70'&&seen===73");
    boolean("'\\uD83D\\uDE00a!'.replace(/a/,\"$`|$&|$'\")==='\\uD83D\\uDE00\\uD83D\\uDE00|a|!!'");
    boolean("let r={global:false,exec(){return {0:'long',length:1,index:Infinity}}};RegExp.prototype[Symbol.replace].call(r,'é',()=> '!')==='é!'");
    boolean("let seen='',r={global:true,unicode:true,lastIndex:0,exec(){seen+=this.lastIndex;if(this.lastIndex>1)return null;return {0:'',length:1,index:this.lastIndex}}};RegExp.prototype[Symbol.replace].call(r,'\\uD83D\\uDE00',()=> '-')==='-\\uD83D\\uDE00'&&seen==='02'");
}
TEST_F(JsMvpLmd, RegExpReplaceAbruptOrderAndStrictLastIndex) {
    boolean("let token={},caught=false,r={global:false,exec(){return {0:'x',length:2,index:0,get 1(){throw token}}}};try{RegExp.prototype[Symbol.replace].call(r,'x',()=> 'z')}catch(e){caught=e===token}caught");
    boolean("let token={},caught=false,r={flags:'',global:false,exec(){return {0:'x',length:1,index:0,groups:{get name(){throw token}}}}};try{RegExp.prototype[Symbol.replace].call(r,'x','$<name>')}catch(e){caught=e===token}caught");
    error("let r=/x/g;Object.defineProperty(r,'lastIndex',{writable:false});r[Symbol.replace]('x','a')", "TypeError");
    boolean("let r=/a/g,n=0;r.exec=function(){return n++?null:['a']};r[Symbol.replace]('a',function(){return 'b'})==='b'&&n===2");
}
TEST_F(JsMvpLmd, RegExpSplitSpeciesAndStringHandoff) {
    boolean("'a,b,c'.split(/,/).join('|')==='a|b|c'&&'a1b2c'.split(/(\\d)/).join('|')==='a|1|b|2|c'");
    boolean("let receiver={},p={[Symbol.split](s,l){return s===receiver&&l===7}};String.prototype.split.call(receiver,p,7)");
    boolean("let seen='',splitter={lastIndex:0,exec(s){seen+=this.lastIndex;return null}},r={flags:'u',constructor:{[Symbol.species]:function(pattern,flags){seen+=pattern===r&&flags==='uy'?'c':'bad';return splitter}}};let a=RegExp.prototype[Symbol.split].call(r,'ab');a.length===1&&a[0]==='ab'&&seen==='c01'");
    boolean("let descriptor=Object.getOwnPropertyDescriptor(RegExp,Symbol.species);descriptor.get.call(Array)===Array&&!descriptor.enumerable&&descriptor.configurable&&descriptor.set===undefined");
    error("RegExp.prototype[Symbol.split].call({constructor:{[Symbol.species]:()=> ({})}},'x')", "TypeError");
}
TEST_F(JsMvpLmd, RegExpSplitUtf16LimitAndObservableExec) {
    boolean("'a,b'.split(/,/,Infinity).length===0&&'a,b'.split(/,/,-Infinity).length===0");
    boolean("'\\uD83D\\uDE00'.split(/(?:)/).length===2&&'\\uD83D\\uDE00'.split(/(?:)/u).length===1");
    boolean("let seen='',splitter={lastIndex:0,exec(){seen+=this.lastIndex;return null}},r={flags:'',constructor:{[Symbol.species]:function(){return splitter}}};let a=RegExp.prototype[Symbol.split].call(r,'\\uD83D\\uDE00');seen==='01'&&a[0]==='\\uD83D\\uDE00'");
    boolean("let splitter={lastIndex:0,exec(){if(this.lastIndex!==1)return null;this.lastIndex=2;return {0:'b',1:null,2:undefined,length:3}}},r={flags:'',constructor:{[Symbol.species]:function(){return splitter}}};let a=RegExp.prototype[Symbol.split].call(r,'abc');a.length===4&&a[0]==='a'&&a[1]===null&&a[2]===undefined&&a[3]==='c'");
    error("let splitter={exec(){return 1}},r={flags:'',constructor:{[Symbol.species]:function(){return splitter}}};RegExp.prototype[Symbol.split].call(r,'x')", "TypeError");
    boolean("let token={},caught=false,splitter={set lastIndex(v){throw token},exec(){throw 99}},r={flags:'',constructor:{[Symbol.species]:function(){return splitter}}};try{RegExp.prototype[Symbol.split].call(r,'x')}catch(e){caught=e===token}caught");
}

TEST_F(JsMvpLmd, DynamicClassFieldThunkIdentity) {
    boolean(R"JS(
function check(value){if(!value)throw new Error('field thunk identity');}
let C=Function('return class {field=()=>this;nested=()=>()=>[this,new.target];normal=function(){return 7};target=function(){return new.target};static field=()=>this;static nested=()=>()=>[this,new.target]}')();
let c=new C();check(c.field()===c&&C.field()===C&&c.normal()===7);
let inner=c.nested(),staticInner=C.nested();let a=inner(),b=staticInner();
check(a[0]===c&&a[1]===undefined&&b[0]===C&&b[1]===undefined);
let Target=c.target;check(Target()===undefined&&new Target()===Target);
true;
    )JS");
}

TEST_F(JsMvpLmd, ModulesShareLiveSlabsAndRealm) {
    create_dir("temp/mvp_modules");
    write_text_file("temp/mvp_modules/live.js", "export let count=1;export function add(){count++}export const ArrayCtor=Array;export const object={x:7};");
    module("import {count,add,ArrayCtor,object} from './live.js';if(count!==1||ArrayCtor!==Array||Object.getPrototypeOf(object)!==Object.prototype)throw 1;add();if(count!==2||object.x!==7)throw 2;", "temp/mvp_modules/live-main.js");
}
TEST_F(JsMvpLmd, ModuleDefaultSnapshotAndHoistedIdentity) {
    create_dir("temp/mvp_modules");
    write_text_file("temp/mvp_modules/default-value.js", "export let x=1;export default x;x=2;");
    write_text_file("temp/mvp_modules/default-function.js", "export default function(){return 9}");
    module("import snapshot,{x} from './default-value.js';import f from './default-function.js';if(snapshot!==1||x!==2||f()!==9||f.name!=='default')throw 1;", "temp/mvp_modules/default-main.js");
}
TEST_F(JsMvpLmd, ModuleCycleHoistsFunctionsBeforeBodies) {
    create_dir("temp/mvp_modules");
    write_text_file("temp/mvp_modules/cycle-a.js", "import {seen} from './cycle-b.js';export function value(){return 7}export var n=9;export const result=seen;");
    write_text_file("temp/mvp_modules/cycle-b.js", "import {value,n} from './cycle-a.js';export const seen=value()===7&&n===undefined;");
    module("import {result,n} from './cycle-a.js';if(!result||n!==9)throw 1;", "temp/mvp_modules/cycle-main.js");
}
TEST_F(JsMvpLmd, ModuleStarDiamondResolvesDefiningCell) {
    create_dir("temp/mvp_modules");
    write_text_file("temp/mvp_modules/diamond-origin.js", "export let value=3;export function set(){value=4}");
    write_text_file("temp/mvp_modules/diamond-left.js", "export * from './diamond-origin.js'");
    write_text_file("temp/mvp_modules/diamond-right.js", "export {value,set} from './diamond-origin.js'");
    write_text_file("temp/mvp_modules/diamond-top.js", "export * from './diamond-left.js';export * from './diamond-right.js'");
    module("import {value,set} from './diamond-top.js';if(value!==3)throw 1;set();if(value!==4)throw 2;", "temp/mvp_modules/diamond-main.js");
}
TEST_F(JsMvpLmd, ModuleLinkFailurePrecedesAllBodies) {
    create_dir("temp/mvp_modules");
    write_text_file("temp/mvp_modules/link-side.js", "throw 73;export const value=1;");
    MvpLmdFailurePhase phase = MVP_LMD_SUCCESS;
    Item result = module("import './link-side.js';import {missing} from './link-side.js';", "temp/mvp_modules/link-main.js", &phase);
    EXPECT_TRUE(item_is_error(result)); EXPECT_EQ(phase, MVP_LMD_RESOLUTION_FAILURE);
    const char* kind = NULL;
    mvp_lmd_diagnostic(execution, NULL, &kind); EXPECT_STREQ(kind, "SyntaxError");
}
TEST_F(JsMvpLmd, ModuleAmbiguousStarImportIsLinkError) {
    create_dir("temp/mvp_modules");
    write_text_file("temp/mvp_modules/ambiguous-a.js", "export const value=1");
    write_text_file("temp/mvp_modules/ambiguous-b.js", "export const value=1");
    write_text_file("temp/mvp_modules/ambiguous-top.js", "export * from './ambiguous-a.js';export * from './ambiguous-b.js'");
    MvpLmdFailurePhase phase = MVP_LMD_SUCCESS;
    EXPECT_TRUE(item_is_error(module("import {value} from './ambiguous-top.js';", "temp/mvp_modules/ambiguous-main.js", &phase)));
    EXPECT_EQ(phase, MVP_LMD_RESOLUTION_FAILURE);
}
TEST_F(JsMvpLmd, ModuleLexicalsStayPrivateAndStrict) {
    module("var hidden=1;let NaN=3;const arrow=()=>this;if(this!==undefined||arrow()!==undefined||NaN!==3||'hidden' in globalThis)throw 1;let strict=false;try{undeclared=1}catch(e){strict=e instanceof ReferenceError}if(!strict)throw 2;", "temp/mvp-module-isolation.js");
}
TEST_F(JsMvpLmd, ModuleImportsRetainScalarAndAssignmentSemantics) {
    create_dir("temp/mvp_modules");
    write_text_file("temp/mvp_modules/scalar.js", "export let tiny=5e-324;export function change(){tiny=7;return 0}");
    module("import {tiny,change} from './scalar.js';let before=tiny+change();if(before!==5e-324||tiny!==7)throw 1;let denied=false;try{tiny=2}catch(e){denied=e instanceof TypeError}if(!denied||tiny!==7)throw 2;", "temp/mvp_modules/scalar-main.js");
}

TEST_F(JsMvpLmd, ModuleDefaultDeclarationsRetainLocalIdentity) {
    create_dir("temp/mvp_modules");
    write_text_file("temp/mvp_modules/default-named-class.js", "export default class Named{};export const original=Named;Named=8;");
    write_text_file("temp/mvp_modules/default-anonymous-class.js", "export default class{};");
    write_text_file("temp/mvp_modules/default-named-function.js", "export default function named(){return 1};named=7;");
    write_text_file("temp/mvp_modules/default-expression-function.js", "function named(){return 2}export default named;named=9;");
    module("import Changed,{original} from './default-named-class.js';import Anonymous from './default-anonymous-class.js';import value from './default-named-function.js';import snapshot from './default-expression-function.js';if(Changed!==8||original.name!=='Named'||Anonymous.name!=='default'||value!==7||snapshot()!==2)throw 1;", "temp/mvp_modules/default-declarations-main.js");
}

TEST_F(JsMvpLmd, RegExpMatchAllSharedIterationAndClone) {
    boolean("let r=/a/g;r.lastIndex=1;let i='aba'.matchAll(r),a=i.next(),b=i.next();a.value[0]==='a'&&a.value.index===2&&!a.done&&b.done&&i.next().done&&r.lastIndex===1");
    boolean("let i='aba'.matchAll('a');i.next().value.index===0&&i.next().value.index===2&&i.next().done");
    boolean("let i=RegExp.prototype[Symbol.matchAll].call(/a/,'aba');!i.next().done&&i.next().done&&i.next().done");
    boolean("let i=''.matchAll();!i.next().done&&i.next().done");
    error("'x'.matchAll(/x/)", "TypeError");
}
TEST_F(JsMvpLmd, RegExpMatchAllStringProtocolOrder) {
    boolean("let seen='',receiver={toString(){seen+='s';return 'x'}},p={[Symbol.matchAll](s){return s===receiver&&seen===''}};String.prototype.matchAll.call(receiver,p)");
    boolean("let seen='',receiver={},p={[Symbol.match]:true,get flags(){seen+='f';return {toString(){seen+='t';return 'g'}}},get [Symbol.matchAll](){seen+='m';return function(s){return s===receiver&&seen==='ftm'}}};String.prototype.matchAll.call(receiver,p)");
    boolean("let token={},caught=false;try{'x'.matchAll({get [Symbol.matchAll](){throw token}})}catch(e){caught=e===token}caught");
    boolean("let i='a3b'.matchAll(3);i.next().value[0]==='3'&&i.next().done");
    boolean("'x'.matchAll({[Symbol.matchAll](){return 17}})===17");
    error("String.prototype.matchAll.call(null,{[Symbol.matchAll](){return 1}})", "TypeError");
}
TEST_F(JsMvpLmd, RegExpMatchAllSpeciesAndLastIndexOrder) {
    boolean("let seen='',matcher={set lastIndex(v){seen+='i'+v},exec(){return null}},r={get constructor(){seen+='c';return {get [Symbol.species](){seen+='p';return function(a,b){seen+=a===r&&b==='g'?'n':'bad';return matcher}}}},get flags(){seen+='f';return 'g'},get lastIndex(){seen+='l';return {valueOf(){seen+='v';return -2}}}},s={toString(){seen+='s';return 'x'}};let i=RegExp.prototype[Symbol.matchAll].call(r,s);seen==='scpfnlvi0'&&i.next().done");
    boolean("let value=-1,matcher={set lastIndex(v){value=v},exec(){return null}},r={flags:'g',lastIndex:Infinity,constructor:{[Symbol.species]:function(){return matcher}}};RegExp.prototype[Symbol.matchAll].call(r,'');value===9007199254740991");
    boolean("let token={},caught=false,matcher={set lastIndex(v){throw token}},r={flags:'g',lastIndex:0,constructor:{[Symbol.species]:function(){return matcher}}};try{RegExp.prototype[Symbol.matchAll].call(r,'x')}catch(e){caught=e===token}caught");
    error("let matcher=Object.freeze({lastIndex:0}),r={flags:'g',lastIndex:0,constructor:{[Symbol.species]:function(){return matcher}}};RegExp.prototype[Symbol.matchAll].call(r,'x')", "TypeError");
}
TEST_F(JsMvpLmd, RegExpMatchAllObservableExecAndUnicodeAdvance) {
    boolean("let n=0,record={0:'x'},matcher={lastIndex:0,exec(s){return n++?null:record}},r={flags:'g',lastIndex:0,constructor:{[Symbol.species]:function(){return matcher}}};let i=RegExp.prototype[Symbol.matchAll].call(r,'x');i.next().value===record&&i.next().done&&i.next().done&&n===2");
    boolean("let n=0,matcher={lastIndex:0,exec(){return n++?null:{0:''}}},r={flags:'gu',lastIndex:0,constructor:{[Symbol.species]:function(){return matcher}}};let i=RegExp.prototype[Symbol.matchAll].call(r,'\\uD83D\\uDE00');!i.next().done&&matcher.lastIndex===2&&i.next().done");
    boolean("let record={get 0(){throw 1}},matcher={lastIndex:0,exec(){return record}},r={flags:'',lastIndex:0,constructor:{[Symbol.species]:function(){return matcher}}};let i=RegExp.prototype[Symbol.matchAll].call(r,'x');i.next().value===record&&i.next().done");
    boolean("let token={},caught=false,matcher={lastIndex:0,exec(){throw token}},r={flags:'g',lastIndex:0,constructor:{[Symbol.species]:function(){return matcher}}};let i=RegExp.prototype[Symbol.matchAll].call(r,'x');try{i.next()}catch(e){caught=e===token}caught");
    error("let matcher={lastIndex:0,exec(){return 1}},r={flags:'g',lastIndex:0,constructor:{[Symbol.species]:function(){return matcher}}};RegExp.prototype[Symbol.matchAll].call(r,'x').next()", "TypeError");
    error("let matcher={lastIndex:0,exec(){Object.defineProperty(this,'lastIndex',{writable:false});return {0:''}}},r={flags:'g',lastIndex:0,constructor:{[Symbol.species]:function(){return matcher}}};RegExp.prototype[Symbol.matchAll].call(r,'x').next()", "TypeError");
}
TEST_F(JsMvpLmd, RegExpMatchAllIteratorMetadataBrandAndRetention) {
    boolean("let i='x'.matchAll(/x/g),p=Object.getPrototypeOf(i),d=Object.getOwnPropertyDescriptor(p,'next'),tag=Object.getOwnPropertyDescriptor(p,Symbol.toStringTag);i[Symbol.iterator]()===i&&p.next.name==='next'&&p.next.length===0&&d.writable&&!d.enumerable&&d.configurable&&tag.value==='RegExp String Iterator'&&!tag.writable&&!tag.enumerable&&tag.configurable&&Reflect.ownKeys(i).length===0");
    boolean("let i='x'.matchAll(/x/g),next=i.next,p=Object.getPrototypeOf(i),ok=0;try{next.call(Object.create(p))}catch(e){ok+=e instanceof TypeError}try{next.call(new Proxy(i,{}))}catch(e){ok+=e instanceof TypeError}Object.setPrototypeOf(i,null);ok===2&&next.call(i).value[0]==='x'&&next.call(i).done");
    boolean("let i='ab'.matchAll(/./g);Object.freeze(i);let junk=[];for(let n=0;n<200;n++)junk.push({v:'retained-'+n});i.next().value[0]==='a'&&i.next().value[0]==='b'&&i.next().done&&i.next().done");
}

TEST_F(JsMvpLmd, SharedIteratorPrototypeAncestryAndMetadata) {
    boolean("let base=Object.getPrototypeOf(Object.getPrototypeOf([][Symbol.iterator]())),m=Object.getPrototypeOf(new Map().keys()),s=Object.getPrototypeOf(new Set().values()),r=Object.getPrototypeOf(/./g[Symbol.matchAll]('x'));base!==Object.prototype&&Object.getPrototypeOf(base)===Object.prototype&&Object.getPrototypeOf(m)===base&&Object.getPrototypeOf(s)===base&&Object.getPrototypeOf(r)===base");
    boolean("let base=Object.getPrototypeOf(Object.getPrototypeOf([][Symbol.iterator]())),d=Object.getOwnPropertyDescriptor(base,Symbol.iterator),o={};d.value.call(o)===o&&d.value.name==='[Symbol.iterator]'&&d.value.length===0&&d.writable&&!d.enumerable&&d.configurable&&!Object.hasOwn(base,'next')");
    boolean("let a=[].values(),m=new Map().values(),r='x'.matchAll(/x/g),base=Object.getPrototypeOf(Object.getPrototypeOf(a)),i=base[Symbol.iterator];a[Symbol.iterator]===i&&m[Symbol.iterator]===i&&r[Symbol.iterator]===i&&i.call(undefined)===undefined&&i.call(null)===null&&i.call(3)===3");
}
TEST_F(JsMvpLmd, GeneratorAndSequenceShareIteratorPrototype) {
    boolean("function* g(){yield 1}let base=Object.getPrototypeOf(Object.getPrototypeOf([][Symbol.iterator]())),generatorBase=Object.getPrototypeOf(Object.getPrototypeOf(g.prototype));base===generatorBase&&g()[Symbol.iterator]===base[Symbol.iterator]");
    boolean("function* g(){yield 1}let base=Object.getPrototypeOf(Object.getPrototypeOf([][Symbol.iterator]())),i=g();base.extra=27;i.extra===27&&new Map().keys().extra===27&&'x'.matchAll(/x/g).extra===27");
}

TEST_F(JsMvpLmd, RegExpReplaceLargeLastIndexAndNullGroups) {
    boolean("let r=/./g,n=0;r.exec=function(){if(n++)return null;this.lastIndex={valueOf(){return Math.pow(2,54)}};return {0:'',length:1,index:0}};r[Symbol.replace]('','')===''&&r.lastIndex===Math.pow(2,53)");
    error("let r=/./;r.exec=function(){return {0:'',length:1,index:0,groups:null}};r[Symbol.replace]('bar','')", "TypeError");
    boolean("let r=/./,seen=false;r.exec=function(){return {0:'',length:1,index:0,groups:null}};r[Symbol.replace]('bar',function(m,i,s,g){seen=g===null;return 'x'})==='xbar'&&seen");
    boolean("let matcher={lastIndex:0,exec(){this.lastIndex=Math.pow(2,54);return {0:''}}},r={flags:'g',lastIndex:0,constructor:{[Symbol.species]:function(){return matcher}}};let i=RegExp.prototype[Symbol.matchAll].call(r,'');!i.next().done&&matcher.lastIndex===Math.pow(2,53)");
}

TEST_F(JsMvpLmd, ModuleNamespacesKeepIdentityAndLiveExports) {
    create_dir("temp/mvp_modules");
    write_text_file("temp/mvp_modules/ns-origin.js", "export let x=1;export function change(){x=2};export default 9;");
    write_text_file("temp/mvp_modules/ns-reexport.js", "export * as named from './ns-origin.js';export * from './ns-origin.js';");
    module("import * as a from './ns-origin.js';import * as b from './ns-origin.js';import {named,change} from './ns-reexport.js';if(a!==b||a!==named||a.default!==9||a.x!==1)throw 1;change();if(a.x!==2||typeof a!=='object'||Object(a)!==a)throw 2;", "temp/mvp_modules/ns-main.js");
}
TEST_F(JsMvpLmd, ModuleNamespaceKeysUseLexicalOrderAndTag) {
    create_dir("temp/mvp_modules");
    write_text_file("temp/mvp_modules/ns-keys.js", "const x=1;export {x as '2',x as '10',x as 'z',x as 'A'};");
    module("import * as n from './ns-keys.js';let names=Object.getOwnPropertyNames(n);if(names.join(',')!=='10,2,A,z'||Object.keys(n).join(',')!==names.join(','))throw 1;let keys=Reflect.ownKeys(n);if(keys.length!==5||keys[4]!==Symbol.toStringTag||n[Symbol.toStringTag]!=='Module')throw 2;let d=Object.getOwnPropertyDescriptor(n,Symbol.toStringTag);if(d.value!=='Module'||d.writable||d.enumerable||d.configurable)throw 3;", "temp/mvp_modules/ns-keys-main.js");
}
TEST_F(JsMvpLmd, ModuleNamespaceDescriptorsAndExoticSet) {
    create_dir("temp/mvp_modules");
    write_text_file("temp/mvp_modules/ns-descriptor.js", "export let value=1;");
    module("import * as n from './ns-descriptor.js';let d=Object.getOwnPropertyDescriptor(n,'value');if(d.value!==1||!d.writable||!d.enumerable||d.configurable)throw 1;if(!Reflect.defineProperty(n,'value',{value:1})||Reflect.defineProperty(n,'value',{value:2})||Reflect.defineProperty(n,'value',{writable:false})||Reflect.defineProperty(n,'value',{get:function(){return 1}})||Reflect.defineProperty(n,'missing',{}))throw 2;let child=Object.create(n),receiver={};if(child.value!==1||Reflect.set(n,'value',2,receiver)||Reflect.set(child,'value',2)||Reflect.set(child,'missing',2)||Object.hasOwn(child,'value')||Object.hasOwn(receiver,'value'))throw 3;if(Reflect.deleteProperty(n,'value')||!Reflect.deleteProperty(n,'missing'))throw 4;", "temp/mvp_modules/ns-descriptor-main.js");
}
TEST_F(JsMvpLmd, ModuleNamespaceIntegrityAndNullPrototype) {
    create_dir("temp/mvp_modules");
    write_text_file("temp/mvp_modules/ns-integrity.js", "export const x=1;");
    write_text_file("temp/mvp_modules/ns-empty.js", "export {};");
    module("import * as n from './ns-integrity.js';import * as empty from './ns-empty.js';if(Object.getPrototypeOf(n)!==null||Object.isExtensible(n)||!Object.isSealed(n)||Object.isFrozen(n)||!Object.isFrozen(empty))throw 1;if(!Reflect.preventExtensions(n)||!Reflect.setPrototypeOf(n,null)||Reflect.setPrototypeOf(n,Object.prototype)||Object.seal(n)!==n)throw 2;let denied=false;try{Object.freeze(n)}catch(e){denied=e instanceof TypeError}if(!denied||Object.prototype.toString.call(n)!=='[object Module]')throw 3;", "temp/mvp_modules/ns-integrity-main.js");
}
TEST_F(JsMvpLmd, ModuleNamespaceCycleKeepsTdzReflectionOrder) {
    create_dir("temp/mvp_modules");
    write_text_file("temp/mvp_modules/ns-cycle-a.js", "import './ns-cycle-b.js';export let x=1;");
    write_text_file("temp/mvp_modules/ns-cycle-b.js", "import * as n from './ns-cycle-a.js';let failures=0;if(!('x' in n)||Object.getOwnPropertyNames(n).join(',')!=='x'||Reflect.deleteProperty(n,'x'))throw 1;try{n.x}catch(e){if(e instanceof ReferenceError)failures++}try{Object.getOwnPropertyDescriptor(n,'x')}catch(e){if(e instanceof ReferenceError)failures++}try{Object.prototype.hasOwnProperty.call(n,'x')}catch(e){if(e instanceof ReferenceError)failures++}try{Object.keys(n)}catch(e){if(e instanceof ReferenceError)failures++}globalThis.namespaceTdzCount=failures;");
    module("import * as n from './ns-cycle-a.js';if(globalThis.namespaceTdzCount!==4||n.x!==1)throw 1;", "temp/mvp_modules/ns-cycle-main.js");
}
TEST_F(JsMvpLmd, ModuleNamespaceOmitsAmbiguousStars) {
    create_dir("temp/mvp_modules");
    write_text_file("temp/mvp_modules/ns-ambiguous-a.js", "export const duplicate=1;export const left=2;");
    write_text_file("temp/mvp_modules/ns-ambiguous-b.js", "export const duplicate=1;export const right=3;");
    write_text_file("temp/mvp_modules/ns-ambiguous.js", "export * from './ns-ambiguous-a.js';export * from './ns-ambiguous-b.js';");
    module("import * as n from './ns-ambiguous.js';if('duplicate' in n||Object.getOwnPropertyNames(n).join(',')!=='left,right'||n.left!==2||n.right!==3)throw 1;", "temp/mvp_modules/ns-ambiguous-main.js");
}
TEST_F(JsMvpLmd, ModuleNamespaceProxyUsesSharedInvariants) {
    create_dir("temp/mvp_modules");
    write_text_file("temp/mvp_modules/ns-proxy.js", "export const x=1;");
    module("import * as n from './ns-proxy.js';let p=new Proxy(n,{});if(p.x!==1||Object.getPrototypeOf(p)!==null||Object.isExtensible(p)||Reflect.ownKeys(p).length!==2)throw 1;let failed=false;try{Reflect.ownKeys(new Proxy(n,{ownKeys(){return []}}))}catch(e){failed=e instanceof TypeError}if(!failed)throw 2;", "temp/mvp_modules/ns-proxy-main.js");
}
TEST_F(JsMvpLmd, ModuleImportMetaIsCachedPerDefiningModule) {
    create_dir("temp/mvp_modules");
    write_text_file("temp/mvp_modules/meta-other.js", "export const meta=import.meta;export function read(){return import.meta};");
    module("import {meta,read} from './meta-other.js';let own=import.meta;own.marker=7;if(own!==import.meta||own.marker!==7||own===meta||read()!==meta||Object.getPrototypeOf(own)!==null||typeof own.url!=='string'||typeof meta.url!=='string'||own.url===meta.url)throw 1;let d=Object.getOwnPropertyDescriptor(own,'url');if(!d.writable||!d.enumerable||!d.configurable)throw 2;", "temp/mvp_modules/meta-main.js");
}
TEST_F(JsMvpLmd, ModuleNamespaceCopiesAndSerializesLiveValues) {
    create_dir("temp/mvp_modules");
    write_text_file("temp/mvp_modules/ns-copy.js", "export const a=1;export const b=2;");
    module("import * as n from './ns-copy.js';let a=Object.assign({},n),b={...n};if(a.a!==1||a.b!==2||b.a!==1||b.b!==2||JSON.stringify(n)!=='{\"a\":1,\"b\":2}')throw 1;", "temp/mvp_modules/ns-copy-main.js");
}

TEST_F(JsMvpLmd, StringRegExpProtocolsIgnorePrimitivePrototypeHooks) {
    boolean("let seen=0;for(let name of ['match','search','replace','split'])Object.defineProperty(Number.prototype,Symbol[name],{get(){seen++;throw 9}});let value=1,source='a1b1c',match=source.match(value);match.index===1&&match[0]==='1'&&source.search(value)===1&&source.replace(value,'X')==='aXb1c'&&source.split(value).join('|')==='a|b|c'&&seen===0");
    boolean("let seen=0;for(let name of ['match','search','replace','split'])Object.defineProperty(BigInt.prototype,Symbol[name],{get(){seen++;throw 9}});let value=1n,source='a1b1c',match=source.match(value);match.index===1&&match[0]==='1'&&source.search(value)===1&&source.replace(value,'X')==='aXb1c'&&source.split(value).join('|')==='a|b|c'&&seen===0");
    boolean("let seen=0;for(let name of ['match','search','replace','split'])Object.defineProperty(Boolean.prototype,Symbol[name],{get(){seen++;throw 9}});let value=true,source='atruebtruec',match=source.match(value);match.index===1&&match[0]==='true'&&source.search(value)===1&&source.replace(value,'X')==='aXbtruec'&&source.split(value).join('|')==='a|b|c'&&seen===0");
    boolean("let seen=0;for(let name of ['match','search','replace','split'])Object.defineProperty(String.prototype,Symbol[name],{get(){seen++;throw 9}});let value=',',source='a,b,c',match=source.match(value);match.index===1&&match[0]===','&&source.search(value)===1&&source.replace(value,'X')==='aXb,c'&&source.split(value).join('|')==='a|b|c'&&seen===0");
    boolean("let seen='',receiver={toString(){seen+='s';throw 9}},pattern={get [Symbol.match](){seen+='m';return function(value){return value===receiver&&this===pattern}}};String.prototype.match.call(receiver,pattern)&&seen==='m'");
}

TEST_F(JsMvpLmd, MirDirectEvalLiveCells) {
    boolean(R"JS(
function check(x){if(!x)throw new Error('MIR eval live cells');}
function run(a){let x=1;function read(){return x+a}function write(v){x=v}
let before=read;let result=eval('x=4;a=7;write(6);[read(),()=>x+a]');
check(result[0]===13&&before()===13);x=8;return [result[1],before]}
let pair=run(2);check(pair[0]()===15&&pair[1]()===15);true;
    )JS");
}

TEST_F(JsMvpLmd, MirDirectEvalLateVarsAndClosures) {
    boolean(R"JS(
function check(x){if(!x)throw new Error('MIR eval retained vars');}
function make(seed){function read(){return created+seed}let before=read;
eval('var created=3');let interpreted=eval('(function(v){created=v;return created+seed})');
return [before,interpreted,function(v){created=v;return read()}]}
let a=make(1),b=make(10);check(a[0]()===4&&b[0]()===13);check(a[1](8)===9&&a[0]()===9&&b[0]()===13);check(a[2](11)===12);
Object.defineProperty(a[0],'extra',{value:5});check(a[0].extra===5&&a[0]()===12);true;
    )JS");
}

TEST_F(JsMvpLmd, MirDirectEvalStrictAndLexicals) {
    boolean(R"JS(
function check(x){if(!x)throw new Error('MIR eval lexical rules');}
function strict(){'use strict';let x=1;eval('var hidden=3;x=4');return [x,typeof hidden,eval('this')]}
let s=strict();check(s[0]===4&&s[1]==='undefined'&&s[2]===undefined);
function sloppy(){let x=2;const c=3;let types=0;try{eval('c=4')}catch(e){types+=e instanceof TypeError}try{eval('var x=9')}catch(e){types+=e instanceof SyntaxError}
{try{eval('typeof later')}catch(e){types+=e instanceof ReferenceError}let later=7;check(eval('later')===7)}
eval('var gone=5');check(delete gone&&typeof gone==='undefined');return x===2&&types===3&&eval('delete c')===false}
check(sloppy());true;
    )JS");
}

TEST_F(JsMvpLmd, MirDirectEvalReferenceBeforeRhs) {
    boolean(R"JS(
let mvpBeforeRhs=1;
function run(){mvpBeforeRhs=eval('var mvpBeforeRhs=3;2');return mvpBeforeRhs}
run()===3&&mvpBeforeRhs===2;
    )JS");
}

TEST_F(JsMvpLmd, MirDirectEvalArgumentsCells) {
    boolean(R"JS(
function check(x){if(!x)throw new Error('MIR eval arguments');}
function mapped(a){let original=arguments;eval('a=5');check(original[0]===5);eval('arguments[0]=7');check(a===7);delete original[0];eval('a=9');check(original[0]===undefined&&a===9);eval('arguments=11');return arguments===11}
function duplicate(a,a){eval('a=6');return arguments[0]===1&&arguments[1]===6&&a===6}
function strict(a){'use strict';eval('a=8');return arguments[0]===2&&a===8}
check(mapped(1)&&duplicate(1,2)&&strict(2));true;
    )JS");
}

TEST_F(JsMvpLmd, MirDirectEvalReceiverAndNewTarget) {
    boolean(R"JS(
function check(x){if(!x)throw new Error('MIR eval receiver');}
function F(){this.result=eval('[this,new.target,()=>[this,new.target]]');this.get=()=>eval('[this,new.target]')}
let f=new F();check(f.result[0]===f&&f.result[1]===F&&f.result[2]()[0]===f&&f.result[2]()[1]===F&&f.get()[0]===f&&f.get()[1]===F);
let arrow=()=>eval('this');check(arrow()===globalThis);
function plain(){return eval('this')}check(plain.call({tag:2}).tag===2);true;
    )JS");
}

TEST_F(JsMvpLmd, MirDirectEvalLoopsAndCatch) {
    boolean(R"JS(
function check(x){if(!x)throw new Error('MIR eval loop environments');}
function run(){let reads=[];for(let i=0;i<3;i++){eval('i');reads.push(eval('()=>i'));reads.push(()=>i)}
for(let v of [4,5]){reads.push(()=>eval('v'));reads.push(eval('()=>v'))}
let outer=10;try{throw 6}catch(outer){eval('outer=7');reads.push(()=>outer)}
check(outer===10);return reads}
let r=run();check(r[0]()===0&&r[1]()===0&&r[2]()===1&&r[3]()===1&&r[4]()===2&&r[5]()===2&&r[6]()===4&&r[7]()===4&&r[8]()===5&&r[9]()===5&&r[10]()===7);true;
    )JS");
}

TEST_F(JsMvpLmd, MirDirectEvalRecursionAndAbrupt) {
    boolean(R"JS(
function check(x){if(!x)throw new Error('MIR eval recursion');}
let token={};function rec(n){let x=n;let read=()=>x;if(n){eval('x=rec(n-1)[0]+10')}return [x,read]}
let r=rec(3);check(r[0]===30&&r[1]()===30);
function fail(){let x=1;let read=()=>x;try{eval('x=9;throw token')}catch(e){check(e===token&&read()===9)}finally{check(eval('x')===9)}return read}
check(fail()()===9);true;
    )JS");
}

TEST_F(JsMvpLmd, MirDirectEvalIdentityAndCallOrder) {
    boolean(R"JS(
function check(x){if(!x)throw new Error('MIR eval call order');}
let order='';function shadow(eval){let local=4;return eval('local',order+='a',order+='b')}
check(shadow(function(s,a,b){return s==='local'&&a==='a'&&b==='ab'})&&order==='ab');
function direct(){let local=5;let r=eval('local',order+='c',...[order+='d']);let alias=eval;return r===5&&alias('typeof local')==='undefined'&&eval?.('typeof local')==='undefined'}
check(direct()&&order==='abcd');function nonstring(){let o={};return eval(o)===o&&eval()===undefined}check(nonstring());true;
    )JS");
}

TEST_F(JsMvpLmd, MirDirectEvalGlobalCellsAndDeclarations) {
    boolean(R"JS(
function check(x){if(!x)throw new Error('MIR eval global cells');}
let mvpEvalGlobalLexical=1;var mvpEvalGlobalVar=2;
function read(){return mvpEvalGlobalLexical+mvpEvalGlobalVar}
eval('mvpEvalGlobalLexical=3;mvpEvalGlobalVar=4;var mvpEvalCreated=5');
check(read()===7&&mvpEvalCreated===5&&globalThis.mvpEvalGlobalVar===4&&!Object.hasOwn(globalThis,'mvpEvalGlobalLexical'));
check((0,eval)('mvpEvalGlobalLexical')===3);(0,eval)('mvpEvalGlobalLexical=6');check(mvpEvalGlobalLexical===6&&read()===10);
check(delete mvpEvalCreated&&typeof mvpEvalCreated==='undefined');true;
    )JS");
}

TEST_F(JsMvpLmd, MirDirectEvalWideScalarOwnership) {
    boolean(R"JS(
function check(x){if(!x)throw new Error('MIR eval wide scalars');}
function run(){let x=5e-324;function change(){x=1e-323;let junk=[];for(let i=0;i<15;i++)junk.push({v:i});return 0}
let sum=eval('x+change()');check(sum===5e-324&&x===1e-323);let read=eval('()=>x');eval('x=-0');check(1/read()===-Infinity);eval('x=9007199254740991');check(read()===9007199254740991);return read}
check(run()()===9007199254740991);true;
    )JS");
}

TEST_F(JsMvpLmd, MirDirectEvalFunctionNameAndAnnexB) {
    boolean(R"JS(
function check(x){if(!x)throw new Error('MIR eval function scope');}
let f=function named(n){eval('n=n+1');return eval('named')===named&&n===2};check(f(1));
function blocks(){let seen=0;{eval('var added=3;{function companion(){return added}}');seen=companion()}
return seen===3&&added===3&&typeof companion==='function'}check(blocks());true;
    )JS");
}

TEST_F(JsMvpLmd, MirDirectEvalRemainingCallerCapabilities) {
    boolean("function f(){with({}){return eval('1')===1}}f()");
    MvpLmdFailurePhase phase = MVP_LMD_SUCCESS;
    module("eval('1');export const value=1", "temp/mvp_eval_boundary.mjs", &phase);
    EXPECT_EQ(phase, MVP_LMD_ADMISSION_FAILURE);
}

TEST_F(JsMvpLmd, MirDirectEvalNonStringScalarIdentity) {
    boolean(R"JS(
function check(x){if(!x)throw new Error('MIR eval non-string scalar identity');}
function identity(value){let first=eval(value);let junk=[];for(let i=0;i<20;i++)junk.push({v:i});return [first,eval(value)]}
let tiny=identity(5e-324),wide=identity(9007199254740991),zero=identity(-0),big=identity(9007199254740993n);
check(tiny[0]===5e-324&&tiny[1]===5e-324&&wide[0]===9007199254740991&&wide[1]===9007199254740991&&1/zero[0]===-Infinity&&1/zero[1]===-Infinity&&big[0]===9007199254740993n&&big[1]===9007199254740993n);true;
    )JS");
}

TEST_F(JsMvpLmd, IteratorErrorPadsCloseNestedIteratorsOnce) {
    boolean("let seen='',marker={value:7};function iterator(name){return {[Symbol.iterator](){return this},next(){return {value:1,done:false}},return(){seen+=name;throw {other:1}}}}let caught=false;try{for(let a of iterator('O'))for(let b of iterator('I'))throw marker}catch(e){caught=e===marker&&e.value===7}caught&&seen==='IO'");
    boolean("let closed=0,steps=0,sum=0,it={[Symbol.iterator](){return this},next(){return ++steps<=2?{value:steps,done:false}:{done:true}},return(){closed++;return {}}};for(let x of it){try{throw x}catch(e){sum+=e}}closed===0&&steps===3&&sum===3");
}

TEST_F(JsMvpLmd, IteratorErrorPadsPreserveFinallyBoundaries) {
    boolean("let seen='',marker={};function iterator(name){return {[Symbol.iterator](){return this},next(){return {value:1,done:false}},return(){seen+=name;return {}}}}let caught=false;try{for(let a of iterator('O')){try{for(let b of iterator('I'))throw marker}finally{seen+='F'}}}catch(e){seen+='C';caught=e===marker}caught&&seen==='IFOC'");
    boolean("let seen='',first={},second={};function iterator(name){return {[Symbol.iterator](){return this},next(){return {value:1,done:false}},return(){seen+=name;return {}}}}let caught=false;try{for(let a of iterator('O')){try{for(let b of iterator('I'))throw first}finally{seen+='F';throw second}}}catch(e){seen+='C';caught=e===second}caught&&seen==='IFOC'");
}

TEST_F(JsMvpLmd, IteratorErrorPadsSeparateDestructuringNormalAndThrowPaths) {
    boolean("let closed=0,marker={},caught=false,it={[Symbol.iterator](){return this},next(){return {value:undefined,done:false}},return(){closed++;throw 9}};function fail(){throw marker}try{let [value=fail()]=it}catch(e){caught=e===marker}caught&&closed===1");
    boolean("let closed=0,it={[Symbol.iterator](){return this},next(){return {value:3,done:false}},return(){closed++;return {}}};let [value]=it;value===3&&closed===1");
    boolean("let seen='',marker={},steps=0,outer={[Symbol.iterator](){return this},next(){return ++steps<=2?{value:1,done:false}:{done:true}},return(){seen+='O';return {}}},inner={[Symbol.iterator](){return this},next(){return {value:undefined,done:false}},return(){seen+='I';return {}}};function fail(){throw marker}for(let x of outer){try{let [value=fail()]=inner}catch(e){if(e!==marker)throw 7;seen+='C'}}seen==='ICIC'&&steps===3");
}

TEST_F(JsMvpLmd, IteratorErrorPadsRootPendingThrowAcrossReturnAccessors) {
    boolean("let seen='',caught=false;function iterator(name){return {[Symbol.iterator](){return this},next(){return {value:1,done:false}},get return(){seen+=name;let garbage=[];for(let i=0;i<48;i++)garbage.push({value:i});return function(){seen+=name.toLowerCase();let more=[];for(let i=0;i<48;i++)more.push([i]);throw {replacement:1}}}}}try{for(let x of iterator('O'))for(let y of iterator('I'))throw {keep:'alive',nested:{value:19}}}catch(e){caught=e.keep==='alive'&&e.nested.value===19}caught&&seen==='IiOo'");
    boolean("let seen='',caught=false,it={[Symbol.iterator](){return this},next(){return {value:1,done:false}},get return(){seen+='R';let garbage=[];for(let i=0;i<48;i++)garbage.push({value:i});throw {replacement:1}}};try{for(let x of it)throw {keep:23}}catch(e){caught=e.keep===23}caught&&seen==='R'");
}

TEST_F(JsMvpLmd, ModuleExportNamesDecodeAndEmptyClausesKeepDependencies) {
    create_dir("temp/mvp_modules");
    write_text_file("temp/mvp_modules/ns-decoded.js", "const value=7;export {value as '\\u0061',value as ''};export {};");
    write_text_file("temp/mvp_modules/ns-empty-effect.js", "globalThis.emptyExportRan=true;export {};");
    module("export {} from './ns-empty-effect.js';import * as n from './ns-decoded.js';if(!globalThis.emptyExportRan||n.a!==7||n['']!==7||Reflect.ownKeys(n)[0]!==''||Reflect.ownKeys(n)[1]!=='a')throw 1;", "temp/mvp_modules/ns-decoded-main.js");
}

TEST_F(JsMvpLmd, StringSplitCoercesSeparatorBeforeZeroLimit) {
    boolean("let seen='',sentinel={keep:37},caught=false,receiver={toString(){seen+='S';return 'abc'}},separator={get [Symbol.split](){seen+='M';return undefined},toString(){seen+='R';let garbage=[];for(let i=0;i<32;i++)garbage.push({i});throw sentinel}},limit={valueOf(){seen+='L';return 0}};try{String.prototype.split.call(receiver,separator,limit)}catch(e){caught=e===sentinel}caught&&seen==='MSLR'");
    boolean("let seen='',separator={toString(){seen+='R';return ','}},limit={valueOf(){seen+='L';return 0}};'a,b'.split(separator,limit).length===0&&seen==='LR'");
    boolean("let seen='',sentinel={},caught=false,separator={toString(){seen+='R';return ','}},limit={valueOf(){seen+='L';throw sentinel}};try{'a,b'.split(separator,limit)}catch(e){caught=e===sentinel}caught&&seen==='L'");
    boolean("let result='a,b'.split(undefined,0);result.length===0");
}

TEST_F(JsMvpLmd, MirDirectEvalParameterBodyCells) {
    boolean(R"JS(
function check(x){if(!x)throw new Error('parameter/body cells')}
function split(a=1, saved=()=>eval('a')) {var a;eval('a=7');return [saved,()=>eval('a')]}
let x=split();check(x[0]()===1&&x[1]()===7);
function mutate(a=1, saved=eval('(value)=>a=value')) {var a;eval('a=9');return [saved,()=>eval('a')]}
let y=mutate();check(y[0](4)===4&&y[1]()===9);true;
    )JS");
}

TEST_F(JsMvpLmd, MirDirectEvalParameterHoistedFunction) {
    boolean(R"JS(
function check(x){if(!x)throw new Error('parameter function hoist')}
function split(a=1, saved=()=>eval('a')){function a(){return 9}return [saved,()=>eval('a')]}
let x=split();check(x[0]()===1&&typeof x[1]()==='function'&&x[1]()()===9);
function defaults(a=eval('typeof hidden')){function hidden(){}return a}
check(defaults()==='undefined');true;
    )JS");
}

TEST_F(JsMvpLmd, MirDirectEvalParameterTdzAndClashes) {
    boolean(R"JS(
function check(x){if(!x)throw new Error('parameter TDZ or clash')}
function later(a=eval('b'),b=2){return a}let threw=false;
try{later()}catch(e){threw=e instanceof ReferenceError}check(threw);
function own(a=eval('var a')){}threw=false;try{own()}catch(e){threw=e instanceof SyntaxError}check(threw);
function earlier(a=1,b=eval('var a')){}threw=false;try{earlier()}catch(e){threw=e instanceof SyntaxError}check(threw);
function laterClash(a=eval('var b'),b=2){}threw=false;try{laterClash()}catch(e){threw=e instanceof SyntaxError}check(threw);
function args(a=eval('var arguments')){}threw=false;try{args()}catch(e){threw=e instanceof SyntaxError}check(threw);true;
    )JS");
}

TEST_F(JsMvpLmd, MirDirectEvalParameterArguments) {
    boolean(R"JS(
function check(x){if(!x)throw new Error('parameter arguments')}
function split(a=1,saved=()=>eval('arguments')){var arguments;let first=arguments;eval('arguments=42');return [saved,first,()=>eval('arguments')]}
let x=split(undefined,undefined,8);check(x[0]()[2]===8&&x[1]===x[0]()&&x[2]()===42);
function unmapped(a=1){eval('arguments[0]=6');check(a===2);eval('a=9');return arguments[0]===6}
check(unmapped(2));
function named(arguments=eval('3')){return eval('arguments')===3}check(named());true;
    )JS");
}

TEST_F(JsMvpLmd, MirDirectEvalParameterNestedDeclarations) {
    boolean(R"JS(
function check(x){if(!x)throw new Error('parameter nested declarations')}
function make(a=eval('eval("var added=3");()=>added')) {var added;eval('added=7');return [a,()=>eval('added')]}
let x=make();check(x[0]()===3&&x[1]()===7);
function through(a=eval('var added=4'),read=()=>eval('added')){eval('added=8');return read}
check(through()()===8);
function isolated(a=eval('"use strict";var absent=3'),read=()=>eval('typeof absent')){return read}
check(isolated()()==='undefined');true;
    )JS");
}

TEST_F(JsMvpLmd, MirDirectEvalParameterPatterns) {
    boolean(R"JS(
function check(x){if(!x)throw new Error('parameter patterns')}
function destructure({[eval("'x'")]:a=eval('2')},[b=eval('a+1')]=[],...rest){eval('a+=b');return [a,b,rest,arguments[0].x]}
let x=destructure({},[],7,8);check(x[0]===5&&x[1]===3&&x[2][0]===7&&x[2][1]===8&&x[3]===undefined);
function plain({a},...rest){eval('a=4');arguments[1]=9;return a===4&&rest[0]===3}
check(plain({a:2},3));
function rest(...items){eval('items[0]=7');return items[0]===7&&arguments[0]===2}check(rest(2));true;
    )JS");
}

TEST_F(JsMvpLmd, MirDirectEvalParameterReceiver) {
    boolean(R"JS(
function check(x){if(!x)throw new Error('parameter receiver')}
function C(a=eval('new.target'),b=eval('this')){eval('this.target=a');this.same=b===this}
let x=new C();check(x.target===C&&x.same);
function f(a=eval('this.value'),b=()=>eval('this.value')){return [a,b]}
let receiver={value:7};let y=f.call(receiver);receiver.value=9;check(y[0]===7&&y[1]()===9);true;
    )JS");
}

TEST_F(JsMvpLmd, MirDirectEvalParameterScalarOwnership) {
    boolean(R"JS(
function check(x){if(!x)throw new Error('parameter scalar ownership')}
function f(a=5e-324,b=-0,old=()=>eval('[a,b]')){var a,b;eval('a=9007199254740991;b=3.5');return [old,()=>eval('[a,b]')]}
let x=f();let junk=[];for(let i=0;i<100;i++)junk.push({i:i});let old=x[0](),now=x[1]();
check(old[0]===5e-324&&1/old[1]===-Infinity&&now[0]===9007199254740991&&now[1]===3.5);true;
    )JS");
}

TEST_F(JsMvpLmd, MirDirectEvalParameterStrictAndArrows) {
    boolean(R"JS(
function check(x){if(!x)throw new Error('strict parameter eval')}
(function(){'use strict';function f(a=eval('var added=3;2')){return a===2&&typeof added==='undefined'}check(f())})();
let arrow=(a=eval('1'),saved=()=>eval('a'))=>{var a;eval('a=4');return [saved,()=>eval('a')]};
let x=arrow();check(x[0]()===1&&x[1]()===4);
function parent(a=1,read=()=>a){eval('a=7');return read}check(parent()()===7);true;
    )JS");
}

TEST_F(JsMvpLmd, ColdPropertySitesRetainDescriptorAndReceiverSemantics) {
    boolean("let seen='',parent={get value(){seen+='P';return this.number}},object=Object.create(parent);object.number=7;let first=object.value;Object.defineProperty(object,'value',{get(){seen+='O';return this.number+1},configurable:true});let second=object.value;delete object.value;first===7&&second===8&&object.value===7&&seen==='POP'");
    boolean("let sentinel={keep:19},object={get value(){let garbage=[];for(let i=0;i<32;i++)garbage.push({i});throw sentinel}},caught=false;try{object.value}catch(e){caught=e===sentinel&&e.keep===19}caught");
    boolean("let object={value:1};let first=object.value;Object.defineProperty(object,'value',{get(){return 2},configurable:true});let second=object.value;Object.defineProperty(object,'value',{value:3,writable:true});first===1&&second===2&&object.value===3");
}
TEST_F(JsMvpLmd, RepeatedPropertySitesRetainCacheInvalidation) {
    boolean("let object={value:1},sum=0;for(let i=0;i<6;i++){if(i===2)Object.defineProperty(object,'value',{get(){return 5},configurable:true});if(i===4)Object.defineProperty(object,'value',{value:9,writable:true});sum+=object.value}sum===30");
    boolean("function read(object){return object.value}let parent={value:1},object=Object.create(parent);let first=read(object);parent.value=2;let second=read(object);object.value=3;let third=read(object);delete object.value;first===1&&second===2&&third===3&&read(object)===2");
}
TEST_F(JsMvpLmd, GenericArraySearchRetainsHolesEqualityAndCoercion) {
    boolean("let objects=[[,NaN,4],new Float64Array([NaN,4])],ok=true;for(let object of objects){ok=ok&&object.includes(NaN)&&object.indexOf(NaN)===-1&&object.includes(4)&&object.indexOf(4)>=0}ok&&objects[0].includes(undefined)&&objects[0].indexOf(undefined)===-1");
    boolean("let seen='',array=[1,2,3],owners=[array,new Int8Array([1,2,3])];for(let owner of owners){let from={valueOf(){seen+='F';return 1}};if(!owner.includes(2,from)||owner.indexOf(3,from)!==2)throw 9}seen==='FFFF'");
    boolean("let token={},other={},arrays=[[token,1n,'1'],[other,2n,'2']],ok=true;for(let owner of arrays){ok=ok&&owner.indexOf(owner[0])===0&&!owner.includes({})&&!owner.includes(1)&&owner.includes(owner[1])}ok&&arrays[0].indexOf(other)===-1&&arrays[0].includes('1')");
    boolean("let seen='',array=[1,2,3],owners=[array,new Int8Array([1])],ok=true;for(let owner of owners){if(owner===array){let from={valueOf(){seen+='F';Object.defineProperty(array,'1',{get(){seen+='G';return 4}});return 1}};ok=ok&&owner.includes(4,from)}else ok=ok&&owner.indexOf(1)===0}ok&&seen==='FG'");
}

TEST_F(JsMvpLmd, MirDirectEvalClassMethods) {
    boolean(R"JS(
function check(x){if(!x)throw new Error('class eval methods')}
class A {get value(){return this.x}set value(x){this.x=x}plus(x){return this.x+x}static read(){return this.tag}}
class B extends A {get current(){return eval('super.value')}set current(x){eval('super.value=x')}sum(x){return eval('super.plus(x)')}static tag=8;static read(){return eval('super.read()')}}
let x=new B();x.current=3;check(x.current===3&&x.sum(4)===7&&B.read()===8&&x instanceof A&&x instanceof B);true;
    )JS");
}

TEST_F(JsMvpLmd, MirDirectEvalConstructorSuperCells) {
    boolean(R"JS(
function check(x){if(!x)throw new Error('constructor eval cells')}
let fields=0;class A {constructor(x){this.x=x}get value(){return this.x}}
class B extends A {field=(fields++,4);constructor(x){let read=()=>eval('this');eval('super(x)');this.same=read()===this;this.value2=eval('super.value');this.target=eval('new.target')}}
let x=new B(3);check(x.same&&x.value2===3&&x.target===B&&x.field===4&&fields===1);true;
    )JS");
}

TEST_F(JsMvpLmd, MirDirectEvalDuplicateSuperEffects) {
    boolean(R"JS(
function check(x){if(!x)throw new Error('duplicate super effects')}
let parents=0,fields=0;class A {constructor(){parents++}}
class B extends A {field=(fields++,1);constructor(){eval('super()');try{eval('super()')}catch(e){this.failed=e instanceof ReferenceError}}}
let x=new B();check(x.failed&&parents===2&&fields===1);
class Bad extends A {constructor(){eval('this')}}let threw=false;try{new Bad()}catch(e){threw=e instanceof ReferenceError}check(threw);true;
    )JS");
}

TEST_F(JsMvpLmd, MirDirectEvalClassPrivateMembers) {
    boolean(R"JS(
function check(x){if(!x)throw new Error('private eval members')}
class C {#x=2;#method(){return this.#x+1}get #access(){return this.#x}set #access(x){this.#x=x}
read(){return eval('[this.#x,this.#method(),this.#access]')}write(x){eval('this.#access=x')}has(other){return eval('#x in other')}
static #s=7;static read(){return eval('this.#s')}}
let x=new C();check(x.read()[1]===3&&x.has(x)&&!x.has({})&&C.read()===7);x.write(9);check(x.read()[0]===9&&x.read()[2]===9);true;
    )JS");
}

TEST_F(JsMvpLmd, MirDirectEvalPrivateBrandsAndLifetime) {
    boolean(R"JS(
function check(x){if(!x)throw new Error('private eval brands')}
function make(){return class {#x=3;reader(){return eval('(object)=>object.#x')}has(object){return eval('#x in object')}}}
let A=make(),B=make(),a=new A(),b=new B(),read=a.reader();check(read(a)===3&&a.has(a)&&!a.has(b));
let threw=false;try{read(b)}catch(e){threw=e instanceof TypeError}check(threw);
let junk=[];for(let i=0;i<100;i++)junk.push({i:i});check(read(a)===3);true;
    )JS");
}

TEST_F(JsMvpLmd, MirDirectEvalClassNestedPrivate) {
    boolean(R"JS(
function check(x){if(!x)throw new Error('nested private eval')}
class Outer {#x=5;make(){return class Inner {#y=7;read(outer){return eval('outer.#x+this.#y')}closure(outer){return eval('()=>outer.#x+this.#y')}}}}
let outer=new Outer(),Inner=outer.make(),inner=new Inner(),read=inner.closure(outer);check(inner.read(outer)===12&&read()===12);
class Shadow {#x=8;make(){return class Inner {#x=9;read(){return eval('this.#x')}}}}check(new (new Shadow().make())().read()===9);true;
    )JS");
}

TEST_F(JsMvpLmd, MirDirectEvalEscapedMethodHome) {
    boolean(R"JS(
function check(x){if(!x)throw new Error('escaped method home')}
class A {get value(){return this.tag}}
class B extends A {#x=3;tag=4;read(){return eval('()=>super.value+this.#x')}mir(){return ()=>eval('super.value+this.#x')}}
let x=new B(),read=x.read(),mir=x.mir();Object.defineProperty(read,'extra',{value:1});Object.defineProperty(mir,'extra',{value:2});
let junk=[];for(let i=0;i<100;i++)junk.push({i:i});check(read()===7&&mir()===7);Object.setPrototypeOf(B.prototype,{get value(){return this.tag+1}});check(read()===8&&mir()===8);true;
    )JS");
}

TEST_F(JsMvpLmd, MirDirectEvalObjectMethodHome) {
    boolean(R"JS(
function check(x){if(!x)throw new Error('object eval home')}
let base={get value(){return this.tag},set value(x){this.tag=x}};
function make(){return {__proto__:base,tag:3,read(){return eval('super.value')},write(x){eval('super.value=x')},arrow(){return ()=>eval('super.value')}}}
let a=make(),b=make(),read=a.arrow();a.write(7);check(a.read()===7&&b.read()===3&&read()===7);
Object.setPrototypeOf(a,{get value(){return this.tag+1}});check(a.read()===8&&read()===8&&b.read()===3);true;
    )JS");
}

TEST_F(JsMvpLmd, MirDirectEvalClassComputedScope) {
    boolean(R"JS(
function check(x){if(!x)throw new Error('class computed eval scope')}
let failed=false;try{let C=class C {[eval('C')](){}}}catch(e){failed=e instanceof ReferenceError}check(failed);
function make(){let receiver=this;return class C {[eval("this.key")](){return eval('C')}static receiver=eval('receiver')}}
let receiver={key:'read'},C=make.call(receiver);check(new C().read()===C&&C.receiver===receiver);
class Outer {#x=5;make(object){return class extends eval('Object') {[eval("'#x' in {} ? 'bad' : 'read'")](){return eval('object.#x')}}}}
let outer=new Outer(),Inner=outer.make(outer);check(new Inner().read()===5);true;
    )JS");
}

TEST_F(JsMvpLmd, MirDirectEvalClassInitializers) {
    boolean(R"JS(
function check(x){if(!x)throw new Error('class initializer eval')}
class A {get value(){return 3}static get value(){return 7}}
class B extends A {#x=2;field=eval('super.value+this.#x');arrow=()=>eval('super.value+this.#x');target=eval('new.target');
static field=eval('super.value');static target=eval('new.target');static {this.block=eval('super.value+1')}
static bad(){try{eval('super()')}catch(e){return e instanceof SyntaxError}}}
let x=new B();check(x.field===5&&x.arrow()===5&&x.target===undefined&&B.field===7&&B.target===undefined&&B.block===8&&B.bad());
class Bad {field=(()=>{try{eval('arguments')}catch(e){return e instanceof SyntaxError}})();static {try{eval('arguments')}catch(e){this.bad=e instanceof SyntaxError}}}
check(new Bad().field&&Bad.bad);true;
    )JS");
}

TEST_F(JsMvpLmd, MirDirectEvalConstructorDefaults) {
    boolean(R"JS(
function check(x){if(!x)throw new Error('constructor defaults eval')}
class A {constructor(x){this.x=x}}
class B extends A {constructor(x=2,init=eval('super(x)'),target=eval('new.target')){this.same=init===this;this.target=target}}
let x=new B();check(x.x===2&&x.same&&x.target===B);
class C extends A {constructor(x=eval('3'),get=()=>eval('x')){super(x);var x;eval('x=7');this.read=get;this.current=()=>eval('x')}}
let y=new C();check(y.x===3&&y.read()===3&&y.current()===7);true;
    )JS");
}

TEST_F(JsMvpLmd, DynamicConstructorHomeProjection) {
    boolean(R"JS(
function check(x){if(!x)throw new Error('hosted constructor home')}
check(Function('Object.prototype.value=3;try{class A{constructor(){this.seen=super.value}}return new A().seen===3}finally{delete Object.prototype.value}')());
check(Function('class A{get value(){return this.tag}}class B extends A{constructor(){super();this.tag=7;this.seen=super.value}}return new B().seen===7')());
check(Function('class A{get value(){return this.tag}}class B extends A{constructor(){super();this.tag=7;this.read=()=>super.value}}return new B().read()===7')());
check(Function('class A{get value(){return this.tag}}class B extends A{constructor(){eval("super()");this.tag=7;this.seen=eval("super.value")}}return new B().seen===7')());true;
    )JS");
}

TEST_F(JsMvpLmd, ModuleTopLevelAwaitPreservesLiveCellsAcrossSeveralParks) {
    create_dir("temp/mvp_modules");
    write_text_file("temp/mvp_modules/tla-values.js", "export let value=await Promise.resolve(3);await 0;value+=4;export const ready=true;");
    module("import {value,ready} from './tla-values.js';if(value!==7||!ready)throw 1;let events=[];Promise.resolve().then(()=>events.push('job'));await 0;events.push('body');if(events.join(',')!=='job,body')throw 2;", "temp/mvp_modules/tla-main.js");
}
TEST_F(JsMvpLmd, ModuleTopLevelAwaitUsesExistingTimersAndScalarRoots) {
    create_dir("temp/mvp_modules");
    write_text_file("temp/mvp_modules/tla-timer.js", "export let tiny=await new Promise(resolve=>setTimeout(resolve,0,5e-324));export let minus=await Promise.resolve(-0);export const ready=true;");
    module("import {tiny,minus,ready} from './tla-timer.js';if(!ready||tiny!==5e-324||1/minus!==-Infinity)throw 1;let seen=[];for await(let x of [Promise.resolve(2),3])seen.push(x);if(seen.join(',')!=='2,3')throw 2;", "temp/mvp_modules/tla-timer-main.js");
}
TEST_F(JsMvpLmd, ModuleAsyncDependencyOrderFollowsDfs) {
    create_dir("temp/mvp_modules");
    write_text_file("temp/mvp_modules/tla-order-leaf.js", "globalThis.moduleOrder=['leaf0'];await 0;moduleOrder.push('leaf1');");
    write_text_file("temp/mvp_modules/tla-order-left.js", "import './tla-order-leaf.js';moduleOrder.push('left');");
    write_text_file("temp/mvp_modules/tla-order-right.js", "import './tla-order-leaf.js';moduleOrder.push('right');");
    module("import './tla-order-left.js';import './tla-order-right.js';moduleOrder.push('root');if(moduleOrder.join(',')!=='leaf0,leaf1,left,right,root')throw 1;", "temp/mvp_modules/tla-order-main.js");
}
TEST_F(JsMvpLmd, ModuleAsyncCyclesUseDfsRootCompletion) {
    create_dir("temp/mvp_modules");
    write_text_file("temp/mvp_modules/tla-cycle-a.js", "import {b} from './tla-cycle-b.js';export let a;await 0;a=b+1;");
    write_text_file("temp/mvp_modules/tla-cycle-b.js", "import './tla-cycle-a.js';export let b;await 0;b=2;");
    module("import {a} from './tla-cycle-a.js';import {b} from './tla-cycle-b.js';if(a!==3||b!==2)throw 1;let ns=await import('./tla-cycle-b.js');if(ns.b!==2)throw 2;", "temp/mvp_modules/tla-cycle-main.js");
}
TEST_F(JsMvpLmd, DynamicImportHasFreshPromisesAndCanonicalNamespace) {
    create_dir("temp/mvp_modules");
    write_text_file("temp/mvp_modules/import-identity.js", "globalThis.moduleLoadCount=(globalThis.moduleLoadCount||0)+1;export let value=7;");
    module("import * as staticNs from './import-identity.js';let first=import('./import-identity.js'),second=import('././import-identity.js');if(first===second)throw 1;let a=await first,b=await second;if(a!==b||a!==staticNs||moduleLoadCount!==1||a.value!==7)throw 2;", "temp/mvp_modules/import-identity-main.js");
}
TEST_F(JsMvpLmd, DynamicImportCoercionRejectsWithoutSynchronousThrow) {
    module("let token={tag:9},p;try{p=import({toString(){throw token}})}catch(e){throw 1}let reason;try{await p}catch(e){reason=e}if(reason!==token)throw 2;let type=false;try{await import(Symbol())}catch(e){type=e instanceof TypeError}if(!type)throw 3;", "temp/mvp_modules/import-coerce-main.js");
}
TEST_F(JsMvpLmd, DynamicImportKeepsNullUndefinedAndScalarRejections) {
    create_dir("temp/mvp_modules");
    write_text_file("temp/mvp_modules/import-null.js", "await 0;throw null;");
    write_text_file("temp/mvp_modules/import-undefined.js", "await 0;throw undefined;");
    write_text_file("temp/mvp_modules/import-scalar.js", "await 0;throw 5e-324;");
    module("let seen=[];for(let name of ['./import-null.js','./import-undefined.js','./import-scalar.js']){try{await import(name)}catch(e){seen.push(e)}}if(seen.length!==3||seen[0]!==null||seen[1]!==undefined||seen[2]!==5e-324)throw 1;", "temp/mvp_modules/import-reject-main.js");
}
TEST_F(JsMvpLmd, DynamicImportCachesFailureAndSuppressesDependentBodies) {
    create_dir("temp/mvp_modules");
    write_text_file("temp/mvp_modules/import-fail-child.js", "globalThis.failedRuns=(globalThis.failedRuns||0)+1;await 0;throw globalThis.moduleToken;");
    write_text_file("temp/mvp_modules/import-fail-parent.js", "import './import-fail-child.js';globalThis.forbiddenBody=true;");
    module("globalThis.moduleToken={tag:5};let first,second;try{await import('./import-fail-parent.js')}catch(e){first=e}try{await import('./import-fail-parent.js')}catch(e){second=e}if(first!==moduleToken||second!==moduleToken||failedRuns!==1||globalThis.forbiddenBody!==undefined)throw 1;", "temp/mvp_modules/import-fail-main.js");
}
TEST_F(JsMvpLmd, DynamicImportHandlesSyntaxFailureWithoutPoisoningCaller) {
    create_dir("temp/mvp_modules");
    write_text_file("temp/mvp_modules/import-invalid.js", "export const = 1;");
    module("let type=false;try{await import('./import-invalid.js')}catch(e){type=e instanceof SyntaxError}if(!type)throw 1;await 0;", "temp/mvp_modules/import-invalid-main.js");
}
TEST_F(JsMvpLmd, DynamicImportResolvesNamespaceThenExport) {
    create_dir("temp/mvp_modules");
    write_text_file("temp/mvp_modules/import-then.js", "export function then(resolve){resolve(17)};");
    module("let value=await import('./import-then.js');if(value!==17)throw 1;", "temp/mvp_modules/import-then-main.js");
}
TEST_F(JsMvpLmd, DynamicImportUsesDefiningModuleOriginAfterResume) {
    create_dir("temp/mvp_modules"); create_dir("temp/mvp_modules/import-nested");
    write_text_file("temp/mvp_modules/import-nested/value.js", "export const value=29;");
    write_text_file("temp/mvp_modules/import-nested/loader.js", "export function load(){return import('./value.js')}");
    module("import {load} from './import-nested/loader.js';await 0;let n=await load();if(n.value!==29)throw 1;", "temp/mvp_modules/import-origin-main.js");
}
TEST_F(JsMvpLmd, DynamicSelfImportWaitsForCompleteModule) {
    create_dir("temp/mvp_modules");
    write_text_file("temp/mvp_modules/import-self.js", "export let value=1;export const requested=import('./import-self.js');await 0;value=7;");
    module("let n=await import('./import-self.js'),again=await n.requested;if(again!==n||again.value!==7)throw 1;", "temp/mvp_modules/import-self-main.js");
}
TEST_F(JsMvpLmd, DynamicImportInternalObservationIgnoresSpecies) {
    create_dir("temp/mvp_modules");
    write_text_file("temp/mvp_modules/import-species.js", "await 0;export const value=13;");
    module("Object.defineProperty(Promise,Symbol.species,{get(){throw 8},configurable:true});let n=await import('./import-species.js');if(n.value!==13)throw 1;", "temp/mvp_modules/import-species-main.js");
}
TEST_F(JsMvpLmd, ClassicDynamicImportUsesSameContextAndIndependentRegistries) {
    create_dir("temp/mvp_modules");
    write_text_file("temp/mvp_modules/import-classic.js", "globalThis.classicCount=(globalThis.classicCount||0)+1;export const count=globalThis.classicCount;");
    for (int repetition=0;repetition<2;repetition++) {
        Item result=run("let out=[];import('./temp/mvp_modules/import-classic.js').then(n=>out.push(n.count));out");
        ASSERT_EQ(get_type_id(result),LMD_TYPE_ARRAY);
        ASSERT_EQ(result.array->length,1);
        EXPECT_EQ(it2d(result.array->items[0]),1);
    }
}
TEST_F(JsMvpLmd, FunctionCreatedImportAndSchedulerStayInterpreted) {
    create_dir("temp/mvp_modules");
    write_text_file("temp/mvp_modules/import-function.js", "export const value=31;");
    Item result=run("let out=[];Function('out',\"setTimeout(()=>out.push(5e-324),0);return import('./temp/mvp_modules/import-function.js')\")(out).then(n=>out.push(n.value));out");
    ASSERT_EQ(get_type_id(result),LMD_TYPE_ARRAY);
    ASSERT_EQ(result.array->length,2);
    double first=it2d(result.array->items[0]),second=it2d(result.array->items[1]);
    EXPECT_TRUE((first==31&&second==5e-324)||(first==5e-324&&second==31));
}
TEST_F(JsMvpLmd, UnsettledTopLevelAwaitIsNotReportedAsCompletion) {
    MvpLmdFailurePhase phase=MVP_LMD_SUCCESS;
    Item result=module("await new Promise(()=>{});throw 7;", "temp/mvp_modules/tla-unsettled.js", &phase);
    EXPECT_EQ(get_type_id(result),LMD_TYPE_ERROR);
    EXPECT_EQ(phase,MVP_LMD_RUNTIME_FAILURE);
}
TEST_F(JsMvpLmd, AsyncModuleRejectionReachesSccSiblingsAndStaticRoot) {
    create_dir("temp/mvp_modules");
    write_text_file("temp/mvp_modules/tla-error-cycle-a.js", "import './tla-error-cycle-b.js';await 0;throw 5e-324;");
    write_text_file("temp/mvp_modules/tla-error-cycle-b.js", "import './tla-error-cycle-a.js';await 0;export const value=2;");
    write_text_file("temp/mvp_modules/tla-error-cycle-parent.js", "import './tla-error-cycle-b.js';globalThis.cycleForbidden=true;");
    module("let first,second;try{await import('./tla-error-cycle-parent.js')}catch(e){first=e}try{await import('./tla-error-cycle-b.js')}catch(e){second=e}if(first!==5e-324||second!==5e-324||globalThis.cycleForbidden!==undefined)throw 1;", "temp/mvp_modules/tla-error-cycle-main.js");
    MvpLmdFailurePhase phase=MVP_LMD_SUCCESS;
    Item result=module("import './tla-error-cycle-parent.js';", "temp/mvp_modules/tla-error-static.js", &phase);
    ASSERT_EQ(get_type_id(result),LMD_TYPE_ERROR);
    EXPECT_EQ(phase,MVP_LMD_RUNTIME_FAILURE);
    EXPECT_EQ(it2d(js_error_lane_payload(result)),5e-324);
}
TEST_F(JsMvpLmd, PublicDynamicImportRejectionRetainsUnhandledTracking) {
    create_dir("temp/mvp_modules");
    write_text_file("temp/mvp_modules/import-public-rejection.js", "await 0;throw 7;");
    Item result=run("import('./temp/mvp_modules/import-public-rejection.js')");
    ASSERT_TRUE(js_promise_is(result));
    JsPromise* promise=(JsPromise*)result.vmap;
    EXPECT_EQ(promise->state,JS_PROMISE_REJECTED);
    EXPECT_FALSE(promise->rejection_handled);
    EXPECT_TRUE(promise->unhandled_reported);
    EXPECT_EQ(it2d(promise->result),7);
}

TEST_F(JsMvpLmd, MirDirectEvalStaticClassInnerCell) {
    boolean(R"JS(
var executed = false;
class C {}
try {
  eval('executed = true; new C().#x');
  throw 'missing SyntaxError';
} catch (error) {
  if (!(error instanceof SyntaxError)) throw error;
}
if (executed) throw 'executed invalid eval';
let outer = 4;
class D { method() { return outer; } }
if (eval(1) !== 1 || new D().method() !== 4) throw 'class cell initialization';
true;
)JS");
}

TEST_F(JsMvpLmd, MirDirectEvalStaticClassMethodCapture) {
    boolean(R"JS(
function check(value) { if (!value) throw 'class name capture'; }
class C {
  constructor() { this.className = C; }
  getClass() { return C; }
  arrow() { return () => C; }
  static getClass() { return C; }
}
let original = C, instance = new C(), arrow = instance.arrow(), detached = instance.getClass;
eval('C = 1');
check(instance.className === original && instance.getClass() === original);
check(arrow() === original && detached() === original && original.getClass() === original);
class D { static factory() { return function() { return D; }; } }
let getD = D.factory(), originalD = D;
eval('D = 2');
check(getD() === originalD);
true;
)JS");
}

TEST_F(JsMvpLmd, MirDirectEvalGeneratorLiveCells) {
    boolean(R"JS(
function check(x){if(!x)throw 'generator live cells'}
function* make(a){let local=1;let before=()=>added+a+local;eval('var added=3');yield ()=>eval('a+local+added');a+=10;local+=2;return before()}
let a=make(2),b=make(20),readA=a.next().value,readB=b.next().value;
check(readA()===6&&readB()===24);let doneA=a.next();check(doneA.done&&doneA.value===18&&readA()===18&&readB()===24);
let doneB=b.next();check(doneB.done&&doneB.value===36&&readB()===36);true;
)JS");
}

TEST_F(JsMvpLmd, MirDirectEvalGeneratorParameterCells) {
    boolean(R"JS(
function check(x){if(!x)throw 'generator parameters'}
let effects=0;
function* make(a=(effects++,eval('2')),read=()=>a){var a;eval('a=5');yield read;yield ()=>eval('a');return a}
let iterator=make();check(effects===1);let parameter=iterator.next().value,body=iterator.next().value;
check(parameter()===2&&body()===5&&iterator.next().value===5&&parameter()===2&&body()===5);
function* mapped(a){eval('a=7');yield arguments[0];arguments[0]=8;return eval('a')}
let m=mapped(1);check(m.next().value===7&&m.next().value===8);true;
)JS");
}

TEST_F(JsMvpLmd, MirDirectEvalGeneratorAbruptLifetime) {
    boolean(R"JS(
function check(x){if(!x)throw 'generator abrupt'}
let read;
function* make(){let value=1;read=eval('()=>value');try{yield value;eval('value=2')}finally{eval('value=3');yield read()}}
let g=make();check(g.next().value===1&&g.return(8).value===3);let done=g.next();check(done.done&&done.value===8&&read()===3);
function* caught(){let value=2;try{yield 1}catch(e){eval('value=e');yield eval('value')}return eval('()=>value')}
let c=caught();c.next();check(c.throw(7).value===7&&c.next().value()===7);true;
)JS");
}

TEST_F(JsMvpLmd, MirDirectEvalGeneratorClassContext) {
    boolean(R"JS(
function check(x){if(!x)throw 'generator class'}
class A {get value(){return this.x}}
class B extends A {#value=4;constructor(){super();this.x=3}*run(){yield eval('super.value+this.#value');eval('this.#value=8');return eval('()=>this.#value')}}
let b=new B(),g=b.run();check(g.next().value===7);let read=g.next().value;check(read()===8&&b instanceof A&&b instanceof B);true;
)JS");
}

TEST_F(JsMvpLmd, MirDirectEvalAsyncLiveCells) {
    expect_async_flags(run(R"JS(
let out=[],releaseA,releaseB,readA,readB;
async function make(a,wait){let local=1;let prior=()=>added+a+local;eval('var added=3');await wait;eval('local+=2');return [prior,eval('()=>a+local+added')]}
let pa=new Promise(r=>releaseA=r),pb=new Promise(r=>releaseB=r);
make(2,pa).then(pair=>{readA=pair[0];out.push(readA()===8&&pair[1]()===8)},()=>out.push(false));
make(20,pb).then(pair=>{readB=pair[0];out.push(readB()===26&&pair[1]()===26)},()=>out.push(false));
releaseB();releaseA();Promise.all([pa,pb]).then(()=>Promise.resolve()).then(()=>out.push(readA()===8&&readB()===26),()=>out.push(false));out;
)JS"),3);
}

TEST_F(JsMvpLmd, MirDirectEvalAsyncParametersAndScalars) {
    expect_async_flags(run(R"JS(
let out=[];
async function split(a=5e-324,read=()=>a){var a;eval('a=9007199254740992');await 0;return read()===5e-324&&eval('a')===9007199254740992}
async function mapped(a){eval('a=7');let same=arguments[0]===7;await 0;arguments[0]=8;return same&&eval('a')===8}
split().then(v=>out.push(v),()=>out.push(false));mapped(1).then(v=>out.push(v),()=>out.push(false));out;
)JS"),2);
}

TEST_F(JsMvpLmd, MirDirectEvalAsyncReceiverAndErrors) {
    expect_async_flags(run(R"JS(
let out=[],receiver={value:9};
async function inspect(){'use strict';let arrow=()=>eval('this');await 0;return eval('this')===receiver&&arrow()===receiver&&eval('new.target')===undefined}
async function rejected(){let marker={};try{await 0;eval('throw marker')}catch(e){return e===marker}return false}
inspect.call(receiver).then(v=>out.push(v),()=>out.push(false));rejected().then(v=>out.push(v),()=>out.push(false));out;
)JS"),2);
}

TEST_F(JsMvpLmd, MirDirectEvalAsyncGeneratorQueue) {
    expect_async_flags(run(R"JS(
let out=[],read;
async function* make(a){let value=a;read=eval('()=>value');try{await 0;yield eval('value');eval('value+=1');yield value}finally{eval('value+=10')}}
let g=make(2);g.next().then(r=>out.push(r.value===2&&!r.done),()=>out.push(false));
g.next().then(r=>out.push(r.value===3&&!r.done),()=>out.push(false));
g.return(9).then(r=>out.push(r.done&&r.value===9&&read()===13),()=>out.push(false));out;
)JS"),3);
}

TEST_F(JsMvpLmd, MirDirectEvalSuspendedSourceRestrictions) {
    expect_async_flags(run(R"JS(
let out=[];
function* g(){let passed=false;try{eval('yield 1')}catch(e){passed=e instanceof SyntaxError}yield passed}
async function a(){let passed=false;try{eval('await 1')}catch(e){passed=e instanceof SyntaxError}await 0;return passed}
let first=g().next();a().then(v=>out.push(first.value===true&&!first.done&&v),()=>out.push(false));out;
)JS"),1);
}

TEST_F(JsMvpLmd, MirDirectEvalAsyncClassPrivateHome) {
    expect_async_flags(run(R"JS(
let out=[];
class A {get value(){return this.x}}
class B extends A {#value=4;constructor(){super();this.x=3}async run(){await 0;return eval('super.value+this.#value')}async *iterate(){yield eval('this.#value');await 0;eval('this.#value=9');return eval('()=>this.#value')}}
let b=new B();b.run().then(v=>out.push(v===7),()=>out.push(false));let g=b.iterate();g.next().then(r=>g.next().then(end=>out.push(r.value===4&&!r.done&&end.done&&end.value()===9)),()=>out.push(false));out;
)JS"),2);
}

TEST_F(JsMvpLmd, DynamicImportHostedClosuresRetainNestedCreatorOrigin) {
    create_dir("temp/mvp_modules"); create_dir("temp/mvp_modules/host-origin");
    write_text_file("temp/mvp_modules/host-origin/value.js", "export const value=41;");
    write_text_file("temp/mvp_modules/value.js", "export const value=97;");
    write_text_file("temp/mvp_modules/host-origin/loader.js", "export const made=Function(\"return import('./value.js')\");export const evaluated=(0,eval)(\"(function(){return import('./value.js')})\");");
    module("import {made,evaluated} from './host-origin/loader.js';await 0;let a=await made(),b=await evaluated();if(a.value!==41||b!==a)throw 1;let root=await import('./value.js');if(root.value!==97)throw 2;", "temp/mvp_modules/host-origin-main.js");
}
TEST_F(JsMvpLmd, DynamicImportLateFactoriesRestoreOriginAcrossSuspension) {
    create_dir("temp/mvp_modules"); create_dir("temp/mvp_modules/host-late");
    write_text_file("temp/mvp_modules/host-late/value.js", "export const value=43;");
    write_text_file("temp/mvp_modules/value.js", "export const value=97;");
    write_text_file("temp/mvp_modules/host-late/loader.js", "export function later(){return Function(\"return import('./value.js')\")}export const parked=async function(){await 0;return (0,eval)(\"(function(){return import('./value.js')})\")};export const nested=Function(\"return eval('(function(){return import(\\\"./value.js\\\")})')\");");
    module("import {later,parked,nested} from './host-late/loader.js';await 0;let a=await later()(),f=await parked(),b=await f(),c=await nested()();if(a.value!==43||b!==a||c!==a)throw 1;let root=await Function(\"return import('./value.js')\")();if(root.value!==97)throw 2;", "temp/mvp_modules/host-late-main.js");
}
TEST_F(JsMvpLmd, DynamicImportIdenticalFunctionSourcesKeepIndependentOrigins) {
    create_dir("temp/mvp_modules"); create_dir("temp/mvp_modules/host-left"); create_dir("temp/mvp_modules/host-right");
    write_text_file("temp/mvp_modules/host-left/value.js", "export const value=47;");
    write_text_file("temp/mvp_modules/host-right/value.js", "export const value=53;");
    write_text_file("temp/mvp_modules/host-left/loader.js", "export const load=Function(\"return import('./value.js')\");");
    write_text_file("temp/mvp_modules/host-right/loader.js", "export const load=Function(\"return import('./value.js')\");");
    module("import {load as left} from './host-left/loader.js';import {load as right} from './host-right/loader.js';let a=await left(),b=await right();if(a.value!==47||b.value!==53||a===b)throw 1;", "temp/mvp_modules/host-cache-main.js");
}
TEST_F(JsMvpLmd, DynamicImportGeneratorDefaultsAndBodyRetainOrigin) {
    create_dir("temp/mvp_modules"); create_dir("temp/mvp_modules/host-generator");
    write_text_file("temp/mvp_modules/host-generator/value.js", "export const value=59;");
    write_text_file("temp/mvp_modules/host-generator/loader.js", "const G=Object.getPrototypeOf(function*(){}).constructor;export const make=G(\"g=Function(\\\"return import('./value.js')\\\")\",\"yield g;yield Function(\\\"return import('./value.js')\\\")\");");
    module("import {make} from './host-generator/loader.js';await 0;let it=make(),first=it.next().value;await 0;let second=it.next().value;let a=await first(),b=await second();if(a.value!==59||b!==a)throw 1;", "temp/mvp_modules/host-generator-main.js");
}

TEST_F(JsMvpLmd, RegExpConstructorCallAndConstructIdentity) {
    boolean("let r=/a/g,copy=new RegExp(r);RegExp(r)===r&&copy!==r&&copy.source==='a'&&copy.flags==='g'&&copy.lastIndex===0");
    boolean("let seen='',object={get [Symbol.match](){seen+='M';return true},get constructor(){seen+='C';return RegExp},source:'a',flags:'i'};let same=RegExp(object),copy=new RegExp(object);same===object&&copy.source==='a'&&copy.flags==='i'&&seen==='MCM'");
    boolean("let r=/a/g;Object.defineProperty(r,Symbol.match,{value:null});Object.defineProperty(r,'constructor',{get(){throw 19}});let copy=RegExp(r);copy!==r&&copy.source==='a'&&copy.flags==='g'");
    boolean("let r=/a/g;Object.defineProperty(r,Symbol.match,{value:false});Object.defineProperty(r,'source',{get(){throw 20}});Object.defineProperty(r,'flags',{get(){throw 21}});let copy=new RegExp(r,'i');copy!==r&&copy.source==='a'&&copy.flags==='i'");
}
TEST_F(JsMvpLmd, RegExpConstructorOrdersSelectionPrototypeAndCoercion) {
    boolean("let seen='',proto={},pattern={get [Symbol.match](){seen+='M';return true},get source(){seen+='S';return {toString(){seen+='p';return 'a'}}},get flags(){seen+='F';return {toString(){seen+='f';return 'g'}}}};function T(){}let target=new Proxy(T,{get(o,k,r){if(k==='prototype'){seen+='P';return proto}return Reflect.get(o,k,r)}});let value=Reflect.construct(RegExp,[pattern],target);seen==='MSFPpf'&&Object.getPrototypeOf(value)===proto&&RegExp.prototype.exec.call(value,'a')[0]==='a'");
    boolean("let seen='',pattern={get [Symbol.match](){seen+='M';return true},get constructor(){seen+='C';return null},get source(){seen+='S';return {toString(){seen+='p';return 'a'}}},get flags(){seen+='F';return {toString(){seen+='f';return 'i'}}}};let value=RegExp(pattern);seen==='MCSFpf'&&value.source==='a'&&value.flags==='i'");
    boolean("let seen='',pattern={get [Symbol.match](){seen+='M';return null},get source(){throw 7},toString(){seen+='p';return 'a'}},flags={toString(){seen+='f';return 'i'}};let value=new RegExp(pattern,flags);seen==='Mpf'&&value.source==='a'&&value.flags==='i'");
    boolean("let seen='',pattern={get [Symbol.match](){seen+='M';return true},get source(){seen+='S';return undefined},get flags(){throw 7}},flags={toString(){seen+='F';return ''}};let value=new RegExp(pattern,flags);seen==='MSF'&&value.source==='(?:)'");
}
TEST_F(JsMvpLmd, RegExpConstructorPreservesAbruptCompletionAndScalarHomes) {
    boolean("let token={keep:17},seen='',pattern={get [Symbol.match](){seen+='M';let garbage=[];for(let i=0;i<32;i++)garbage.push({i});throw token}},caught=false;try{new RegExp(pattern,{toString(){seen+='F';return ''}})}catch(e){caught=e===token&&e.keep===17}caught&&seen==='M'");
    boolean("let token={},seen='',pattern={get [Symbol.match](){seen+='M';return true},get source(){seen+='S';return {toString(){seen+='p';return 'a'}}},get flags(){seen+='F';throw token}};function T(){}let target=new Proxy(T,{get(o,k,r){if(k==='prototype'){seen+='P';return {}}return Reflect.get(o,k,r)}}),caught=false;try{Reflect.construct(RegExp,[pattern],target)}catch(e){caught=e===token}caught&&seen==='MSF'");
    boolean("let token={},seen='',pattern={get [Symbol.match](){seen+='M';return false},toString(){seen+='S';return 'a'}};function T(){}let target=new Proxy(T,{get(o,k,r){if(k==='prototype'){seen+='P';let garbage=[];for(let i=0;i<32;i++)garbage.push({i});throw token}return Reflect.get(o,k,r)}}),caught=false;try{Reflect.construct(RegExp,[pattern],target)}catch(e){caught=e===token}caught&&seen==='MP'");
    boolean("let pattern={get [Symbol.match](){return true},get source(){return 5e-324},get flags(){let garbage=[];for(let i=0;i<32;i++)garbage.push({i});return undefined}};new RegExp(pattern).source==='5e-324'");
}
TEST_F(JsMvpLmd, RegExpConstructorNativeSlotsSurvivePublicMutation) {
    boolean("let source='a/b',flags='mi',r=new RegExp(source,flags);Object.defineProperty(r,'source',{get(){throw 31}});Object.defineProperty(r,'flags',{get(){throw 32}});Object.setPrototypeOf(r,{});let garbage=[];for(let i=0;i<32;i++)garbage.push({i});let copy=new RegExp(r);copy.source==='a\\\\/b'&&copy.flags==='im'&&copy.test('a/b')");
    boolean("let make=()=>/ab/g,a=make(),b=make();Object.defineProperty(a,'source',{value:'bad'});Object.defineProperty(a,'flags',{value:'i'});let first=new RegExp(a),second=new RegExp(b);first.source==='ab'&&first.flags==='g'&&second.source==='ab'&&second.flags==='g'&&first!==second");
    boolean("class Child extends RegExp{}let value=new Child('ab','gi'),copy=new RegExp(value);value instanceof Child&&value instanceof RegExp&&copy.source==='ab'&&copy.flags==='gi'&&Object.getPrototypeOf(copy)===RegExp.prototype");
}
TEST_F(JsMvpLmd, RegExpCreateDoesNotRepeatConstructorObservation) {
    boolean("let count=0,value={get [Symbol.match](){count++;return false},toString(){return 'a'}};let iterator='a'.matchAll(value),first=iterator.next();count===1&&!first.done&&first.value[0]==='a'&&iterator.next().done");
    boolean("let seen='',value={get [Symbol.match](){seen+='M';return true},flags:'g',get [Symbol.matchAll](){seen+='A';return undefined},get source(){throw 7},toString(){seen+='S';return 'a'}};let first='a'.matchAll(value).next();seen==='MAS'&&!first.done&&first.value[0]==='a'");
    boolean("let seen='',value={get [Symbol.match](){seen+='M';return undefined},toString(){seen+='S';return 'a'}};let result='a'.match(value);seen==='MS'&&result[0]==='a'");
}

TEST_F(JsMvpLmd, ObjectSubclassIgnoresValueArgument) {
    boolean(R"JS(
function check(x){if(!x)throw 'Object subclass argument'}
let source={value:3};class O extends Object {}
let direct=new O(source),reflected=Reflect.construct(Object,[source],O);
check(direct!==source&&reflected!==source&&!Object.hasOwn(direct,'value')&&!Object.hasOwn(reflected,'value'));
check(Object.getPrototypeOf(direct)===O.prototype&&Object.getPrototypeOf(reflected)===O.prototype);
check(direct instanceof O&&direct instanceof Object&&reflected instanceof O);
class Explicit extends Object {constructor(value){super(value);this.own=7}}
let explicit=new Explicit(source);check(explicit.own===7&&!Object.hasOwn(explicit,'value'));
check(Object(source)===source&&new Object(source)===source&&typeof Object(1)==='object');true;
)JS");
}

TEST_F(JsMvpLmd, ObjectNewTargetPrototypeEffects) {
    boolean(R"JS(
function check(x){if(!x)throw 'Object newTarget effects'}
let seen=[],prototype={marker:1},mode=0,marker={};function Target(){}
let target=new Proxy(Target,{get(owner,key,receiver){if(key==='prototype'){seen.push(key);if(mode===2)throw marker;return mode===1?3:prototype}return Reflect.get(owner,key,receiver)}});
let poison={valueOf(){throw 'converted argument'},toString(){throw 'converted argument'}};
let object=Reflect.construct(Object,[poison],target);check(Object.getPrototypeOf(object)===prototype&&seen.length===1);
mode=1;let fallback=Reflect.construct(Object,[poison],target);check(Object.getPrototypeOf(fallback)===Object.prototype&&seen.length===2);
mode=2;let caught=false;try{Reflect.construct(Object,[poison],target)}catch(e){caught=e===marker}check(caught&&seen.length===3);true;
)JS");
}

TEST_F(JsMvpLmd, OrdinaryNewTargetUsesSharedAllocation) {
    boolean(R"JS(
function check(x){if(!x)throw 'ordinary newTarget allocation'}
function Source(){this.value=4}function Target(){}
let first=Reflect.construct(Source,[],Target);check(first.value===4&&Object.getPrototypeOf(first)===Target.prototype&&first instanceof Target);
Target.prototype=1;let fallback=Reflect.construct(Source,[],Target);check(fallback.value===4&&Object.getPrototypeOf(fallback)===Object.prototype);
let marker={},target=new Proxy(Target,{get(owner,key,receiver){if(key==='prototype')throw marker;return Reflect.get(owner,key,receiver)}});
let caught=false;try{Reflect.construct(Source,[],target)}catch(e){caught=e===marker}check(caught);true;
)JS");
}

TEST_F(JsMvpLmd, ObjectNativeNewTargetDoesNotCreateInternalSlots) {
    boolean(R"JS(
function check(x){if(!x)throw 'ordinary native newTarget brand'}
let fakeDate=Reflect.construct(Object,[],Date);check(Object.getPrototypeOf(fakeDate)===Date.prototype&&Object.prototype.toString.call(fakeDate)==='[object Object]');
let rejected=false;try{Date.prototype.getTime.call(fakeDate)}catch(e){rejected=e instanceof TypeError}check(rejected);
let fakeArray=Reflect.construct(Object,[],Array);check(Object.getPrototypeOf(fakeArray)===Array.prototype&&!Array.isArray(fakeArray));
function Source(){}let ordinary=Reflect.construct(Source,[],Date);rejected=false;try{Date.prototype.getTime.call(ordinary)}catch(e){rejected=e instanceof TypeError}check(rejected);true;
)JS");
}

TEST_F(JsMvpLmd, ObjectNativeDescendantNewTargetDoesNotCreateSlots) {
    boolean(R"JS(
function check(x){if(!x)throw 'native descendant slots'}
let calls=0;class D extends Date {constructor(value){super(value);calls++}}class DD extends D {}
class A extends Array {}class AA extends A {}class T extends Uint8Array {}class TT extends T {}
function Source(){this.ordinary=1}
let constructors=[D,DD,A,AA,T,TT];
for(let i=0;i<constructors.length;i++){
  let target=constructors[i],object=Reflect.construct(Object,[{payload:9}],target),ordinary=Reflect.construct(Source,[],target);
  check(Object.getPrototypeOf(object)===target.prototype&&Object.getPrototypeOf(ordinary)===target.prototype);
  check(object instanceof target&&ordinary instanceof target&&!Object.hasOwn(object,'payload')&&ordinary.ordinary===1);
  check(!Array.isArray(object)&&!Array.isArray(ordinary)&&!ArrayBuffer.isView(object)&&!ArrayBuffer.isView(ordinary));
  if(i<2){let a=false,b=false;try{Date.prototype.getTime.call(object)}catch(e){a=e instanceof TypeError}try{Date.prototype.getTime.call(ordinary)}catch(e){b=e instanceof TypeError}check(a&&b)}
  if(i>=4){let a=false,b=false;try{Uint8Array.prototype.set.call(object,[])}catch(e){a=e instanceof TypeError}try{Uint8Array.prototype.set.call(ordinary,[])}catch(e){b=e instanceof TypeError}check(a&&b)}
}
check(calls===0);let realDate=new DD(7),realTyped=new TT(2);
check(calls===1&&realDate.getTime()===7&&realDate instanceof DD&&realTyped.length===2&&ArrayBuffer.isView(realTyped)&&realTyped instanceof TT);true;
)JS");
}

TEST_F(JsMvpLmd, ObjectOrdinaryDescendantKeepsPrototypeWithoutFields) {
    boolean(R"JS(
function check(x){if(!x)throw 'ordinary descendant fields'}
let effects=0;class O extends Object {constructor(){super();effects++;this.own=3}}class P extends O {}
let object=Reflect.construct(Object,[],P);check(effects===0&&!Object.hasOwn(object,'own')&&Object.getPrototypeOf(object)===P.prototype&&object instanceof P&&object instanceof O);
let normal=new P();check(effects===1&&normal.own===3&&normal instanceof P&&normal instanceof O);true;
)JS");
}

TEST_F(JsMvpLmd, RegExpLegacyIncompleteEscapesRestoreParserPosition) {
    boolean("/^\\x$/.test('x')&&/^\\x1Z$/.test('x1Z')&&/^\\u$/.test('u')&&/^\\u12Z$/.test('u12Z')");
    boolean("/^[\\x]$/.test('x')&&/^[\\u]$/.test('u')&&/^[a-\\x]$/.test('x')&&/^[a-\\u]$/.test('u')");
    boolean("/^(?=x)\\x1Z$/.test('x1Z')&&/^(?=u)\\u12Z$/.test('u12Z')&&/^(?=x)[\\x]$/.test('x')&&/^(?=u)[a-\\u]$/.test('u')");
}
TEST_F(JsMvpLmd, RegExpLegacyBracesRemainQuantifiers) {
    boolean("/^\\x{2}$/.test('xx')&&/^\\u{2}$/.test('uu')&&/^(?=x)\\x{2}$/.test('xx')&&/^(?=u)\\u{2}$/.test('uu')");
    boolean("/^\\u{41}$/.test('u'.repeat(41))&&/^[\\u{2}]+$/.test('u{2}')&&/^[\\x{2}]+$/.test('x{2}')");
}
TEST_F(JsMvpLmd, RegExpIdentityKDependsOnNamedCaptureGrammar) {
    boolean("/^\\k<x>$/.test('k<x>')&&/^(?=k)\\k<x>$/.test('k<x>')&&/^\\k$/.test('k')&&/(?<x>a)\\k<x>/.test('aa')");
    boolean("let failures=0;for(let pattern of ['(?<x>a)\\\\k<y>','(?<x>a)\\\\k','(?<x>a)\\\\k<>']){try{new RegExp(pattern)}catch(e){if(e instanceof SyntaxError)failures++}}failures===3");
}
TEST_F(JsMvpLmd, RegExpUnicodeEscapesStayStrict) {
    boolean("let failures=0;for(let pattern of ['\\\\x','\\\\x1Z','\\\\u','\\\\u12Z','[\\\\x]','[a-\\\\u]','\\\\x{2}','\\\\k<x>']){try{new RegExp(pattern,'u')}catch(e){if(e instanceof SyntaxError)failures++}}failures===8");
    boolean("/^\\x41$/.test('A')&&/^\\u0041$/.test('A')&&/^\\u{41}$/u.test('A')&&/^(?=A)\\u{41}$/u.test('A')&&/^\\cA$/.test(String.fromCharCode(1))&&/^[\\cA]$/.test(String.fromCharCode(1))");
}

TEST_F(JsMvpLmd, ModuleQuotedImportsKeepDecodedLiveBindings) {
    create_dir("temp/mvp_modules");
    write_text_file("temp/mvp_modules/quoted-names.js", "let value=5;export {value as '',value as '☿',value as '\\u{d83d}\\u{de00}'};export function bump(){value++}");
    module("import {'' as empty,'\\u263f' as planet,'\\uD83D\\uDE00' as face,bump} from './quoted-names.js';if(empty!==5||planet!==5||face!==5)throw 1;bump();if(empty!==6||planet!==6||face!==6)throw 2;", "temp/mvp_modules/quoted-names-main.js");
}
TEST_F(JsMvpLmd, ModuleQuotedNamespaceAndReexportsUseDecodedNames) {
    create_dir("temp/mvp_modules");
    write_text_file("temp/mvp_modules/quoted-base.js", "const value=11;export {value as '☿'};");
    write_text_file("temp/mvp_modules/quoted-reexport.js", "export {'☿' as 'renamed'} from './quoted-base.js';export * as 'all names' from './quoted-base.js';export * as default from './quoted-base.js';");
    module("import ns,{'renamed' as value,'all names' as all} from './quoted-reexport.js';if(value!==11||all['☿']!==11||all!==ns)throw 1;", "temp/mvp_modules/quoted-reexport-main.js");
}
TEST_F(JsMvpLmd, ModuleNamedAsyncDeclarationsReuseFunctionInstantiation) {
    create_dir("temp/mvp_modules");
    write_text_file("temp/mvp_modules/export-async.js", "export async function value(){return await 17}export async function* stream(){yield await 19}");
    module("import {value,stream} from './export-async.js';if(await value()!==17)throw 1;let it=stream(),item=await it.next();if(item.done||item.value!==19||(await it.next()).done!==true)throw 2;", "temp/mvp_modules/export-async-main.js");
}
TEST_F(JsMvpLmd, ModuleExportNameEarlyErrorsRemainSyntaxFailures) {
    create_dir("temp/mvp_modules");
    const char* sources[] = {
        "import {'name'} from './unreached.js';",
        "const name=1;export {'name'};",
        "const name=1;export {'name' as alias};",
        "import {#private as local} from './unreached.js';",
        "export * as #private from './unreached.js';",
        "export {#private} from './unreached.js';",
        "import {name as \\u0069f} from './unreached.js';",
        "import \\u0069f from './unreached.js';",
        "import * as \\u0069f from './unreached.js';",
        "import {name as await} from './unreached.js';",
        "import {name as eval} from './unreached.js';",
        "const name=1;export {name as '\\uD83D'};",
        "import {'\\uDC00' as local} from './unreached.js';",
        "export * as '\\uD83D' from './unreached.js';",
        "export {'\\uDC00' as name} from './unreached.js';",
        "export async\nfunction value(){}",
    };
    for (const char* source : sources) {
        MvpLmdFailurePhase phase=MVP_LMD_SUCCESS;
        Item result=module(source,"temp/mvp_modules/quoted-invalid.js",&phase);
        EXPECT_EQ(get_type_id(result),LMD_TYPE_ERROR) << source;
        EXPECT_EQ(phase,MVP_LMD_PARSE_FAILURE) << source;
    }
}

TEST_F(JsMvpLmd, DynamicFieldNestedOrdinaryEval) {
    boolean(R"JS(
function check(value){if(!value)throw 'ordinary field eval'}
let C=Function('return class {field=function(){return eval("arguments[0]")};static field=function(){return eval("arguments[0]")};nested=function(){return ()=>eval("arguments[0]")}}')();
let c=new C();check(c.field(4)===4&&C.field(5)===5&&c.nested(6)()===6);true;
)JS");
}

TEST_F(JsMvpLmd, DynamicFieldLexicalArrowEvalRestriction) {
    boolean(R"JS(
function check(value){if(!value)throw 'arrow field eval'}
let C=Function('return class {field=()=>eval("arguments");nested=()=>()=>eval("arguments");static field=()=>eval("arguments")}')();
let c=new C(),a=false,b=false,d=false;try{c.field()}catch(e){a=e instanceof SyntaxError}try{c.nested()()}catch(e){b=e instanceof SyntaxError}try{C.field()}catch(e){d=e instanceof SyntaxError}check(a&&b&&d);true;
)JS");
}

TEST_F(JsMvpLmd, ColdPropertyReadRetainsGetterAndProxySemantics) {
    boolean("const S=String;let hits=0,object={get x(){hits++;return this===object?7:0}};function choose(flag){return flag?object:'x'}let value=choose(true);value.x===7&&hits===1");
    boolean("const S=String;let seen='',target={x:7},proxy=new Proxy(target,{get(t,k,r){seen+=k;return r===proxy?Reflect.get(t,k,r):0}});function choose(flag){return flag?proxy:[]};let value=choose(true);value.x===7&&seen==='x'");
    boolean("const S=String;let token={keep:17},object={get x(){let garbage=[];for(let i=0;i<32;i++)garbage.push({i});throw token}};function choose(flag){return flag?object:[]};let value=choose(true),caught=false;try{value.x}catch(e){caught=e===token&&e.keep===17}caught");
    boolean("const S=String;let seen='',object={get run(){seen+='g';return function(x){return this===object&&x===3}}};function choose(flag){return flag?object:'x'}let value=choose(true);value.run((seen+='a',3))&&seen==='ga'");
}
TEST_F(JsMvpLmd, ColdPropertyReadPreservesScalarAndSequenceOwnership) {
    boolean("const S=String;let object={x:5e-324};function choose(flag){return flag?object:[]};let value=choose(true),saved=value.x;Object.defineProperty(object,'x',{value:7});let garbage=[];for(let i=0;i<32;i++)garbage.push({i});saved===5e-324&&value.x===7");
    boolean("const S=String;let object={get x(){'use strict';return this}};Object.defineProperty(Number.prototype,'x',Object.getOwnPropertyDescriptor(object,'x'));function choose(flag){return flag?5e-324:'s'}let value=choose(true),saved=value.x;let garbage=[];for(let i=0;i<32;i++)garbage.push({i});saved===5e-324");
    boolean("const S=String;let array=[5e-324];function choose(flag){return flag?array:'ab'}let value=choose(true),saved=value['0'];value[0]=7;let garbage=[];for(let i=0;i<32;i++)garbage.push({i});saved===5e-324&&value.length===1");
    boolean("const S=String;function choose(flag){return flag?'\\uD83D\\uDE00':[]}let value=choose(true);value.length===2&&value['0']==='\\uD83D'&&value['1']==='\\uDE00'&&value['2']===undefined");
    boolean("let key=Symbol('read'),object={[key]:9};function choose(flag){return flag?object:'ab'}let value=choose(true);value[key]===9");
}
TEST_F(JsMvpLmd, ColdPropertyReadLeavesDeferredKeysAndNullishOrder) {
    boolean("const S=String;let seen='',object={get '0.25'(){seen+='g';return 9}};function choose(flag){return flag?object:[]};function key(){seen+='k';return 0.25}let value=choose(true);value[key()]===9&&seen==='kg'");
    boolean("const S=String;let seen='';function choose(flag){return flag?null:[]}function key(){seen+='k';return 0.25}let value=choose(true),caught=false;try{value[key()]}catch(e){caught=e instanceof TypeError}caught&&seen==='k'");
    boolean("const S=String;let seen='',object={x:9};function choose(flag){return flag?object:[]}let value=choose(true),key={toString(){seen+='k';return 'x'}};value[key]===9&&seen==='k'");
}
TEST_F(JsMvpLmd, ColdPropertyReadKeepsWarmShapeInvalidation) {
    boolean("const S=String;let object={x:1};function choose(flag){return flag?object:'s'}let value=choose(true),sum=0;for(let i=0;i<4;i++){if(i===2)Object.defineProperty(object,'x',{get(){return 3},configurable:true});sum+=value.x}sum===8");
    boolean("const S=String;let object={x:1};function read(value){return value.x}let a=read(object);Object.defineProperty(object,'x',{get(){return 7}});a===1&&read(object)===7");
}
TEST_F(JsMvpLmd, ColdPropertyReadKeepsLegacyArrayNumBounds) {
    boolean("const S=String;function choose(flag){return flag?new Int32Array(2):'ab'}let value=choose(true);value[0]=-2147483648;value[1]=2147483647;typeof value['0']==='number'&&value['0']===-2147483648&&value['1']===2147483647");
    boolean("const S=String;function choose(flag){return flag?new Uint8Array(2):'ab'}let value=choose(true);value[0]=255;typeof value['0']==='number'&&value['0']===255&&value['1']===0&&value['2']===undefined&&value['-0']===undefined");
    boolean("const S=String;function choose(flag){return flag?new Int32Array(2):'ab'}let value=choose(true);value[0]=7;value.length===2&&value['0']===7&&value['2']===undefined&&value['-0']===undefined");
    boolean("const S=String;function choose(flag){return flag?new Float64Array(2):'ab'}let value=choose(true);value[0]=5e-324;let saved=value['0'];value[0]=7;saved===5e-324&&value.length===2");
}
TEST_F(JsMvpLmd, ColdPropertyReadKeepsFunctionLowering) {
    boolean("const S=String;function choose(flag){return flag?Object.create({x:7}):'s'}function read(value){return value.x}let value=choose(true);value.x===7&&read(value)===7");
    char* mir = dump("temp/mvp_cold_property.mir");
    ASSERT_NE(mir, nullptr);
    EXPECT_NE(strstr(mir, "\timport\tmvp_lmd_property_read"), nullptr);
    const char* body = strstr(mir, "mvp_lmd_f2:\tfunc");
    ASSERT_NE(body, nullptr);
    const char* end = strstr(body, "\tendfunc");
    ASSERT_NE(end, nullptr);
    const char* outlined = strstr(body, "mvp_lmd_property_read");
    EXPECT_TRUE(!outlined || outlined > end);
    mem_free(mir);
}

TEST_F(JsMvpLmd, DynamicFieldCallBoundaryOrdinary) {
    boolean(R"JS(
function check(value){if(!value)throw 'field ordinary call boundary'}
let C=Function('return class { immediate=(function(a){return eval("arguments[0]")})(7); escaped=(function(a){return ()=>eval("arguments[0]")})(8); method=this.read(9); read(a){return eval("arguments[0]")} constructed=new (function(a){this.value=eval("arguments[0]")})(10); static immediate=(function(a){return eval("arguments[0]")})(11); }')();
let c=new C();check(c.immediate===7&&c.escaped()===8&&c.method===9&&c.constructed.value===10&&C.immediate===11);true;
)JS");
}

TEST_F(JsMvpLmd, DynamicFieldCallBoundaryExternalArrow) {
    boolean(R"JS(
function check(value){if(!value)throw 'external lexical arrow'}
let external=(function(a){return ()=>eval('arguments[0]')})(12);
let C=Function('external','return class { value=external(); nested=Function(\'return ()=>eval("arguments[0]")\')(13); static value=external(); }')(external);
let c=new C();check(c.value===12&&C.value===12&&c.nested()===13);true;
)JS");
}

TEST_F(JsMvpLmd, DynamicFieldCallBoundaryNativeForwarders) {
    boolean(R"JS(
function check(value){if(!value)throw 'field callback forwarding'}
let C=Function('return class { field=(function(a){return eval("arguments[0]")}).call(null,14); bound=(function(a){return ()=>eval("arguments[0]")}).bind(null,15)(); getter=(function(){let object={};Object.defineProperty(object,"value",{get:function(){return eval("arguments.length")}});return object.value})(); applied=(function(a){return eval("arguments[0]")}).apply(null,[16]); }')();
let c=new C();check(c.field===14&&c.bound()===15&&c.getter===0&&c.applied===16);true;
)JS");
}

TEST_F(JsMvpLmd, DynamicFieldCallBoundaryRestoresInitializer) {
    boolean(R"JS(
function check(value){if(!value)throw 'restored field context'}
let C=Function('return class { field=(()=>{try{(function(){throw 1})()}catch(e){} return ()=>eval("arguments");})(); next=()=>eval("arguments"); }')();
let c=new C(),first=false,second=false;try{c.field()}catch(e){first=e instanceof SyntaxError}try{c.next()}catch(e){second=e instanceof SyntaxError}check(first&&second);true;
)JS");
}

TEST_F(JsMvpLmd, DynamicFieldCallBoundaryIndirectEval) {
    boolean(R"JS(
function check(value){if(!value)throw 'indirect eval field context'}
let own=Object.hasOwn(globalThis,'arguments'),before=globalThis.arguments;globalThis.arguments=17;
let C=Function('return class { field=(0,eval)(\'()=>eval("arguments")\'); restricted=()=>eval("arguments"); }')();
let c=new C();check(c.field()===17);let restricted=false;try{c.restricted()}catch(e){restricted=e instanceof SyntaxError}check(restricted);
if(own)globalThis.arguments=before;else delete globalThis.arguments;true;
)JS");
}

TEST_F(JsMvpLmd, DynamicFieldCallBoundaryPrivateMethod) {
    boolean(R"JS(
function check(value){if(!value)throw 'field private method context'}
let C=Function('return class { #value=18; field=this.read(1); read(a){return eval("this.#value+arguments[0]")} }')();
check(new C().field===19);true;
)JS");
}

TEST_F(JsMvpLmd, DynamicObjectMethodsAndNames) {
    boolean(R"JS(
function check(value){if(!value)throw 'object method metadata'}
let object=Function(`return {plain(a){return [a,this]},['computed'](a,b){return a+b},[Symbol.iterator](){return this},normal:function named(){return 3}}`)();
check(object.plain.name==='plain'&&object.plain.length===1&&object.computed.name==='computed'&&object.computed.length===2&&object[Symbol.iterator].name==='[Symbol.iterator]');
let d=Object.getOwnPropertyDescriptor(object,'plain');check(d.writable&&d.enumerable&&d.configurable&&!Object.hasOwn(object.plain,'prototype'));
let rejected=false;try{new object.plain()}catch(e){rejected=e instanceof TypeError}check(rejected&&object.plain(4)[0]===4&&object.plain(4)[1]===object&&object.computed(2,3)===5&&object.normal.name==='named');true;
)JS");
}

TEST_F(JsMvpLmd, DynamicObjectAccessorDescriptors) {
    boolean(R"JS(
function check(value){if(!value)throw 'object accessors'}
let symbol=Symbol('key');let object=Function('symbol',`return {value:1,get value(){return this.saved===undefined?2:this.saved},set value(v){this.saved=v},get [symbol](){return this.saved},set [symbol](v){this.saved=v+1},get reset(){return 8},reset:9}`)(symbol);
let d=Object.getOwnPropertyDescriptor(object,'value');check(d.enumerable&&d.configurable&&d.get.name==='get value'&&d.get.length===0&&d.set.name==='set value'&&d.set.length===1&&!Object.hasOwn(d,'value'));
check(object.value===2);object.value=4;check(object.value===4&&object[symbol]===4);object[symbol]=6;check(object.value===7&&object.reset===9);
let s=Object.getOwnPropertyDescriptor(object,symbol),r=Object.getOwnPropertyDescriptor(object,'reset');check(s.get.name==='get [key]'&&s.set.name==='set [key]'&&r.value===9&&!Object.hasOwn(r,'get'));true;
)JS");
}

TEST_F(JsMvpLmd, DynamicObjectPrototypeLiteralForms) {
    boolean(R"JS(
function check(value){if(!value)throw 'object prototype literal'}
let base={value:3};let result=Function('base',`let __proto__=4;return [{__proto__:base},{'__proto__':null},{__proto__:7},{['__proto__']:5},{__proto__},{__proto__(){return 6}},{get __proto__(){return 8}}]`)(base);
check(Object.getPrototypeOf(result[0])===base&&result[0].value===3&&!Object.hasOwn(result[0],'__proto__'));
check(Object.getPrototypeOf(result[1])===null&&Object.getPrototypeOf(result[2])===Object.prototype);
check(result[3].__proto__===5&&Object.hasOwn(result[3],'__proto__')&&result[4].__proto__===4&&Object.hasOwn(result[4],'__proto__'));
check(result[5].__proto__()===6&&result[6].__proto__===8&&Object.getPrototypeOf(result[5])===Object.prototype);true;
)JS");
}

TEST_F(JsMvpLmd, DynamicObjectMethodSuperAndEval) {
    boolean(R"JS(
function check(value){if(!value)throw 'object method super'}
let base={get value(){return this.saved},set value(v){this.saved=v+1}};
let object=Function('base',`return {__proto__:base,saved:2,read(){return super.value},write(v){super.value=v},arrow(){return ()=>super.value},fromEval(){return eval('super.value')},computed(k){return super[k]}}`)(base);
check(object.read()===2&&object.fromEval()===2&&object.computed('value')===2);object.write(4);check(object.saved===5);
let other={saved:8},arrow=object.arrow.call(other);check(object.read.call(other)===8&&object.fromEval.call(other)===8&&arrow()===8);
Object.setPrototypeOf(object,{value:9});check(object.read()===9&&object.fromEval()===9&&arrow()===9);true;
)JS");
}

TEST_F(JsMvpLmd, DynamicObjectPropertyEffectOrder) {
    boolean(R"JS(
function check(value){if(!value)throw 'object property order'}
let result=Function(`let seen='',key={toString(){seen+='k';return 'value'}};let object={[(seen+='a',key)]: (seen+='b',1),get [(seen+='c','value')](){return 2},[(seen+='d','method')](){return seen},last:(seen+='e',3)};return [object,seen]`)();
check(result[1]==='akbcde'&&result[0].value===2&&result[0].method()==='akbcde'&&result[0].last===3);
let marker={},caught=false;try{Function('marker',`return { [{toString(){throw marker}}](){return 1} }`)(marker)}catch(e){caught=e===marker}check(caught);true;
)JS");
}

TEST_F(JsMvpLmd, DynamicObjectEscapedMethodHome) {
    boolean(R"JS(
function check(value){if(!value)throw 'escaped object home'}
let read=Function(`let count=2,object={__proto__:{value:3},method(){count++;return ()=>super.value+count}};return object.method`)();
let a=read(),b=read();check(a()===7&&b()===7);let values=[];for(let i=0;i<40;i++)values.push({i:i});check(a()===7&&read()()===8&&b()===8);true;
)JS");
}

TEST_F(JsMvpLmd, DynamicObjectGeneratorMethods) {
    boolean(R"JS(
function check(value){if(!value)throw 'object generator home'}
let object=Function(`return {__proto__:{value:5},*run(a){yield super.value+a;return eval('super.value')},get value(){return 1}}`)();
let g=object.run(2),first=g.next(),last=g.next();check(first.value===7&&!first.done&&last.value===5&&last.done&&object.run.name==='run');true;
)JS");
}

TEST_F(JsMvpLmd, DynamicObjectAsyncMethods) {
    expect_async_flags(run(R"JS(
let out=[];let object=Function(`return {__proto__:{value:6},async run(a){await 0;return super.value+a},async *iterate(){yield eval('super.value');await 0;return super.value+1}}`)();
object.run(2).then(v=>out.push(v===8),()=>out.push(false));let g=object.iterate();g.next().then(first=>g.next().then(last=>out.push(first.value===6&&!first.done&&last.value===7&&last.done),()=>out.push(false)),()=>out.push(false));out;
)JS"),2);
}

TEST_F(JsMvpLmd, HostedForInUsesExistingKeysAndPrototypeOrder) {
    boolean(R"JS(
        let keys=Function('object','let out="";for(let key in object)out+=key+",";return out');
        let proto={2:1,z:2},object=Object.create(proto);object[10]=3;object[1]=4;object.a=5;
        Object.defineProperty(object,'z',{value:6,enumerable:false});object[Symbol('hidden')]=7;
        keys(object)==='1,10,a,2,'&&keys(null)===''&&keys(undefined)===''&&keys('ab')==='0,1,'
    )JS");
}
TEST_F(JsMvpLmd, HostedForInSkipsDeletedAndNonEnumerableKeys) {
    boolean(R"JS(
        let object={a:1,b:2,c:3},reads=0;
        Object.defineProperty(object,'c',{enumerable:false});
        Object.defineProperty(object,'g',{get(){reads++;throw 7},enumerable:true});
        let enumerate=Function('object','let out="";for(let key in object){out+=key;if(key==="a")delete object.b}return out');
        enumerate(object)==='ag'&&reads===0
    )JS");
}
TEST_F(JsMvpLmd, HostedForInSnapshotDoesNotUseArrayIterator) {
    boolean(R"JS(
        let iterator=Array.prototype[Symbol.iterator],prototype=Object.getPrototypeOf([][Symbol.iterator]());
        let close=Object.getOwnPropertyDescriptor(prototype,'return'),reads=0,marker={};
        Object.defineProperty(prototype,'return',{configurable:true,get(){reads++;throw marker}});
        Array.prototype[Symbol.iterator]=function(){reads++;throw marker};
        let ok=false;
        try {
            ok=Function('object','for(let key in object){break}return true')({a:1})&&
                Function('object','for(let key in object){return key}')({b:1})==='b'&&reads===0;
        } finally {
            Array.prototype[Symbol.iterator]=iterator;
            if(close)Object.defineProperty(prototype,'return',close);else delete prototype.return;
        }
        ok
    )JS");
}
TEST_F(JsMvpLmd, HostedForInPreservesProxyDescriptorException) {
    boolean(R"JS(
        let marker={payload:[5e-324,-0]},seen=0;
        let object=new Proxy({a:1,b:2},{getOwnPropertyDescriptor(target,key){
            if(seen&&key==='b')throw marker;
            return Object.getOwnPropertyDescriptor(target,key);
        }});
        let caught=false;
        try {Function('object','step','for(let key in object)step()')(object,()=>{seen++;new Array(100)})}
        catch(error){caught=error===marker&&error.payload[0]===5e-324&&1/error.payload[1]===-Infinity}
        caught&&seen===1
    )JS");
}
TEST_F(JsMvpLmd, HostedForInPreservesProxyPrototypeException) {
    boolean(R"JS(
        let marker={},seen=0,proto={b:2},target=Object.create(proto);target.a=1;target.b=3;
        let object=new Proxy(target,{getPrototypeOf(){if(seen)throw marker;return proto}});
        let caught=false;
        try {Function('object','step','for(let key in object)step()')(object,()=>{seen++;delete target.b})}
        catch(error){caught=error===marker}
        caught&&seen===1
    )JS");
}
TEST_F(JsMvpLmd, HostedForInRetainsIterationCellsAndCompletion) {
    boolean(R"JS(
        let make=Function('object','let out=[];for(let key in object)out.push(()=>key);return out');
        let cells=make({a:1,b:2,c:3});
        let labels=Function('object','let out="";outer:for(let key in object){for(let inner in {x:1,y:2}){if(inner==="x")continue;out+=key;break outer}}return out');
        cells[0]()==='a'&&cells[1]()==='b'&&cells[2]()==='c'&&labels({a:1,b:2})==='a'&&
            eval('2;for(var key in {a:1}){3;}')===3&&
            eval('2;for(var key in {a:1}){3;break;}')===3&&
            eval('2;for(var key in {}){3;}')===undefined
    )JS");
}
TEST_F(JsMvpLmd, HostedForInPreservesAbruptHeadAndBody) {
    boolean(R"JS(
        let marker={},holder={},first=false,second=false;
        Object.defineProperty(holder,'value',{set(value){new Array(100);throw marker}});
        try {Function('holder','for(holder.value in {a:1}){}')(holder)}catch(error){first=error===marker}
        try {Function('marker','for(let key in {a:1})throw marker')(marker)}catch(error){second=error===marker}
        first&&second
    )JS");
}
TEST_F(JsMvpLmd, HostedForInOwnsSnapshotAcrossSuspension) {
    expect_async_flags(run(R"JS(
        let out=[],A=Object.getPrototypeOf(async function(){}).constructor;
        let body=A('object','let out=[];for(let key in object){await 0;out.push(()=>key)}return out');
        body({a:1,b:2}).then(cells=>out.push(cells.length===2&&cells[0]()==='a'&&cells[1]()==='b'));
        let G=Object.getPrototypeOf(function*(){}).constructor;
        let iterator=G('object','for(let key in object)yield ()=>key')({a:1,b:2});
        let first=iterator.next().value,second=iterator.next().value;
        out.push(first()==='a'&&second()==='b'&&iterator.next().done);out
    )JS"),2);
}
TEST_F(JsMvpLmd, HostedForInRetainsDeepInheritedKeys) {
    boolean(R"JS(
        let object={x:1};for(let i=0;i<70;i++)object=Object.create(object);
        Function('object','let out="";for(let key in object)out+=key;return out')(object)==='x'
    )JS");
}

TEST_F(JsMvpLmd, ForInPrimitivePrototypeKeysUseExistingBoxing) {
    boolean(R"JS(
        String.prototype.extra=1;Number.prototype.extra=1;Boolean.prototype.extra=1;Symbol.prototype.extra=1;BigInt.prototype.extra=1;
        let values=['ab',3,true,Symbol('x'),1n],expected=['01extra','extra','extra','extra','extra'],ok=true;
        let hosted=Function('value','let out="";for(let key in value)out+=key;return out');
        for(let i=0;i<values.length;i++){
            let seen='';for(let key in values[i])seen+=key;
            ok=ok&&seen===expected[i]&&hosted(values[i])===expected[i];
        }
        ok
    )JS");
}
TEST_F(JsMvpLmd, ForInAbsentProxyKeysDoNotShadowInheritedKeys) {
    boolean(R"JS(
        let proto={x:1};
        let object=new Proxy(Object.create(proto),{ownKeys(){return ['x']},getOwnPropertyDescriptor(){return undefined}});
        let seen='';for(let key in object)seen+=key;
        let hosted=Function('object','let out="";for(let key in object)out+=key;return out');
        seen==='x'&&hosted(object)==='x'
    )JS");
}

TEST_F(JsMvpLmd, SharedMethodEarlyErrorsRejectBeforeExecution) {
    const char* sources[] = {
        "class C {constructor(){super();}}",
        "class A{} class B extends A {m(){super();}}",
        "class A{} class B extends A {m(x=super()){}}",
        "class A{} class B extends A {static m(){super();}}",
        "({m(){super();}});",
        "({m(x=super()) {}});",
        "class A{} class B extends A {m(){return ()=>super();}}",
        "class A{} class B extends A {constructor(){function inner(){super();}super();}}",
        "class A{} class B extends A {m(){return function(){return super.value;};}}",
        "class C {constructor(){} constructor(){}}",
        "class C {constructor(){} \"constructor\"(){}}",
        "class C {get constructor(){return 1;}}",
        "class C {set constructor(value){}}",
        "class C {*constructor(){}}",
        "class C {async constructor(){}}",
        "class C {async *constructor(){}}",
    };
    for (const char* source : sources) {
        SCOPED_TRACE(source);
        mvp_lmd_destroy(execution);
        execution = mvp_lmd_execute(source, strlen(source));
        MvpLmdFailurePhase phase;
        const char* type = NULL;
        ASSERT_NE(mvp_lmd_diagnostic(execution, &phase, &type), nullptr);
        EXPECT_EQ(phase, MVP_LMD_PARSE_FAILURE);
        EXPECT_STREQ(type, "SyntaxError");
    }
}

TEST_F(JsMvpLmd, SharedSuperValidationDerivedAndLexicalArrows) {
    boolean(R"JS(
class Base {constructor(value){this.value=value;}}
class Direct extends Base {constructor(){super(3);}}
class Arrow extends Base {constructor(){let initialize=()=>super(4);initialize();}}
class Nested extends Base {constructor(){super(5);function make(){return class extends Base {constructor(){super(6);}};}this.other=new (make())();}}
new Direct().value===3&&new Arrow().value===4&&new Nested().other.value===6;
)JS");
}

TEST_F(JsMvpLmd, SharedSuperValidationDerivedParameters) {
    boolean(R"JS(
let Derived=Function(`return class extends class {constructor(value){this.value=value;}} {constructor(value=super(7)){this.seen=value===this;}}`)();
let instance=new Derived();instance.value===7&&instance.seen;
)JS");
}

TEST_F(JsMvpLmd, SharedSuperValidationMethodHomes) {
    boolean(R"JS(
class Base {get value(){return this.saved;}}
class Derived extends Base {constructor(){super();this.saved=8;}read(){return super.value;}arrow(){return ()=>super.value;}}
let instance=new Derived(),object={__proto__:{value:9},read(){return super.value;},arrow(){return ()=>super.value;}};
instance.read()===8&&instance.arrow()()===8&&object.read()===9&&object.arrow()()===9;
)JS");
}

TEST_F(JsMvpLmd, SharedSuperValidationConstructorSpelling) {
    boolean(R"JS(
class StaticMethod {static constructor(){return 1;}}
class StaticGetter {static get constructor(){return 2;}}
class Computed {['constructor'](){return 3;}}
class ComputedGetter {get ['constructor'](){return 4;}}
StaticMethod.constructor()===1&&StaticGetter.constructor===2&&new Computed().constructor()===3&&new ComputedGetter().constructor===4;
)JS");
}

TEST_F(JsMvpLmd, SharedSuperValidationEvalAndComputedContext) {
    boolean(R"JS(
let Derived=Function(`return class extends class {constructor(value){this.value=value;}get base(){return this.value;}} {
constructor(){let Inner=class {[eval('()=>super(10)')().value](){return 11;}};this.inner=new Inner();}
read(){return eval('()=>super.base')();}
}`)();
let instance=new Derived();instance.value===10&&instance.read()===10&&instance.inner[10]()===11;
)JS");
}

TEST_F(JsMvpLmd, ClassStrictScopeNestedOrdinaryFunctions) {
    boolean(R"JS(
class Named {constructor(){this.read=function(){return this;};}method(){return function(){return this;};}}
let instance=new Named();instance.read.call(null)===null&&instance.method().call(undefined)===undefined;
)JS");
}

TEST_F(JsMvpLmd, ClassStrictScopeAnonymousHeritageAndKeys) {
    boolean(R"JS(
let heritage=class extends function(){return this;} {};
let keyFunction;let keyed=class {[(keyFunction=function(){return this;},'read')](){return 1;}};
Object.getPrototypeOf(heritage).call(undefined)===undefined&&keyFunction.call(null)===null&&new keyed().read()===1;
)JS");
}

TEST_F(JsMvpLmd, ClassStrictScopeDoesNotLeakOutward) {
    boolean(R"JS(
let object={read(){return this;}},before=function(){return this;};
let cls=class {read(){return this;}};let after=function(){return this;};
let method=cls.prototype.read;
object.read.call(undefined)===globalThis&&before.call(null)===globalThis&&after.call(null)===globalThis&&method.call(null)===null;
)JS");
}

TEST_F(JsMvpLmd, ClassStrictScopeArgumentsPoison) {
    boolean(R"JS(
class C {constructor(){this.read=function(){return arguments.callee;};}read(){return arguments.callee;}}
let instance=new C(),first=false,second=false;
try{instance.read();}catch(e){first=e instanceof TypeError;}
try{C.prototype.read();}catch(e){second=e instanceof TypeError;}
first&&second;
)JS");
}

TEST_F(JsMvpLmd, ForInPrimitiveSnapshotsKeepNullishAndDeletionSemantics) {
    boolean(R"JS(
        String.prototype.extra=1;Number.prototype.extra=1;
        let values=[null,undefined,'ab',3],seen=[];
        for(let i=0;i<values.length;i++){
            let keys='';for(let key in values[i]){keys+=key;if(key==='0')delete String.prototype.extra;}
            seen.push(keys);
        }
        seen.join('|')==='||01|extra'
    )JS");
}

TEST_F(JsMvpLmd, RegExpIndicesReusePairsAndCaptureOffsets) {
    boolean("let m=/(?<a>a)(b)?/d.exec('za');m.index===1&&m.indices.length===3&&m.indices[0][0]===1&&m.indices[0][1]===2&&m.indices[1][0]===1&&m.indices[1][1]===2&&m.indices[2]===undefined&&m.indices.groups.a===m.indices[1]");
    boolean("let m=/(?<a>a)?(?<b>b)/d.exec('b');m.indices.groups.a===undefined&&m.indices.groups.b===m.indices[2]&&Object.getPrototypeOf(m.indices.groups)===null");
    boolean("let m=/(a)(b)/d.exec('zab');m.indices.groups===undefined&&m.indices[0][0]===1&&m.indices[2][1]===3&&Object.getPrototypeOf(m.indices)===Array.prototype&&Object.getPrototypeOf(m.indices[0])===Array.prototype");
    boolean("let r=/a/gd;r.lastIndex=1;let m=r.exec('baa');m.indices[0][0]===1&&r.lastIndex===2&&r.hasIndices&&r.flags==='dg'&&!Object.hasOwn(/a/.exec('a'),'indices')");
}
TEST_F(JsMvpLmd, RegExpIndicesUtf16AndProtocolResults) {
    boolean("let m=/(?<a>.)/du.exec('\\uD83D\\uDE00x');m.indices[0][0]===0&&m.indices[0][1]===2&&m.indices.groups.a===m.indices[1]&&m.indices[1][1]===2");
    boolean("let m=/(.)(.)/d.exec('\\uD83D\\uDE00');m.indices[0][1]===2&&m.indices[1][0]===0&&m.indices[1][1]===1&&m.indices[2][0]===1&&m.indices[2][1]===2");
    boolean("let m='x\\uD83D\\uDE00'.match(/(?<a>.)$/du);m.indices[0][0]===1&&m.indices[0][1]===3&&m.indices.groups.a===m.indices[1]");
    boolean("let iter='aba'.matchAll(/(?<a>a)/dg),a=iter.next().value,b=iter.next().value;a.indices[0][0]===0&&b.indices[0][0]===2&&a.indices.groups.a===a.indices[1]&&iter.next().done");
}
TEST_F(JsMvpLmd, RegExpIndicesOwnDescriptorsAndRetention) {
    boolean("let m=/(?<__proto__>a)/d.exec('a'),d=Object.getOwnPropertyDescriptor(m,'indices'),p=Object.getOwnPropertyDescriptor(m.indices,'0'),g=Object.getOwnPropertyDescriptor(m.indices,'groups');d.writable&&d.enumerable&&d.configurable&&p.writable&&p.enumerable&&p.configurable&&g.writable&&g.enumerable&&g.configurable&&Object.getPrototypeOf(m.indices.groups)===null&&m.indices.groups.__proto__===m.indices[1]");
    boolean("let hits=0;Object.defineProperty(Array.prototype,'indices',{set(){hits++;throw 1},configurable:true});let m=/a/d.exec('a');delete Array.prototype.indices;hits===0&&Object.hasOwn(m,'indices')&&m.indices[0][1]===1");
    boolean("let m=/(?<a>a)(b)?/d.exec('a'),pair=m.indices.groups.a;let garbage=[];for(let i=0;i<64;i++)garbage.push({i,text:'discard'+i});pair===m.indices[1]&&pair[0]===0&&pair[1]===1&&m.indices[2]===undefined");
}
TEST_F(JsMvpLmd, RegExpCompileUsesNativeOriginalSlots) {
    boolean("let source=/a/g,target=/z/;Object.defineProperty(source,'source',{value:'b'});Object.defineProperty(source,'flags',{value:'i'});target.compile(source);target.source==='a'&&target.flags==='g'&&target.test('a')");
    boolean("let r=/a/g,seen=0;Object.defineProperty(r,Symbol.match,{get(){seen++;throw 1}});Object.defineProperty(r,'constructor',{get(){seen++;throw 2}});Object.defineProperty(r,'source',{get(){seen++;throw 3}});let out=/z/;out.compile(r);out.source==='a'&&out.flags==='g'&&seen===0");
    boolean("let r=/a/g;r.lastIndex=9;let result=r.compile(r);result===r&&r.source==='a'&&r.flags==='g'&&r.lastIndex===0");
    boolean("let a=/a/,b=/a/;a.compile('b');a.test('b')&&b.test('a')&&!b.test('b')");
}
TEST_F(JsMvpLmd, RegExpCompileCoercionAndThrows) {
    boolean("let seen='',r=/z/,p={toString(){seen+='p';return 'abc'+String.fromCharCode(100)}},f={toString(){seen+='f';let a=[];for(let i=0;i<32;i++)a.push({i});return 'gi'}};r.compile(p,f);seen==='pf'&&r.source==='abcd'&&r.flags==='gi'&&r.test('ABCD')");
    boolean("let token={},seen=0,r=/z/,caught=false;try{r.compile({toString(){throw token}},{toString(){seen++;return 'g'}})}catch(e){caught=e===token}caught&&seen===0&&r.source==='z'");
    boolean("let token={},r=/z/,caught=false;try{r.compile('a',{toString(){throw token}})}catch(e){caught=e===token}caught&&r.source==='z'");
    boolean("let r=/z/,seen=0,caught=false;try{r.compile(/a/,{toString(){seen++;return 'g'}})}catch(e){caught=e instanceof TypeError}caught&&seen===0&&r.source==='z'");
}
TEST_F(JsMvpLmd, RegExpCompilePreservesPropertiesAndFailureOrder) {
    boolean("let r=/old/;Object.defineProperty(r,'lastIndex',{value:45,writable:false});let caught=false;try{r.compile('new','gi')}catch(e){caught=e instanceof TypeError}caught&&r.source==='new'&&r.flags==='gi'&&r.lastIndex===45");
    boolean("let r=/a/g;r.lastIndex=2;let caught=false;try{r.compile('[')}catch(e){caught=e instanceof SyntaxError}caught&&r.source==='a'&&r.flags==='g'&&r.lastIndex===2");
    boolean("let r=/a/g;r.lastIndex=2;let caught=false;try{r.compile('b','gg')}catch(e){caught=e instanceof SyntaxError}caught&&r.source==='a'&&r.lastIndex===2");
    boolean("let r=/a/;Object.defineProperty(r,'source',{value:'visible',writable:false,configurable:false});r.extra=17;r.compile('b','g');r.source==='visible'&&r.extra===17&&r.flags==='g'&&r.test('b')");
    boolean("let r=/z/;for(let i=0;i<24;i++){r.compile(i%2?'a':'b',i%2?'g':'i');if(!r.test(i%2?'a':'B'))throw 1}r.source==='a'&&r.flags==='g'");
}
TEST_F(JsMvpLmd, RegExpCompileBrandAndMetadata) {
    boolean("let method=RegExp.prototype.compile,desc=Object.getOwnPropertyDescriptor(RegExp.prototype,'compile');method.name==='compile'&&method.length===2&&desc.writable&&!desc.enumerable&&desc.configurable");
    boolean("let f=RegExp.prototype.compile,ok=0;try{f.call(RegExp.prototype,'a')}catch(e){if(e instanceof TypeError)ok++}try{f.call(Object.create(RegExp.prototype),'a')}catch(e){if(e instanceof TypeError)ok++}try{f.call(new Proxy(/a/,{}),'b')}catch(e){if(e instanceof TypeError)ok++}ok===3");
    boolean("let r=/z/;r.compile();r.source==='(?:)'&&r.flags===''&&r.lastIndex===0");
    boolean("let r=/z/,p={[Symbol.match]:true,source:'wrong',flags:'g',toString(){return 'actual'}};r.compile(p,'i');r.source==='actual'&&r.flags==='i'");
}

TEST_F(JsMvpLmd, DynamicNominalParentSurvivesDetachedLinks) {
    boolean(R"JS(
let Derived=Function(`return class extends class {constructor(value){this.value=value;}} {}`)();
Object.setPrototypeOf(Derived,Function.prototype);Object.setPrototypeOf(Derived.prototype,null);
let allocations=[];for(let i=0;i<80;i++)allocations.push({value:i});
let value=Reflect.construct(Object,[],Derived);
Object.getPrototypeOf(value)===Derived.prototype&&Object.getPrototypeOf(Derived)===Function.prototype&&Object.getPrototypeOf(Derived.prototype)===null;
)JS");
}

TEST_F(JsMvpLmd, DynamicNominalParentSurvivesInheritedChain) {
    boolean(R"JS(
let Derived=Function(`return class extends class extends class {constructor(value){this.value=value;}} {} {}`)();
let allocations=[];for(let i=0;i<80;i++)allocations.push({value:i});
let value=new Derived(9),parent=Object.getPrototypeOf(Derived),base=Object.getPrototypeOf(parent);
value.value===9&&Object.getPrototypeOf(value)===Derived.prototype&&Object.getPrototypeOf(Derived.prototype)===parent.prototype&&Object.getPrototypeOf(parent.prototype)===base.prototype;
)JS");
}

// requires the existing lambda/runtime/module_registry.h header.
TEST_F(JsMvpLmd, ModuleRegistryCleanupRetiresEveryDescriptorRoot) {
    boolean("Promise.resolve(1);true");
    ASSERT_NE(context, nullptr);
    ASSERT_NE(context->heap, nullptr);
    gc_heap_t* gc = context->heap->gc;
    ASSERT_NE(gc, nullptr);
    int original_roots = gc->root_slot_count;
    Runtime owner = {};
    owner.eval_context = (EvalContext*)context;
    const char* path = "temp/mvp_registry_cleanup.js";
    module_register_for_runtime(&owner, path, "js", ItemNull, nullptr);
    ModuleDescriptor* descriptor = module_get_for_runtime(&owner, path);
    ASSERT_NE(descriptor, nullptr);
    descriptor->cjs_module = mvp_lmd_array_new(0);
    ASSERT_EQ(get_type_id(descriptor->cjs_module), LMD_TYPE_ARRAY);
    uintptr_t cjs_slot = (uintptr_t)&descriptor->cjs_module.item;
    bool registered = false;
    for (int index = 0; index < gc->root_slot_count; index++)
        registered |= (uintptr_t)gc->root_slots[index] == cjs_slot;
    ASSERT_TRUE(registered);
    module_registry_cleanup_for_runtime(&owner);
    EXPECT_EQ(owner.module_registry, nullptr);
    // inspect addresses before collection so a stale native slot fails deterministically.
    for (int index = 0; index < gc->root_slot_count; index++)
        ASSERT_NE((uintptr_t)gc->root_slots[index], cjs_slot);
    ASSERT_EQ(gc->root_slot_count, original_roots);
    heap_gc_collect();
    EXPECT_EQ(gc->root_slot_count, original_roots);
}

TEST_F(JsMvpLmd, RegExpQuantifierValidationRejectsUnattachedAndReversed) {
    boolean("let bad=['?','*','+','a**','a+*','a??*','{2}','a{1}{2}','.{2,1}','a{02,001}'];let ok=true;for(let p of bad){for(let f of ['', 'u']){let caught=false;try{new RegExp(p,f)}catch(e){caught=e instanceof SyntaxError}ok=ok&&caught}}ok;");
    boolean("let r=/test262/ig;let ok=true;for(let p of ['?','.{2,1}']){r.lastIndex=3;let caught=false;try{r.compile(p)}catch(e){caught=e instanceof SyntaxError}ok=ok&&caught&&r.source==='test262'&&r.flags==='gi'&&r.lastIndex===3;r.lastIndex=0;ok=ok&&r.test('TEsT262')}ok;");
}

TEST_F(JsMvpLmd, RegExpQuantifierValidationPreservesAnnexBAndLargeBounds) {
    boolean("let patterns=['{','a{','a{1','a{1,','a{1,x}','a{2,1x}'];let ok=true;for(let p of patterns)ok=ok&&new RegExp(p).test(p);ok&&/\\?\\*\\+/.test('?*+')&&/[?*+]/.test('+')&&/a??/.exec('a')[0]===''&&/a{0001,02}/.exec('aa')[0]==='aa';");
    boolean("let p=['b{4294967297}','b{4294967297,}','b{4294967297,4294967298}','b{0004294967297,4294967297}'];let ok=true;for(let source of p){let r=new RegExp(source);ok=ok&&r.source===source&&!r.test('')}ok;");
}

TEST_F(JsMvpLmd, RegExpEscapedCaptureNamesUseSharedDecoder) {
    boolean("let r=/(?<\\u{03C0}>a)\\k<\\u03C0>/u;let m=r.exec('baa');m[0]==='aa'&&m.groups['π']==='a'&&r.source==='(?<\\\\u{03C0}>a)\\\\k<\\\\u03C0>';");
    boolean("let r=/(?<\\u{03C0}>.)/du;let m=r.exec('😀');m.groups['π']==='😀'&&m.indices[0][0]===0&&m.indices[0][1]===2&&m.indices.groups['π']===m.indices[1]&&m.indices[1][1]===2;");
    boolean("let r=new RegExp('(?<\\\\u{1D49C}>a)\\\\k<\\\\u{1D49C}>','d');let m=r.exec('baa');m.groups['𝒜']==='a'&&m.indices.groups['𝒜']===m.indices[1]&&m.indices.groups['𝒜'][0]===1&&m.indices.groups['𝒜'][1]===2;");
    boolean("let r=/(?<\\uD835\\uDC9C>a)\\k<\\u{1D49C}>/u;let m=r.exec('baa');m.groups['𝒜']==='a'&&m[0]==='aa';");
    boolean("let m=/(?<_\\u200C>a)(?<_\\u200D>b)(?<\\u0024>c)/du.exec('abc');m.groups['_\\u200C']==='a'&&m.groups['_\\u200D']==='b'&&m.groups.$==='c'&&m.indices.groups.$===m.indices[3];");
}

TEST_F(JsMvpLmd, RegExpEscapedNameNormalizationPreservesBodyAndOriginals) {
    boolean("let source='(?<\\\\u0061>\\\\u{3})';let r=new RegExp(source);let m=r.exec('uuu');let clone=new RegExp(r);let other=/z/;other.compile(r);m.groups.a==='uuu'&&r.source===source&&clone.source===source&&other.source===source&&clone.test('uuu')&&other.test('uuu');");
    boolean("let r=/(?<\\u0061>\\u{61})[\\u0062](?<=ab)/u;let m=r.exec('ab');m.groups.a==='a'&&m[0]==='ab'&&r.source==='(?<\\\\u0061>\\\\u{61})[\\\\u0062](?<=ab)';");
    boolean("let bad=['(?<\\\\u{100000061}>a)','(?<\\\\u{110000}>a)','(?<\\\\u{}>a)','(?<\\\\u00ZZ>a)','(?<\\\\u0030>a)','(?<\\\\u0061>x)(?<a>y)','(?<\\\\u0061>a)\\\\k<\\\\u0062>'];let ok=true;for(let p of bad){for(let f of ['', 'u']){let caught=false;try{new RegExp(p,f)}catch(e){caught=e instanceof SyntaxError}ok=ok&&caught}}ok;");
    boolean("let p='(?<\\\\u{00000000061}>a)';let r=new RegExp(p,'u');r.source===p&&r.exec('a').groups.a==='a';");
}

TEST_F(JsMvpLmd, RegExpEscapedNameNormalizationRetainsRootedResults) {
    boolean("let saved=[];for(let i=0;i<12;i++){let p='(?<\\\\u{03C0}>'+i+')';let r=new RegExp(p,'du');let m=r.exec('x'+i);saved.push([r,m,p])}let ok=true;for(let i=0;i<saved.length;i++){let row=saved[i];let m=row[1];ok=ok&&row[0].source===row[2]&&m.groups['π']===''+i&&m.indices.groups['π']===m.indices[1]&&m.indices[1][0]===1&&m.indices[1][1]===(''+i).length+1}ok;");
}

TEST_F(JsMvpLmd, RegExpQuantifierBoundsComparedBeforeSaturation) {
    boolean("let ok=true;for(let flags of ['', 'u']){let caught=false;try{new RegExp('.{4294967297,4294967296}',flags)}catch(e){caught=e instanceof SyntaxError}ok=ok&&caught}ok;");
}

TEST_F(JsMvpLmd, StringHtmlAttributes) {
    boolean(R"JS(
let text='<&"x',arg='"a&\'"';
if(text.anchor(arg)!=='<a name="&quot;a&\'&quot;"><&"x</a>')throw 1;
if(text.link(arg)!=='<a href="&quot;a&\'&quot;"><&"x</a>')throw 2;
if(text.fontcolor(arg)!=='<font color="&quot;a&\'&quot;"><&"x</font>')throw 3;
if(text.fontsize(arg)!=='<font size="&quot;a&\'&quot;"><&"x</font>')throw 4;
if(''.anchor()!=='<a name="undefined"></a>')throw 5;
if('x'.fontsize(null)!=='<font size="null">x</font>')throw 6;
true;
)JS");
}

TEST_F(JsMvpLmd, StringHtmlCoercionOrder) {
    boolean(R"JS(
let order='',receiver={ [Symbol.toPrimitive](hint){order+='r'+hint;return 'body'} };
let arg={ [Symbol.toPrimitive](hint){order+='a'+hint;return '"'} };
let value=String.prototype.anchor.call(receiver,arg);
if(value!=='<a name="&quot;">body</a>'||order!=='rstringastring')throw 1;
let marker={},caught=false,reads=0;
try{String.prototype.link.call(null,{toString(){reads++;return 'x'}})}catch(e){caught=e instanceof TypeError}
if(!caught||reads!==0)throw 2;
caught=false;
try{String.prototype.anchor.call({toString(){throw marker}},arg)}catch(e){caught=e===marker}
if(!caught||order!=='rstringastring')throw 3;
caught=false;
try{'x'.link({toString(){throw marker}})}catch(e){caught=e===marker}
if(!caught)throw 4;
caught=false;
try{'x'.fontcolor(Symbol())}catch(e){caught=e instanceof TypeError}
if(!caught)throw 5;
true;
)JS");
}

TEST_F(JsMvpLmd, StringHtmlSimpleWrappers) {
    boolean(R"JS(
let names=['big','blink','bold','fixed','italics','small','strike','sub','sup'];
let tags=['big','blink','b','tt','i','small','strike','sub','sup'];
let text='<&"x',ignored={toString(){throw 1}};
for(let i=0;i<names.length;i++) {
    if(String.prototype[names[i]].call(text,ignored)!=='<'+tags[i]+'>'+text+'</'+tags[i]+'>')throw 2;
}
true;
)JS");
}

TEST_F(JsMvpLmd, StringLocaleText) {
    boolean(R"JS(
if('ß\ufb01'.toLocaleUpperCase()!=='SSFI'||'\uD801\uDC28'.toLocaleUpperCase()!=='\uD801\uDC00')throw 1;
if('é'.localeCompare('e\u0301')!==0||'abc'.localeCompare('abd')>=0||'abd'.localeCompare('abc')<=0)throw 2;
if('undefined'.localeCompare()!==0)throw 3;
let order='',receiver={toString(){order+='r';return 'abc'}},other={toString(){order+='a';return 'abc'}};
if(String.prototype.localeCompare.call(receiver,other)!==0||order!=='ra')throw 4;
let marker={},caught=false;
try{'x'.localeCompare({toString(){throw marker}})}catch(e){caught=e===marker}if(!caught)throw 5;
if(String.prototype.toLocaleUpperCase.call({toString(){return 'ß'}})!=='SS')throw 6;
true;
)JS");
}

TEST_F(JsMvpLmd, StringNormalizeEmptyFormOrder) {
    boolean(R"JS(
let order='',receiver={toString(){order+='r';return ''}},form={toString(){order+='f';return 'invalid'}};
let caught=false;try{String.prototype.normalize.call(receiver,form)}catch(e){caught=e instanceof RangeError}
if(!caught||order!=='rf')throw 1;
let marker={};caught=false;
try{''.normalize({toString(){throw marker}})}catch(e){caught=e===marker}if(!caught)throw 2;
caught=false;try{''.normalize(Symbol())}catch(e){caught=e instanceof TypeError}if(!caught)throw 3;
let calls=0;
if(''.normalize({toString(){calls++;return 'NFC'}})!==''||calls!==1)throw 4;
caught=false;calls=0;
try{String.prototype.normalize.call(null,{toString(){calls++;return 'NFC'}})}catch(e){caught=e instanceof TypeError}
if(!caught||calls!==0)throw 5;
true;
)JS");
}

TEST_F(JsMvpLmd, StringNormalizeForms) {
    boolean(R"JS(
if('e\u0301'.normalize()!=='\u00e9'||'e\u0301'.normalize(undefined)!=='\u00e9')throw 1;
if('\u00e9'.normalize('NFD')!=='e\u0301')throw 2;
if('\ufb01'.normalize('NFKC')!=='fi'||'\ufb01'.normalize('NFKD')!=='fi')throw 3;
if('\ufb01'.normalize('NFC')!=='\ufb01')throw 4;
let order='',receiver={toString(){order+='r';return '\u00e9'}},form={toString(){order+='f';return 'NFD'}};
if(String.prototype.normalize.call(receiver,form)!=='e\u0301'||order!=='rf')throw 5;
if('\uD800'.normalize('NFC')!=='\uD800')throw 6;
true;
)JS");
}

TEST_F(JsMvpLmd, StringTextMethodMetadata) {
    boolean(R"JS(
let names=['anchor','big','blink','bold','fixed','fontcolor','fontsize','italics','link','small','strike','sub','sup','normalize','isWellFormed','toWellFormed','localeCompare','toLocaleUpperCase'];
let lengths=[1,0,0,0,0,1,1,0,1,0,0,0,0,0,0,0,1,0];
for(let i=0;i<names.length;i++) {
    let name=names[i],f=String.prototype[name],d=Object.getOwnPropertyDescriptor(String.prototype,name);
    let n=Object.getOwnPropertyDescriptor(f,'name'),l=Object.getOwnPropertyDescriptor(f,'length');
    if(f.name!==name||f.length!==lengths[i]||!d.writable||d.enumerable||!d.configurable)throw 1;
    if(n.writable||n.enumerable||!n.configurable||l.writable||l.enumerable||!l.configurable)throw 2;
    if(Object.hasOwn(f,'prototype'))throw 3;
    let caught=false;try{Reflect.construct(f,[])}catch(e){caught=e instanceof TypeError}if(!caught)throw 4;
}
if(String.prototype.toLocaleUpperCase===String.prototype.toUpperCase)throw 5;
true;
)JS");
}

TEST_F(JsMvpLmd, StringTextCoercionRoots) {
    boolean(R"JS(
let order='',source={toString(){order+='r';return ['e','\u0301'].join('')}};
let argument={toString(){order+='a';let garbage=[];for(let i=0;i<48;i++)garbage.push({text:'q'.repeat(i+1)});return 'NFC'}};
if(String.prototype.normalize.call(source,argument)!=='é'||order!=='ra')throw 1;
order='';argument.toString=function(){order+='a';let garbage=[];for(let i=0;i<48;i++)garbage.push([i,'x'.repeat(i+1)]);return '"';};
if(String.prototype.link.call(source,argument)!=='<a href="&quot;">e\u0301</a>'||order!=='ra')throw 2;
order='';argument.toString=function(){order+='a';let garbage=[];for(let i=0;i<48;i++)garbage.push({value:i});return 'é'};
if(String.prototype.localeCompare.call(source,argument)!==0||order!=='ra')throw 3;
true;
)JS");
}

TEST_F(JsMvpLmd, StringWellFormedCoercion) {
    boolean(R"JS(
let count=0,receiver={toString(){count++;return '\uD800'}};
if(String.prototype.isWellFormed.call(receiver)!==false||String.prototype.toWellFormed.call(receiver)!=='\uFFFD'||count!==2)throw 1;
if(String.prototype.isWellFormed.call(42)!==true||String.prototype.toWellFormed.call(true)!=='true')throw 2;
let marker={},caught=false;
try{String.prototype.toWellFormed.call({toString(){throw marker}})}catch(e){caught=e===marker}if(!caught)throw 3;
caught=false;try{String.prototype.isWellFormed.call(Symbol())}catch(e){caught=e instanceof TypeError}if(!caught)throw 4;
caught=false;try{String.prototype.toWellFormed.call(undefined)}catch(e){caught=e instanceof TypeError}if(!caught)throw 5;
true;
)JS");
}

TEST_F(JsMvpLmd, StringWellFormedSurrogates) {
    boolean(R"JS(
let high='\uD83D',low='\uDCA9',pair=high+low,replacement='\uFFFD';
if(!''.isWellFormed()||!'a💩c'.isWellFormed()||!pair.isWellFormed())throw 1;
if(high.isWellFormed()||low.isWellFormed()||(low+high).isWellFormed())throw 2;
if(('a'+high+'b'+low+'c').toWellFormed()!=='a'+replacement+'b'+replacement+'c')throw 3;
if((low+high).toWellFormed()!==replacement+replacement)throw 4;
if(pair.toWellFormed()!==pair||pair.slice(0,1).toWellFormed()!==replacement||pair.slice(1).toWellFormed()!==replacement)throw 5;
if(('a\0'+high+low+'é').toWellFormed()!=='a\0💩é')throw 6;
true;
)JS");
}

// append to the existing JsMvpLmd fixture; parent owns normal and forced-GC execution.

TEST_F(JsMvpLmd, ArrayCopyMethodsMetadataAndAdmission) {
    {
        SCOPED_TRACE("direct-and-escaped");
        boolean(R"JS(let a = Array.of(3, 1, 2), copy = a.toSorted(), f = [].with;
let replacement = f.call(a, 1, 9), splice = a.toSpliced(1, 1, 8);
a.length === 3 && a[0] === 3 && copy.join(',') === '1,2,3' && replacement.join(',') === '3,9,2' && splice.join(',') === '3,8,2';)JS");
    }
    {
        SCOPED_TRACE("metadata");
        boolean(R"JS(let names = ['copyWithin', 'toSorted', 'toSpliced', 'with'], lengths = [2, 1, 2, 2], ok = true;
for (let i = 0; i < names.length; i++) {
  let name = names[i], f = Array.prototype[name], d = Object.getOwnPropertyDescriptor(Array.prototype, name);
  ok = ok && typeof f === 'function' && f.name === name && f.length === lengths[i] && d.writable && d.configurable && !d.enumerable && !Object.hasOwn(f, 'prototype');
  let threw = false; try { new f(); } catch (e) { threw = e instanceof TypeError; } ok = ok && threw;
}
let of = Object.getOwnPropertyDescriptor(Array, 'of');
ok && Array.of.name === 'of' && Array.of.length === 0 && of.writable && of.configurable && !of.enumerable && !Object.hasOwn(Array.of, 'prototype');)JS");
    }
    {
        SCOPED_TRACE("unscopables");
        boolean(R"JS(let u = Array.prototype[Symbol.unscopables];
u.copyWithin === true && u.toSorted === true && u.toSpliced === true;)JS");
    }
}

TEST_F(JsMvpLmd, ArrayCopyWithinOverlapAndPresence) {
    {
        SCOPED_TRACE("overlap");
        boolean(R"JS(let a = [1,2,3,4,5], b = [1,2,3,4,5];
let same = a.copyWithin(1,0,4) === a && b.copyWithin(0,2) === b;
same && a.join(',') === '1,1,2,3,4' && b.join(',') === '3,4,5,4,5';)JS");
    }
    {
        SCOPED_TRACE("holes-and-inheritance");
        boolean(R"JS(let a = [1,,3,4]; a.copyWithin(2,0,2);
let p = {1:9}, o = Object.create(p); o.length = 3; o[0] = 1;
let same = Array.prototype.copyWithin.call(o, 2, 1, 2) === o;
same && a.length === 4 && a[2] === 1 && !Object.hasOwn(a,1) && !Object.hasOwn(a,3) && o[2] === 9 && Object.hasOwn(o,2);)JS");
    }
    {
        SCOPED_TRACE("proxy-order");
        boolean(R"JS(let trace = [], target = {length:3,1:7};
let p = new Proxy(target, {
  get(t,k,r) { trace.push('g'+k); return Reflect.get(t,k,r); },
  has(t,k) { trace.push('h'+k); return Reflect.has(t,k); },
  set(t,k,v,r) { trace.push('s'+k); return Reflect.set(t,k,v,r); },
  deleteProperty(t,k) { trace.push('d'+k); return Reflect.deleteProperty(t,k); }
});
let result = Array.prototype.copyWithin.call(p,0,1,3);
result === p && trace.join(',') === 'glength,h1,g1,s0,h2,d1' && target[0] === 7 && !Object.hasOwn(target,1);)JS");
    }
}

TEST_F(JsMvpLmd, ArrayCopyWithinStrictWritesAndAbrupt) {
    {
        SCOPED_TRACE("readonly");
        boolean(R"JS(let objects = [{length:2,1:9}, [1,9]], ok = true;
for (let i=0;i<objects.length;i++) {
  let o = objects[i]; Object.defineProperty(o,'0',{value:1,writable:false});
  let threw=false; try { Array.prototype.copyWithin.call(o,0,1); } catch(e) { threw=e instanceof TypeError; }
  ok = ok && threw && o[0] === 1;
}
ok;)JS");
    }
    {
        SCOPED_TRACE("delete-and-proxy-false");
        boolean(R"JS(let o = {length:2}; Object.defineProperty(o,'0',{value:1,configurable:false});
let deleted=false; try { Array.prototype.copyWithin.call(o,0,1); } catch(e) { deleted=e instanceof TypeError; }
let p = new Proxy({length:2,1:5},{set(){return false}}), set=false;
try { Array.prototype.copyWithin.call(p,0,1); } catch(e) { set=e instanceof TypeError; }
let q = new Proxy({length:2,0:5},{deleteProperty(){return false}}), erase=false;
try { Array.prototype.copyWithin.call(q,0,1); } catch(e) { erase=e instanceof TypeError; }
deleted && set && erase;)JS");
    }
    {
        SCOPED_TRACE("thrown-identity");
        boolean(R"JS(let marker = {}, source = {length:2,1:7}; Object.defineProperty(source,'0',{set(v){throw marker}});
let caught; try { Array.prototype.copyWithin.call(source,0,1); } catch(e) { caught=e; }
caught === marker;)JS");
    }
}

TEST_F(JsMvpLmd, ArrayCopyWithinCoercionAndBoxing) {
    {
        SCOPED_TRACE("ordered-coercion");
        boolean(R"JS(let order=[], o={0:1,1:2,2:3};
Object.defineProperty(o,'length',{get(){order.push('length');return {valueOf(){order.push('len');return 3}}}});
let to={valueOf(){order.push('target');return 1}}, start={valueOf(){order.push('start');o[0]=8;return 0}}, end={valueOf(){order.push('end');return 2}};
Array.prototype.copyWithin.call(o,to,start,end);
order.join(',') === 'length,len,target,start,end' && o[0] === 8 && o[1] === 8 && o[2] === 2;)JS");
    }
    {
        SCOPED_TRACE("snapshot-and-boxing");
        boolean(R"JS(let a=[1,2,3]; a.copyWithin({valueOf(){a.length=1;return 1}},0);
let b=Array.prototype.copyWithin.call(true,0), empty=Array.prototype.copyWithin.call('',0);
let threw=false;try{Array.prototype.copyWithin.call('ab',0,1)}catch(e){threw=e instanceof TypeError}
a.length===2 && a[0]===1 && a[1]===1 && typeof b==='object' && b.valueOf()===true && empty.valueOf()==='' && threw;)JS");
    }
}

TEST_F(JsMvpLmd, ArrayToSortedOrderingAndPresence) {
    {
        SCOPED_TRACE("stable-holes");
        boolean(R"JS(let a=[3,,1,undefined,2], bad=false, calls=0;
let r=a.toSorted((x,y)=>{calls++;bad=bad||x===undefined||y===undefined;return x-y});
let items=[{k:1,id:'a'},{k:0,id:'b'},{k:1,id:'c'},{k:0,id:'d'}];
let sorted=items.toSorted((x,y)=>x.k-y.k);
r.length===5 && r[0]===1 && r[1]===2 && r[2]===3 && r[3]===undefined && r[4]===undefined && Object.hasOwn(r,3) && Object.hasOwn(r,4) && !Object.hasOwn(a,1) && calls>0 && !bad && sorted.map(x=>x.id).join('')==='bdac';)JS");
    }
    {
        SCOPED_TRACE("utf16-default-and-boxing");
        boolean(R"JS(let r=['\uE000','\uD83D\uDE00','a'].toSorted();
let chars=Array.prototype.toSorted.call('cba'), empty=Array.prototype.toSorted.call(9);
r[0]==='a' && r[1]==='\uD83D\uDE00' && r[2]==='\uE000' && chars.join('')==='abc' && empty.length===0;)JS");
    }
    {
        SCOPED_TRACE("read-all-before-comparison");
        boolean(R"JS(let trace=[], o={length:3};
Object.defineProperty(o,'0',{get(){trace.push('g0');return 3}});
Object.defineProperty(o,'1',{get(){trace.push('g1');return 1}});
Object.defineProperty(o,'2',{get(){trace.push('g2');return 2}});
let r=Array.prototype.toSorted.call(o,(a,b)=>{trace.push('c');return a-b});
trace[0]==='g0' && trace[1]==='g1' && trace[2]==='g2' && trace[3]==='c' && r.join(',')==='1,2,3';)JS");
    }
}

TEST_F(JsMvpLmd, ArrayCopyMethodsValidationAndIntrinsicResults) {
    {
        SCOPED_TRACE("comparator-first");
        boolean(R"JS(let read=false, o={get length(){read=true;throw 17}}, caught;
try{Array.prototype.toSorted.call(o,1)}catch(e){caught=e}
caught instanceof TypeError && !read;)JS");
    }
    {
        SCOPED_TRACE("ignore-species-and-constructors");
        boolean(R"JS(let a=[3,1,2], touched=0;
Object.defineProperty(a,'constructor',{get(){touched++;throw 1}});
let b=a.toSorted(), c=a.toSpliced(1,1), d=a.with(0,8);
touched===0 && Object.getPrototypeOf(b)===Array.prototype && Object.getPrototypeOf(c)===Array.prototype && Object.getPrototypeOf(d)===Array.prototype && b.join(',')==='1,2,3' && c.join(',')==='3,2' && d[0]===8;)JS");
    }
    {
        SCOPED_TRACE("own-writes-bypass-prototype-setter");
        boolean(R"JS(let a=[3,1,2], called=0, ok=false;
Object.defineProperty(Array.prototype,'0',{get(){return 99},set(v){called++},configurable:true});
try {
  let b=a.toSorted(), c=a.toSpliced(1,1), d=a.with(0,8), e=Array.of(4);
  ok=called===0 && Object.hasOwn(b,0) && b[0]===1 && c[0]===3 && d[0]===8 && e[0]===4;
} finally { delete Array.prototype[0]; }
ok;)JS");
    }
}

TEST_F(JsMvpLmd, ArrayToSplicedArgumentAndReadOrder) {
    {
        SCOPED_TRACE("argument-count-and-holes");
        boolean(R"JS(let a=[1,,3]; let b=a.toSpliced(), c=a.toSpliced(1), d=a.toSpliced(1,undefined,8), e=a.toSpliced(-1,Infinity,9,10);
b.length===3 && Object.hasOwn(b,1) && b[1]===undefined && !Object.hasOwn(a,1) && c.join(',')==='1' && d.join(',')==='1,8,,3' && Object.hasOwn(d,2) && e.join(',')==='1,,9,10';)JS");
    }
    {
        SCOPED_TRACE("skip-deleted-getters");
        boolean(R"JS(let trace=[], o={length:3};
Object.defineProperty(o,'0',{get(){trace.push('g0');return 1}});
Object.defineProperty(o,'1',{get(){throw 29}});
Object.defineProperty(o,'2',{get(){trace.push('g2');return 3}});
let start={valueOf(){trace.push('start');return 1}}, count={valueOf(){trace.push('count');return 1}};
let r=Array.prototype.toSpliced.call(o,start,count,8);
trace.join(',')==='start,count,g0,g2' && r.join(',')==='1,8,3';)JS");
    }
    {
        SCOPED_TRACE("coercion-abrupt-identity");
        boolean(R"JS(let marker={}, countRead=false, caught;
try{[1].toSpliced({valueOf(){throw marker}},{valueOf(){countRead=true;return 0}})}catch(e){caught=e}
caught===marker && !countRead;)JS");
    }
}

TEST_F(JsMvpLmd, ArrayWithBoundsAndReplacement) {
    {
        SCOPED_TRACE("skip-selected-getter");
        boolean(R"JS(let reads=[], o={length:3};
Object.defineProperty(o,'0',{get(){reads.push(0);return 1}});
Object.defineProperty(o,'1',{get(){throw 71}});
Object.defineProperty(o,'2',{get(){reads.push(2);return 3}});
let r=Array.prototype.with.call(o,-2,8), chars=Array.prototype.with.call('abc',-1,'x');
reads.join(',')==='0,2' && r.join(',')==='1,8,3' && chars.join('')==='abx';)JS");
    }
    {
        SCOPED_TRACE("infinite-and-out-of-range");
        boolean(R"JS(let bad=[Infinity,-Infinity,1e100,-1e100,3,-4], ok=true;
for(let i=0;i<bad.length;i++){let threw=false;try{Array.prototype.with.call({length:3},bad[i],0)}catch(e){threw=e instanceof RangeError}ok=ok&&threw}
let a=[1,,3].with(undefined,9);
ok && a[0]===9 && a[1]===undefined && Object.hasOwn(a,1) && a[2]===3;)JS");
    }
}

TEST_F(JsMvpLmd, ArrayCopyMethodsMaximumLengthFailures) {
    {
        SCOPED_TRACE("limits-before-element-reads");
        boolean(R"JS(let read=false, huge={length:4294967296,get 0(){read=true;throw 42}}, sorted=false, spliced=false, replaced=false, overflow=false;
try{Array.prototype.toSorted.call(huge)}catch(e){sorted=e instanceof RangeError}
try{Array.prototype.toSpliced.call(huge,0,0)}catch(e){spliced=e instanceof RangeError}
try{Array.prototype.with.call(huge,0,1)}catch(e){replaced=e instanceof RangeError}
try{Array.prototype.toSpliced.call({length:9007199254740991},0,0,1)}catch(e){overflow=e instanceof TypeError}
sorted && spliced && replaced && overflow && !read;)JS");
    }
    {
        SCOPED_TRACE("bigint-and-symbol-coercion");
        boolean(R"JS(let ok=true, inputs=[1n,Symbol('x')];
for(let i=0;i<inputs.length;i++){
 let a=false,b=false,c=false;try{[1].with(inputs[i],2)}catch(e){a=e instanceof TypeError}
 try{[1].toSpliced(inputs[i],0)}catch(e){b=e instanceof TypeError}
 try{[1].copyWithin(inputs[i],0)}catch(e){c=e instanceof TypeError}
 ok=ok&&a&&b&&c;
}
ok;)JS");
    }
}

TEST_F(JsMvpLmd, ArrayOfConstructorsAndPropertyCreation) {
    {
        SCOPED_TRACE("constructor-count-and-result");
        boolean(R"JS(let seen=-1, argCount=-1;
function C(n){seen=n;argCount=arguments.length;this.kind='custom'}
let r=Array.of.call(C,4,5,6);
r instanceof C && seen===3 && argCount===1 && r.length===3 && r[0]===4 && r[2]===6;)JS");
    }
    {
        SCOPED_TRACE("create-data-then-set-length");
        boolean(R"JS(let trace=[], p={set 0(v){throw 81},set length(n){trace.push('length'+n)}};
function C(n){trace.push('ctor'+n);return Object.create(p)}
let r=Array.of.call(C,4,5), d=Object.getOwnPropertyDescriptor(r,'0');
trace.join(',')==='ctor2,length2' && Object.hasOwn(r,0) && r[0]===4 && r[1]===5 && !Object.hasOwn(r,'length') && d.writable && d.enumerable && d.configurable;)JS");
    }
    {
        SCOPED_TRACE("nonconstructor-fallback");
        boolean(R"JS(let plain=Array.of.call(null,4), arrow=Array.of.call(()=>{},7), empty=Array.of(), of=Array.of, detached=of(9);
Array.isArray(plain) && plain[0]===4 && Array.isArray(arrow) && arrow[0]===7 && empty.length===0 && detached[0]===9;)JS");
    }
    {
        SCOPED_TRACE("proxy-constructor-and-result");
        boolean(R"JS(let trace=[], result={};
let receiver=new Proxy(function(){},{construct(t,a,n){trace.push('construct'+a[0]);return new Proxy(result,{defineProperty(t,k,d){trace.push('define'+k);return Reflect.defineProperty(t,k,d)},set(t,k,v,r){trace.push('set'+k);return Reflect.set(t,k,v,r)}})}});
let r=Array.of.call(receiver,4,5);
trace.join(',')==='construct2,define0,define1,setlength,definelength' && r[0]===4 && result.length===2;)JS");
    }
}

TEST_F(JsMvpLmd, ArrayOfAbruptCompletion) {
    {
        SCOPED_TRACE("constructor-and-define-throw");
        boolean(R"JS(let marker={}, first,second;
function C(){throw marker}
try{Array.of.call(C,1)}catch(e){first=e}
function D(){return new Proxy({},{defineProperty(){throw marker}})}
try{Array.of.call(D,1)}catch(e){second=e}
first===marker && second===marker;)JS");
    }
    {
        SCOPED_TRACE("rejected-define-and-length");
        boolean(R"JS(function C(){return new Proxy({},{defineProperty(){return false}})}
let define=false;try{Array.of.call(C,1)}catch(e){define=e instanceof TypeError}
function D(){return Object.defineProperty({},'length',{value:0,writable:false})}
let length=false;try{Array.of.call(D,1)}catch(e){length=e instanceof TypeError}
define && length;)JS");
    }
}

TEST_F(JsMvpLmd, ArrayCopyMethodsScalarAndObjectOwnership) {
    {
        SCOPED_TRACE("callback-scalar-retention");
        boolean(R"JS(let small=5e-324, precise=1.0000000000000002, output, source={length:2};
Object.defineProperty(source,'0',{get(){return precise}});
Object.defineProperty(source,'1',{set(v){source.extra=Array.of(small,small,small);output=v}});
Array.prototype.copyWithin.call(source,1,0,1);
let replacement={value:small}, o={length:2,get 0(){let waste=Array.of(precise,precise);return small},get 1(){return precise}};
let a=Array.prototype.toSpliced.call(o,1,0,replacement), b=Array.prototype.with.call(o,1,replacement);
Object.is(output,precise) && Object.is(a[0],small) && a[1]===replacement && Object.is(a[2],precise) && Object.is(b[0],small) && b[1]===replacement;)JS");
    }
    {
        SCOPED_TRACE("constructor-argument-retention");
        boolean(R"JS(let tiny=5e-324, precise=1.0000000000000002, object={tag:'kept'};
function C(n){let waste=Array.of(9,8,7);this.count=n}
let r=Array.of.call(C,tiny,precise,object), a=[object,{tag:'a'},{tag:'z'}];
let sorted=a.toSorted((x,y)=>{let waste=Array.of(tiny,precise);return x.tag<y.tag?-1:x.tag>y.tag?1:0});
Object.is(r[0],tiny) && Object.is(r[1],precise) && r[2]===object && r.count===3 && sorted[0].tag==='a' && sorted[1]===object && sorted[2].tag==='z';)JS");
    }
}

TEST_F(JsMvpLmd, ArrayOfConstructorReturnRootAndFunction) {
    {
        SCOPED_TRACE("function-return");
        boolean(R"JS(let result=function(){}; delete result.length; Object.setPrototypeOf(result,null);
function C(n){return result}
let r=Array.of.call(C,7,8), d=Object.getOwnPropertyDescriptor(r,'0');
r===result && typeof r==='function' && r[0]===7 && r[1]===8 && r.length===2 && d.value===7 && d.writable && d.enumerable && d.configurable;)JS");
    }
    {
        SCOPED_TRACE("proxy-descriptor-reentry");
        boolean(R"JS(let descriptors=[], tiny=5e-324, target={}, marker={};
function C(){return new Proxy(target,{defineProperty(t,k,d){
  if(k!=='length') {descriptors.push(d);for(let i=0;i<24;i++){let waste=Array.of({i:i},tiny)} }
  return Reflect.defineProperty(t,k,d);
}})}
let r=Array.of.call(C,tiny,marker);
r.length===2 && Object.is(r[0],tiny) && r[1]===marker && descriptors.length===2 && Object.is(descriptors[0].value,tiny) && descriptors[1].value===marker && descriptors[0].writable && descriptors[0].enumerable && descriptors[0].configurable;)JS");
    }
}

TEST_F(JsMvpLmd, SharedBufferConstructorAndMetadata) {
    boolean(R"JS(
let b=new SharedArrayBuffer(4),p=SharedArrayBuffer.prototype;
let c=Object.getOwnPropertyDescriptor(SharedArrayBuffer,'prototype'),n=Object.getOwnPropertyDescriptor(SharedArrayBuffer,'name');
let tag=Object.getOwnPropertyDescriptor(p,Symbol.toStringTag),s=Object.getOwnPropertyDescriptor(SharedArrayBuffer,Symbol.species);
b.byteLength===4&&b.maxByteLength===4&&!b.growable&&b instanceof SharedArrayBuffer&&
Object.getPrototypeOf(b)===p&&Object.getPrototypeOf(p)===Object.prototype&&Object.getPrototypeOf(SharedArrayBuffer)===Function.prototype&&
SharedArrayBuffer.length===1&&SharedArrayBuffer.name==='SharedArrayBuffer'&&!c.writable&&!c.enumerable&&!c.configurable&&
n.value==='SharedArrayBuffer'&&!n.writable&&!n.enumerable&&n.configurable&&
tag.value==='SharedArrayBuffer'&&!tag.writable&&!tag.enumerable&&tag.configurable&&
s.get.call(7)===7&&s.set===undefined&&!s.enumerable&&s.configurable&&Object.prototype.toString.call(b)==='[object SharedArrayBuffer]';
    )JS");
    boolean(R"JS(
let p=SharedArrayBuffer.prototype,ok=true;
for(let key of ['byteLength','maxByteLength','growable']){let d=Object.getOwnPropertyDescriptor(p,key);ok=ok&&typeof d.get==='function'&&d.get.length===0&&d.get.name==='get '+key&&!d.enumerable&&d.configurable&&d.set===undefined}
for(let key of ['slice','grow']){let d=Object.getOwnPropertyDescriptor(p,key);ok=ok&&d.writable&&!d.enumerable&&d.configurable&&d.value.name===key;try{new d.value();ok=false}catch(e){ok=ok&&e instanceof TypeError}}
ok&&p.slice.length===2&&p.grow.length===1&&p.constructor===SharedArrayBuffer;
    )JS");
    boolean(R"JS(
let ok=true,coerced=0;for(let v of [undefined,null,false,NaN,-0.9])ok=ok&&new SharedArrayBuffer(v).byteLength===0;
for(let v of [-1,Infinity,-Infinity,1e100])try{new SharedArrayBuffer(v);ok=false}catch(e){ok=ok&&e instanceof RangeError}
try{SharedArrayBuffer({valueOf(){coerced++;return 0}});ok=false}catch(e){ok=ok&&e instanceof TypeError}
for(let v of [1n,Symbol()])try{new SharedArrayBuffer(v);ok=false}catch(e){ok=ok&&e instanceof TypeError}
ok&&coerced===0&&new SharedArrayBuffer('2.9').byteLength===2;
    )JS");
}
TEST_F(JsMvpLmd, SharedBufferConstructorOrderAndNewTarget) {
    boolean(R"JS(
let order='',prototype={},F=(function(){}).bind(null);
Object.defineProperty(F,'prototype',{get(){order+='p';return prototype}});
let b=Reflect.construct(SharedArrayBuffer,[{valueOf(){order+='l';return 2}},{get maxByteLength(){order+='m';return {valueOf(){order+='x';return 6}}}}],F);
let bytes=Object.getOwnPropertyDescriptor(SharedArrayBuffer.prototype,'byteLength').get;
order==='lmxp'&&Object.getPrototypeOf(b)===prototype&&bytes.call(b)===2;
    )JS");
    boolean(R"JS(
let called=0,marker={},F=(function(){}).bind(null),range=false;
Object.defineProperty(F,'prototype',{get(){called++;throw marker}});
try{Reflect.construct(SharedArrayBuffer,[4,{maxByteLength:2}],F)}catch(e){range=e instanceof RangeError}
range&&called===0;
    )JS");
    boolean(R"JS(
let F=(function(){}).bind(null);Object.defineProperty(F,'prototype',{value:7});
let b=Reflect.construct(SharedArrayBuffer,[2],F),marker={},same=false;
try{new SharedArrayBuffer(2,{get maxByteLength(){throw marker}})}catch(e){same=e===marker}
Object.getPrototypeOf(b)===SharedArrayBuffer.prototype&&b.byteLength===2&&same&&
!new SharedArrayBuffer(2,7).growable&&new SharedArrayBuffer(2,{maxByteLength:undefined}).maxByteLength===2;
    )JS");
}
TEST_F(JsMvpLmd, SharedBufferBrandsRemainPhysical) {
    boolean(R"JS(
let a=new ArrayBuffer(8),s=new SharedArrayBuffer(8),errors=0;
for(let key of ['byteLength','maxByteLength','resizable','detached']){let get=Object.getOwnPropertyDescriptor(ArrayBuffer.prototype,key).get;try{get.call(s)}catch(e){if(e instanceof TypeError)errors++}}
for(let key of ['byteLength','maxByteLength','growable']){let get=Object.getOwnPropertyDescriptor(SharedArrayBuffer.prototype,key).get;for(let v of [a,SharedArrayBuffer.prototype,Object.create(SharedArrayBuffer.prototype),new Proxy(s,{})])try{get.call(v)}catch(e){if(e instanceof TypeError)errors++}}
for(let key of ['slice','resize','transfer','transferToFixedLength'])try{ArrayBuffer.prototype[key].call(s)}catch(e){if(e instanceof TypeError)errors++}
try{SharedArrayBuffer.prototype.slice.call(a)}catch(e){if(e instanceof TypeError)errors++}
errors===21&&s.byteLength===8&&a.byteLength===8;
    )JS");
    boolean(R"JS(
let b=new SharedArrayBuffer(8),d=new DataView(b),errors=0;
d.setUint16(2,0x1234,true);for(let value of [SharedArrayBuffer.prototype,Object.create(SharedArrayBuffer.prototype),new Proxy(b,{})])try{new DataView(value)}catch(e){if(e instanceof TypeError)errors++}
errors===3&&d.buffer===b&&d.getUint8(2)===0x34&&d.getUint16(2,true)===0x1234&&ArrayBuffer.isView(d)&&!ArrayBuffer.isView(b);
    )JS");
}
TEST_F(JsMvpLmd, SharedBufferSliceBoundsAndCopies) {
    boolean(R"JS(
let b=new SharedArrayBuffer(6),a=new Uint8Array(b);for(let i=0;i<a.length;i++)a[i]=i+1;
let x=b.slice(-4,-1),y=b.slice(-Infinity,Infinity);new Uint8Array(x)[0]=99;
x.byteLength===3&&new Uint8Array(x)[1]===4&&a[2]===3&&y.byteLength===6&&new Uint8Array(y)[5]===6&&
b.slice(Infinity).byteLength===0&&b.slice(1e100).byteLength===0&&b.slice(-1e100,2).byteLength===2&&
b.slice(NaN,2.9).byteLength===2&&b.slice(4,1).byteLength===0;
    )JS");
    boolean(R"JS(
let b=new SharedArrayBuffer(4,{maxByteLength:8}),a=new Uint8Array(b);a[1]=7;
let copy=b.slice({valueOf(){b.grow(8);a[4]=9;return 1}});
b.byteLength===8&&copy.byteLength===3&&copy.maxByteLength===3&&!copy.growable&&new Uint8Array(copy)[0]===7;
    )JS");
}
TEST_F(JsMvpLmd, SharedBufferSliceSpeciesAndAbrupt) {
    boolean(R"JS(
let b=new SharedArrayBuffer(4),a=new Uint8Array(b),log='',out=new SharedArrayBuffer(6);a[1]=23;
b.constructor={get [Symbol.species](){log+='s';return function(n){log+='c'+n;return out}}};
let copy=b.slice({valueOf(){log+='b';return 1}},{valueOf(){log+='e';return 3}});
copy===out&&log==='besc2'&&new Uint8Array(out)[0]===23&&new Uint8Array(out)[2]===0;
    )JS");
    boolean(R"JS(
let b=new SharedArrayBuffer(4),errors=0;
for(let v of [new ArrayBuffer(4),new SharedArrayBuffer(1),b,{},new Proxy(new SharedArrayBuffer(4),{})]){b.constructor={[Symbol.species]:function(){return v}};try{b.slice(0,2)}catch(e){if(e instanceof TypeError)errors++}}
b.constructor={ [Symbol.species]:null };let copy=b.slice(1);
b.constructor=undefined;let defaultCopy=b.slice(2);
errors===5&&copy instanceof SharedArrayBuffer&&copy.byteLength===3&&defaultCopy.byteLength===2;
    )JS");
    boolean(R"JS(
let marker={},b=new SharedArrayBuffer(2),errors=0;
for(let mode of ['begin','end','constructor','species','construct']){let x=new SharedArrayBuffer(2),first=0,last=2;
if(mode==='begin')first={valueOf(){throw marker}};if(mode==='end')last={valueOf(){throw marker}};
if(mode==='constructor')Object.defineProperty(x,'constructor',{get(){throw marker}});
if(mode==='species')x.constructor={get [Symbol.species](){throw marker}};
if(mode==='construct')x.constructor={[Symbol.species]:function(){throw marker}};
try{x.slice(first,last)}catch(e){if(e===marker)errors++}}
errors===5;
    )JS");
}
TEST_F(JsMvpLmd, SharedBufferGrowthReentryAndBounds) {
    boolean(R"JS(
let b=new SharedArrayBuffer(2,{maxByteLength:8}),a=new Uint8Array(b),called=0;a[0]=11;
let empty=b.grow({valueOf(){called++;b.grow(4);return 6}}),same=b.grow(6),errors=0;
for(let v of [5,9,-1,Infinity,1n,Symbol()])try{b.grow(v)}catch(e){if(e instanceof RangeError||e instanceof TypeError)errors++}
empty===undefined&&same===undefined&&called===1&&errors===6&&b.growable&&b.byteLength===6&&b.maxByteLength===8&&a.length===6&&a[0]===11&&a[5]===0;
    )JS");
    boolean(R"JS(
let b=new SharedArrayBuffer(2,{maxByteLength:10}),threw=false,called=0;
try{b.grow({valueOf(){b.grow(8);return 4}})}catch(e){threw=e instanceof RangeError}
try{new SharedArrayBuffer(2).grow({valueOf(){called++;return 2}})}catch(e){threw=threw&&e instanceof TypeError}
let marker={},same=false;try{b.grow({valueOf(){throw marker}})}catch(e){same=e===marker}
threw&&same&&called===0&&b.byteLength===8&&b.grow({valueOf(){b.grow(9);return 9}})===undefined&&b.byteLength===9;
    )JS");
}
TEST_F(JsMvpLmd, SharedBufferViewsKeepOneHandle) {
    boolean(R"JS(
let b=new SharedArrayBuffer(8,{maxByteLength:24}),fixed=new Uint16Array(b,2,2),tracking=new Uint16Array(b,2),d=new DataView(b,2),fd=new DataView(b,2,2);
let little=new Uint8Array(new Uint16Array([1]).buffer)[0]===1;fixed[0]=1234;b.grow(16);tracking[5]=4321;
fixed.buffer===b&&tracking.buffer===b&&d.buffer===b&&fixed.length===2&&tracking.length===7&&d.byteLength===14&&fd.byteLength===2&&
fixed[0]===1234&&d.getUint16(10,little)===4321&&new Uint8Array(b)[15]===0;
    )JS");
    boolean(R"JS(
let b=new SharedArrayBuffer(8,{maxByteLength:16}),a=new BigInt64Array(b),u=new BigUint64Array(b),d=new DataView(b);
let little=new Uint8Array(new Uint16Array([1]).buffer)[0]===1;a[0]=-2n;b.grow(16);d.setBigUint64(8,18446744073709551615n,little);
a.length===2&&u[0]===18446744073709551614n&&a[1]===-1n&&d.byteLength===16&&d.getBigInt64(0,little)===-2n;
    )JS");
}
TEST_F(JsMvpLmd, SharedBufferSubclassAndInterpretedConstruction) {
    boolean(R"JS(
class C extends SharedArrayBuffer{constructor(n){super(n);this.tag=7}}let b=new C(4),copy=b.slice(1);
b instanceof C&&b instanceof SharedArrayBuffer&&b.byteLength===4&&b.tag===7&&Object.getPrototypeOf(b)===C.prototype&&
Object.getPrototypeOf(C)===SharedArrayBuffer&&Object.getPrototypeOf(C.prototype)===SharedArrayBuffer.prototype&&copy instanceof C&&copy.byteLength===3;
    )JS");
    boolean(R"JS(
let C=Function('return class C extends SharedArrayBuffer { constructor(n){super(n);this.tag=8} }')(),b=new C(3);
b instanceof C&&b instanceof SharedArrayBuffer&&b.byteLength===3&&b.tag===8&&Function('return SharedArrayBuffer')()===SharedArrayBuffer;
    )JS");
}
TEST_F(JsMvpLmd, SharedBufferCoercionRootsSurviveAllocation) {
    boolean(R"JS(
let b=new SharedArrayBuffer(6,{maxByteLength:12}),a=new Uint8Array(b);a[1]=17;
function churn(){let sum=0;for(let i=0;i<40;i++){let x={text:'value'+i,nums:[i+0.125,i+0.25]};sum+=x.nums[0]}return sum}
let first={valueOf(){churn();return 1}},end=3.9999999999;
b.constructor={get [Symbol.species](){churn();return function(n){churn();b.grow(10);return new SharedArrayBuffer(n)}}};
let copy=b.slice(first,end);copy.byteLength===2&&new Uint8Array(copy)[0]===17&&b.byteLength===10;
    )JS");
    boolean(R"JS(
let prototype={marker:7},F=(function(){}).bind(null),order='';
Object.defineProperty(F,'prototype',{get(){order+='p';for(let i=0;i<20;i++)new ArrayBuffer(i);return prototype}});
let b=Reflect.construct(SharedArrayBuffer,[{valueOf(){order+='l';return 8}}],F);
Object.getPrototypeOf(b)===prototype&&b.marker===7&&Object.getOwnPropertyDescriptor(SharedArrayBuffer.prototype,'byteLength').get.call(b)===8&&order==='lp';
    )JS");
}
TEST_F(JsMvpLmd, SharedBufferHostDetachRejectsSharedStorage) {
    MvpLmdHost host = {0, NULL, NULL, true};
    Item result = run(R"JS(
        let b=new SharedArrayBuffer(4),v=new Uint8Array(b),caught=false;v[0]=17;
        try{$262.detachArrayBuffer(b)}catch(e){caught=e instanceof TypeError}
        caught&&b.byteLength===4&&v.length===4&&v[0]===17;
    )JS", &host);
    ASSERT_EQ(get_type_id(result), LMD_TYPE_BOOL);
    EXPECT_EQ(result.item, ITEM_TRUE);
}
TEST_F(JsMvpLmd, SharedBufferSliceRejectsAliasedNativeDataBlock) {
    Item pair = run(R"JS(
        let source=new SharedArrayBuffer(4),alias=new SharedArrayBuffer(4);
        source.constructor={[Symbol.species]:function(){return alias}};
        [source,alias,function(){try{source.slice(0,0)}catch(e){return e instanceof TypeError}return false}];
    )JS");
    ASSERT_EQ(get_type_id(pair), LMD_TYPE_ARRAY);
    RootFrame roots(2);
    ASSERT_TRUE(roots.valid());
    Rooted<Item> owners(roots, pair), result(roots, ItemNull);
    JsArrayBuffer* source = js_get_arraybuffer_ptr_item(owners.get().array->items[0]);
    JsArrayBuffer* alias = js_get_arraybuffer_ptr_item(owners.get().array->items[1]);
    ASSERT_NE(source, nullptr);
    ASSERT_NE(alias, nullptr);
    ASSERT_NE(source, alias);
    // model native interop aliases using the existing retained storage handle.
    byte_buffer_destroy(alias);
    ASSERT_TRUE(byte_buffer_init_storage(alias, source->storage, source->storage_offset,
        source->byte_length, source->max_byte_length, BYTE_BUFFER_FLAG_SHARED, MEM_CAT_JS_RUNTIME));
    ASSERT_EQ(source->storage, alias->storage);
    result.set(mvp_lmd_class_invoke(owners.get().array->items[2],
        Item{.item = ITEM_JS_UNDEFINED}, NULL, 0, Item{.item = ITEM_JS_UNDEFINED}));
    ASSERT_EQ(get_type_id(result.get()), LMD_TYPE_BOOL);
    EXPECT_EQ(result.get().item, ITEM_TRUE);
}
TEST_F(JsMvpLmd, SharedBufferFamilyPlanningIsOptional) {
    numeric("let b=new ArrayBuffer(4);b.byteLength", 4);
    MvpLmdLibraryState* library = mvp_lmd_program_library();
    ASSERT_NE(library, nullptr);
    EXPECT_EQ(library->shared_buffer, nullptr);
    EXPECT_EQ(library->shared_buffer_values, nullptr);
    numeric("let a=new Int32Array(8);a[0]=7;a[0]", 7);
    library = mvp_lmd_program_library();
    EXPECT_TRUE(!library || !library->shared_buffer);
    numeric("new SharedArrayBuffer(6).byteLength", 6);
    library = mvp_lmd_program_library();
    ASSERT_NE(library, nullptr);
    EXPECT_NE(library->shared_buffer, nullptr);
    EXPECT_NE(library->shared_buffer_values, nullptr);
}
TEST_F(JsMvpLmd, SharedBufferPrototypeGetterAbruptPrecedesAllocation) {
    boolean(R"JS(
let called=0,marker={},F=(function(){}).bind(null),same=false;
Object.defineProperty(F,'prototype',{get(){called++;throw marker}});
try{Reflect.construct(SharedArrayBuffer,[4,{maxByteLength:8}],F)}catch(e){same=e===marker}
same&&called===1;
    )JS");
}

TEST_F(JsMvpLmd, StringComputedNormalize) {
    boolean(R"JS(
let key='nor'+'malize';
'e\u0301'[key]()==='é';
)JS");
}

TEST_F(JsMvpLmd, StringComputedWellFormed) {
    boolean(R"JS(
let key='is'+'WellFormed';
'x'[key]()===true&&'\uD800'[key]()===false;
)JS");
}

TEST_F(JsMvpLmd, StringComputedHtml) {
    boolean(R"JS(
let key='an'+'chor';
'x'[key]('"')==='<a name="&quot;">x</a>';
)JS");
}

TEST_F(JsMvpLmd, StringComputedExistingTextMethods) {
    boolean(R"JS(
let trim='tr'+'im',concat='con'+'cat';
' x '[trim]()==='x'&&'a'[concat]('b')==='ab';
)JS");
}

TEST_F(JsMvpLmd, StringComputedLivePrototypeMethods) {
    boolean(R"JS(
let key='nor'+'malize',seen='';
String.prototype.normalize=function(){'use strict';seen=typeof this;return 'changed:'+this};
if('x'[key]()!=='changed:x'||seen!=='string')throw 1;
delete String.prototype.normalize;
Object.prototype.normalize=function(){'use strict';return 'inherited:'+this};
if('y'[key]()!=='inherited:y')throw 2;
delete Object.prototype.normalize;
let calls=0;
Object.defineProperty(String.prototype,key,{get(){calls++;return function(){'use strict';return this}},configurable:true});
if('z'[key]()!=='z'||calls!==1)throw 3;
true;
)JS");
}

TEST_F(JsMvpLmd, StringComputedNumericIndices) {
    boolean(R"JS(
let text='abc',key='1',out='';
for(let i=0;i<3;i++)out+=text[i];
let decimal='';for(let i=0;i<3;i++)decimal+=text[''+i];
out==='abc'&&decimal==='abc'&&text[key]==='b'&&text[1]==='b'&&text['1']==='b';
)JS");
}

TEST_F(JsMvpLmd, StringComputedAdmissionKeepsArrayMethods) {
    boolean(R"JS(
let key='copy'+'Within',a=[1,2,3];
a[key](1,0,1)===a&&a[0]===1&&a[1]===1&&a[2]===3;
)JS");
}

TEST_F(JsMvpLmd, StringComputedUnknownReceiver) {
    boolean(R"JS(
function f(s,k){return s[k]()}
f('e\u0301','normalize')==='\u00e9';
)JS");
}

TEST_F(JsMvpLmd, StringComputedNumericFamilyPlanningIsOptional) {
    boolean("let key=1;'abc'[key]==='b'");
    MvpLmdLibraryState* library = mvp_lmd_program_library();
    EXPECT_TRUE(!library || !library->primitives);
    boolean("let key='1';'abc'[key]==='b'");
    library = mvp_lmd_program_library();
    EXPECT_TRUE(!library || !library->primitives);
    boolean("let text='abc',out='';for(let i=0;i<3;i++)out+=text[''+i];out==='abc'");
    library = mvp_lmd_program_library();
    EXPECT_TRUE(!library || !library->primitives);
    boolean(R"JS(let key='nor'+'malize';'e\u0301'[key]()==='\u00e9')JS");
    library = mvp_lmd_program_library();
    ASSERT_NE(library, nullptr);
    EXPECT_NE(library->primitives, nullptr);
}

// contextual of uses the existing shared parser; keep the original Array metadata fixture unchanged.
TEST_F(JsMvpLmd, ContextualOfBindingsAndReferences) {
    { SCOPED_TRACE("let-var-and-reference"); boolean(R"JS(let of=1;of+=2;function local(){var of=4;return of}of===3&&local()===4;)JS"); }
    { SCOPED_TRACE("strict-let-and-const"); boolean(R"JS('use strict';let of=3;{const of=7;if(of!==7)throw 1}of===3;)JS"); }
    { SCOPED_TRACE("params-and-function-name"); boolean(R"JS(function of(of){return of+1}let f=(of)=>of*2;of(2)===3&&f(4)===8;)JS"); }
    { SCOPED_TRACE("property-and-shorthand"); boolean(R"JS(let of=5;let a={of,method(of){return of+this.of}};a.of===5&&a['of']===5&&a.method(3)===8;)JS"); }
    { SCOPED_TRACE("destructuring-bindings"); boolean(R"JS(let {of}={of:9};let [other]=[of];of===9&&other===9;)JS"); }
}
TEST_F(JsMvpLmd, ContextualOfForHeads) {
    { SCOPED_TRACE("for-of-declaration"); boolean(R"JS(let total=0;for(let of of [1,2,3])total+=of;total===6;)JS"); }
    { SCOPED_TRACE("for-of-var-and-reference"); boolean(R"JS(var of=0;for(of of [3,4]){}let total=0;for(var of of [1,2])total+=of;of===2&&total===3;)JS"); }
    { SCOPED_TRACE("for-of-const"); boolean(R"JS(let total=0;for(const of of [2,3])total+=of;total===5;)JS"); }
    { SCOPED_TRACE("for-c-style-and-in"); boolean(R"JS(let total=0;for(let of=0;of<3;of++)total+=of;let names='';for(let of in {a:1,b:2})names+=of;total===3&&names==='ab';)JS"); }
    { SCOPED_TRACE("legacy-let-in"); boolean(R"JS(var let;let count=0;for(let in {a:1,b:2})count++;count===2;)JS"); }
    { SCOPED_TRACE("ordinary-for-of-control"); boolean(R"JS(let of=[2,3],total=0;for(let value of of)total+=value;total===5;)JS"); }
}
TEST_F(JsMvpLmd, ContextualOfForHeadEarlyErrors) {
    const char* sources[] = {
        R"JS(for(let of []){})JS",
        R"JS(for(let of values){})JS",
        R"JS(for(let of=0 of []){})JS",
        R"JS(for(let of,other of []){})JS",
        R"JS(for(let let of []){})JS",
        R"JS(for(const of=0 of []){})JS",
    };
    for (const char* source : sources) {
        SCOPED_TRACE(source);
        mvp_lmd_destroy(execution);
        execution = mvp_lmd_execute(source, strlen(source));
        MvpLmdFailurePhase phase; const char* type = nullptr;
        EXPECT_NE(mvp_lmd_diagnostic(execution, &phase, &type), nullptr);
        EXPECT_EQ(phase, MVP_LMD_PARSE_FAILURE);
        EXPECT_STREQ(type, "SyntaxError");
    }
}

// retain the existing ContextualOfForHeadEarlyErrors and Array metadata fixtures unchanged.
TEST_F(JsMvpLmd, LexicalBindingLetContextControls) {
    { SCOPED_TRACE("sloppy-var-and-function-let"); boolean(R"JS(var let=1;function f(let){return let+1}let===1&&f(2)===3;)JS"); }
    { SCOPED_TRACE("sloppy-var-loop-let"); boolean(R"JS(var total=0;for(var let of [1,2])total+=let;var names='';for(var let in {a:1,b:2})names+=let;total===3&&names==='ab';)JS"); }
    { SCOPED_TRACE("legacy-let-in-reference"); boolean(R"JS(var let;var n=0;for(let in {a:1,b:2})n++;n===2;)JS"); }
    { SCOPED_TRACE("c-style-multiple-bindings"); boolean(R"JS(var sum=0;for(let a=0,b=2;a<2;a++,b++)sum+=a+b;sum===6;)JS"); }
    { SCOPED_TRACE("ordinary-multiple-bindings"); boolean(R"JS(let of=1,other=2;const a=3,b=4;of+other+a+b===10;)JS"); }
    { SCOPED_TRACE("let-property-key"); boolean(R"JS(let {let:value}={let:3};value===3;)JS"); }
    { SCOPED_TRACE("let-loop-property-key"); boolean(R"JS(let total=0;for(let {let:value} of [{let:4},{let:5}])total+=value;total===9;)JS"); }
    { SCOPED_TRACE("let-default-reference"); boolean(R"JS(var let=5;let [value=let]=[];value===5;)JS"); }
    { SCOPED_TRACE("let-computed-key-and-default-reference"); boolean(R"JS(var let=7;let {[let]:value=let}={};value===7;)JS"); }
    { SCOPED_TRACE("let-nested-default-function-param"); boolean(R"JS(let [f=function(let){return let}]=[];f(9)===9;)JS"); }
    { SCOPED_TRACE("let-var-escaped-binding"); boolean(R"JS(var l\u0065t=3;let===3;)JS"); }
    { SCOPED_TRACE("let-var-destructuring"); boolean(R"JS(var [let]=[4];let===4;)JS"); }
    { SCOPED_TRACE("let-var-loop-destructuring"); boolean(R"JS(var total=0;for(var [let] of [[3],[4]])total+=let;total===7;)JS"); }
    { SCOPED_TRACE("strict-property-key"); boolean(R"JS('use strict';let {let:value}={let:3};value===3;)JS"); }
}
TEST_F(JsMvpLmd, IterationDeclarationSingleBinding) {
    const char* sources[] = {
        R"JS(for(let a,b of []){})JS",
        R"JS(for(const a,b of []){})JS",
        R"JS(for(var a,b of []){})JS",
        R"JS(for(let a,b in {}){})JS",
        R"JS(for(const a,b in {}){})JS",
        R"JS(for(var a,b in {}){})JS",
        R"JS(for(let [a],{b} of []){})JS",
    };
    for (const char* source : sources) {
        SCOPED_TRACE(source);
        mvp_lmd_destroy(execution);
        execution = mvp_lmd_execute(source, strlen(source));
        MvpLmdFailurePhase phase = MVP_LMD_SUCCESS; const char* type = nullptr;
        EXPECT_NE(mvp_lmd_diagnostic(execution, &phase, &type), nullptr);
        EXPECT_EQ(phase, MVP_LMD_PARSE_FAILURE);
        EXPECT_STREQ(type, "SyntaxError");
    }
}
TEST_F(JsMvpLmd, LexicalLetBindingNames) {
    const char* sources[] = {
        R"JS(let let=1;)JS",
        R"JS(let let;)JS",
        R"JS(const let=1;)JS",
        R"JS(for(let let in {}){})JS",
        R"JS(for(const let of []){})JS",
        R"JS(for(let {x:let} of []){})JS",
        R"JS(let [let]=[];)JS",
        R"JS(let {a:let}={};)JS",
        R"JS(const {a:[{b:let}]}={a:[{}]};)JS",
        R"JS(let {...let}={};)JS",
        R"JS(for(let [...let] of []){})JS",
        R"JS(let l\u0065t=1;)JS",
        R"JS(for(let \u006cet of []){})JS",
        R"JS(for(let let=0;false;){})JS",
        R"JS('use strict';var let=1;)JS",
    };
    for (const char* source : sources) {
        SCOPED_TRACE(source);
        mvp_lmd_destroy(execution);
        execution = mvp_lmd_execute(source, strlen(source));
        MvpLmdFailurePhase phase = MVP_LMD_SUCCESS; const char* type = nullptr;
        EXPECT_NE(mvp_lmd_diagnostic(execution, &phase, &type), nullptr);
        EXPECT_EQ(phase, MVP_LMD_PARSE_FAILURE);
        EXPECT_STREQ(type, "SyntaxError");
    }
}

TEST_F(JsMvpLmd, StringSubstrBounds) {
    boolean(R"JS(
let s='abcdef';
if(s.substr()!==s||s.substr(undefined)!==s||s.substr(2)!=='cdef'||s.substr(2,undefined)!=='cdef')throw 1;
if(s.substr(-2)!=='ef'||s.substr(-20,3)!=='abc'||s.substr(1.9,2.9)!=='bc'||s.substr(-2.9,1.9)!=='e')throw 2;
if(s.substr(Infinity)!==''||s.substr(-Infinity)!==s||s.substr(1,Infinity)!=='bcdef'||s.substr(1,-Infinity)!=='')throw 3;
if(s.substr(1e100,2)!==''||s.substr(-1e100,1e100)!==s||s.substr(1,-1e100)!=='')throw 4;
if(s.substr(NaN,2)!=='ab'||s.substr(2,NaN)!==''||s.substr(-0,1)!=='a')throw 5;
if(s.substr(1,null)!==''||s.substr(1,false)!==''||s.substr(1,'')!==''||s.substr(1,-1)!=='')throw 6;
true;
)JS");
}

TEST_F(JsMvpLmd, StringSubstrUtf16) {
    boolean(R"JS(
let s='a\uD83D\uDCA9b';
if(s.substr(1,1)!=='\uD83D'||s.substr(2,1)!=='\uDCA9'||s.substr(1,2)!=='\uD83D\uDCA9')throw 1;
if(s.substr(-2,1)!=='\uDCA9'||'\uD800x\uDC00'.substr(0,2)!=='\uD800x')throw 2;
if('é'.substr(0,1)!=='é'||'e\u0301'.substr(1,1)!=='\u0301')throw 3;
true;
)JS");
}

TEST_F(JsMvpLmd, StringSubstrConversionOrder) {
    boolean(R"JS(
let order='',receiver={toString(){order+='r';return 'abcdef'}},start={valueOf(){order+='s';return 1}},length={valueOf(){order+='l';return 2}};
if(String.prototype.substr.call(receiver,start,length)!=='bc'||order!=='rsl')throw 1;
order='';
if(''.substr(start,length)!==''||order!=='sl')throw 2;
order='';
if('a'.substr(Infinity,length)!==''||order!=='l')throw 3;
order='';
if('a'.substr(start,0)!==''||order!=='s')throw 4;
true;
)JS");
}

TEST_F(JsMvpLmd, StringSubstrAbruptConversion) {
    boolean(R"JS(
let marker={},seen=0,caught=false;
try{String.prototype.substr.call({toString(){throw marker}},{valueOf(){seen++;return 0}})}catch(e){caught=e===marker}
if(!caught||seen)throw 1;
caught=false;
try{'abc'.substr({valueOf(){throw marker}},{valueOf(){seen++;return 1}})}catch(e){caught=e===marker}
if(!caught||seen)throw 2;
caught=false;try{''.substr(Infinity,{valueOf(){throw marker}})}catch(e){caught=e===marker}if(!caught)throw 3;
caught=false;try{'abc'.substr(Symbol())}catch(e){caught=e instanceof TypeError}if(!caught)throw 4;
caught=false;try{''.substr(0,Symbol())}catch(e){caught=e instanceof TypeError}if(!caught)throw 5;
caught=false;try{'abc'.substr(1n)}catch(e){caught=e instanceof TypeError}if(!caught)throw 6;
caught=false;try{String.prototype.substr.call(null,0)}catch(e){caught=e instanceof TypeError}if(!caught)throw 7;
caught=false;try{String.prototype.substr.call(undefined,0)}catch(e){caught=e instanceof TypeError}if(!caught)throw 8;
true;
)JS");
}

TEST_F(JsMvpLmd, StringSubstrScalarRetention) {
    boolean(R"JS(
let start=1.25,length=2.25;
let receiver={toString(){start=4.75;length=5.75;for(let i=0;i<90;i++){let a=[i,{i}]}return 'abcdef'}};
if(String.prototype.substr.call(receiver,start,length)!=='bc')throw 1;
length=2.25;
let coercible={valueOf(){length=5.75;for(let i=0;i<90;i++){let a=[i,{i}]}return 1}};
if('abcdef'.substr(coercible,length)!=='bc'||length!==5.75)throw 2;
let tiny=5e-324;
if('abc'.substr({valueOf(){tiny=2;return 1}},tiny)!=='')throw 3;
true;
)JS");
}

TEST_F(JsMvpLmd, StringSubstrMetadata) {
    boolean(R"JS(
let f=String.prototype.substr,d=Object.getOwnPropertyDescriptor(String.prototype,'substr'),n=Object.getOwnPropertyDescriptor(f,'name'),l=Object.getOwnPropertyDescriptor(f,'length');
if(f.name!=='substr'||f.length!==2||!d.writable||d.enumerable||!d.configurable)throw 1;
if(n.writable||n.enumerable||!n.configurable||l.writable||l.enumerable||!l.configurable)throw 2;
let caught=false;try{new f()}catch(e){caught=e instanceof TypeError}if(!caught)throw 3;
if(Object.prototype.hasOwnProperty.call(f,'prototype'))throw 4;
if(String.prototype.substr!==f)throw 5;
true;
)JS");
}

TEST_F(JsMvpLmd, StringSubstrComputedAndBorrowed) {
    boolean(R"JS(
let key='sub'+'str';
if('abcdef'[key](2,2)!=='cd')throw 1;
if(String.prototype.substr.call(12345,1,3)!=='234')throw 2;
function f(s,k,a,b){return s[k](a,b)}
if(f('abcdef',key,-2,1)!=='e')throw 3;
String.prototype.substr=function(){'use strict';return 'changed:'+this};
if('x'[key]()!=='changed:x')throw 4;
true;
)JS");
}

TEST_F(JsMvpLmd, NumberInheritedStringLocaleUpper) {
    boolean(R"JS(
Number.prototype.toLocaleUpperCase=String.prototype.toLocaleUpperCase;
if(NaN.toLocaleUpperCase()!=='NAN'||Infinity.toLocaleUpperCase()!=='INFINITY'||(3.5).toLocaleUpperCase()!=='3.5')throw 1;
if((-Infinity).toLocaleUpperCase()!=='-INFINITY'||(7).toLocaleUpperCase()!=='7')throw 2;
true;
)JS");
}

TEST_F(JsMvpLmd, NumberInheritedGetterReceiverAndOrder) {
    boolean(R"JS(
let order='',getterReceiver,callReceiver;
Object.defineProperty(Number.prototype,'upper',{get(){'use strict';order+='g';getterReceiver=this;return function(arg){'use strict';order+='c';callReceiver=this;return arg}},configurable:true});
function argument(){order+='a';delete Number.prototype.upper;return 7}
if((3.5).upper(argument())!==7||order!=='gac'||getterReceiver!==3.5||callReceiver!==3.5)throw 1;
Object.defineProperty(Number.prototype,'upper',{get(){'use strict';getterReceiver=this;return String.prototype.toLocaleUpperCase},configurable:true});
if(NaN.upper()!=='NAN'||!Number.isNaN(getterReceiver))throw 2;
true;
)JS");
}

TEST_F(JsMvpLmd, NumberInheritedDataReadAndReplacement) {
    boolean(R"JS(
Number.prototype.extra='value';
if((3.5).extra!=='value'||NaN.extra!=='value')throw 1;
Number.prototype.toLocaleUpperCase=String.prototype.toLocaleUpperCase;
let saved=NaN.toLocaleUpperCase;
Number.prototype.toLocaleUpperCase=function(){'use strict';return typeof this};
if(Infinity.toLocaleUpperCase()!=='number'||saved.call(NaN)!=='NAN')throw 2;
delete Number.prototype.toLocaleUpperCase;
Object.prototype.toLocaleUpperCase=String.prototype.toLocaleUpperCase;
if(Infinity.toLocaleUpperCase()!=='INFINITY')throw 3;
true;
)JS");
}

TEST_F(JsMvpLmd, NumberInheritedAdmissionKeepsClosedNumericFamilyOptional) {
    numeric("let n=3.5;n+2", 5.5);
    MvpLmdLibraryState* library = mvp_lmd_program_library();
    EXPECT_TRUE(!library || !library->primitives);
    boolean("(3.5).toFixed(1)==='3.5'");
    library = mvp_lmd_program_library();
    EXPECT_TRUE(!library || !library->primitives);
    boolean("Number.prototype.toLocaleUpperCase=String.prototype.toLocaleUpperCase;NaN.toLocaleUpperCase()==='NAN'");
    library = mvp_lmd_program_library();
    ASSERT_NE(library, nullptr);
    EXPECT_NE(library->primitives, nullptr);
}

TEST_F(JsMvpLmd, LexicalIterationPatternBindingControls) {
    { SCOPED_TRACE("var-array-duplicates"); boolean(R"JS(var x;for(var [x,x] of [[1,2]]){}x===2;)JS"); }
    { SCOPED_TRACE("strict-var-array-duplicates"); boolean(R"JS('use strict';var x;for(var [x,x] of [[1,2]]){}x===2;)JS"); }
    { SCOPED_TRACE("var-object-duplicates"); boolean(R"JS(var x;for(var {a:x,b:x} of [{a:3,b:4}]){}x===4;)JS"); }
    { SCOPED_TRACE("var-rest-duplicates"); boolean(R"JS(var x;for(var [x,...x] of [[1,2,3]]){}x.length===2&&x[0]===2&&x[1]===3;)JS"); }
    { SCOPED_TRACE("var-in-duplicates"); boolean(R"JS(var x;for(var [x,x] in {'ab':1}){}x==='b';)JS"); }
    { SCOPED_TRACE("nested-lexical-targets"); boolean(R"JS(let sum=0;for(let [x,{a:y},...rest] of [[1,{a:2},3,4]])sum=x+y+rest[0]+rest[1];sum===10;)JS"); }
    { SCOPED_TRACE("same-property-keys-distinct-targets"); boolean(R"JS(let sum=0;for(const {x:a,x:b} of [{x:3}])sum=a+b;sum===6;)JS"); }
    { SCOPED_TRACE("default-reference-excluded"); boolean(R"JS(let sum=0;for(let [x=1,y=x] of [[]])sum=x+y;sum===2;)JS"); }
    { SCOPED_TRACE("computed-key-references-excluded"); boolean(R"JS(let x='a',sum=0;for(const {[x]:a,[x]:b} of [{a:3}])sum=a+b;sum===6;)JS"); }
    { SCOPED_TRACE("computed-key-bound-name-excluded"); boolean(R"JS(for(let {[x]:x} of []){}true;)JS"); }
    { SCOPED_TRACE("default-function-names-excluded"); boolean(R"JS(let value=0;for(let [x=function(x,x){return x}] of [[]])value=x(1,2);value===2;)JS"); }
    { SCOPED_TRACE("separate-loop-scopes"); boolean(R"JS(let x=10,total=0;for(let [x] of [[1]])total+=x;for(let [x] of [[2]])total+=x;x===10&&total===3;)JS"); }
    { SCOPED_TRACE("contextual-of-loop-binding"); boolean(R"JS(let total=0;for(let [of] of [[2],[3]])total+=of;total===5;)JS"); }
}
TEST_F(JsMvpLmd, LexicalIterationPatternDuplicateEarlyErrors) {
    const char* sources[] = {
        R"JS(for(let [x,x] of []){})JS",
        R"JS(for(const [x,x] of []){})JS",
        R"JS(for(let [x,x] in {}){})JS",
        R"JS(for(const [x,x] in {}){})JS",
        R"JS(for(let [x,{a:x}] of []){})JS",
        R"JS(for(const {a:[x],b:{c:x}} of []){})JS",
        R"JS(for(let [x,...x] of []){})JS",
        R"JS(for(const {x,...x} of []){})JS",
        R"JS(for(let [x=0,x] of []){})JS",
        R"JS(for(const {[key]:x,other:x} of []){})JS",
        R"JS(for(let [x,\u0078] of []){})JS",
        R"JS(for(let [x,...[x]] of []){})JS",
        R"JS(let [x,x]=[];)JS",
        R"JS(for(let [x,x]=[];false;){})JS",
    };
    for (const char* source : sources) {
        SCOPED_TRACE(source);
        mvp_lmd_destroy(execution);
        execution = mvp_lmd_execute(source, strlen(source));
        MvpLmdFailurePhase phase = MVP_LMD_SUCCESS; const char* type = nullptr;
        EXPECT_NE(mvp_lmd_diagnostic(execution, &phase, &type), nullptr);
        EXPECT_EQ(phase, MVP_LMD_PARSE_FAILURE);
        EXPECT_STREQ(type, "SyntaxError");
    }
}

// synthetic size pressure exercises compiler policy without changing a corpus input.
static StrBuf* mvp_compact_entry_source(const char* prefix, const char* body,
        const char* suffix, int reads = 64, bool open = true) {
    StrBuf* source = strbuf_new();
    if (!source) return NULL;
    strbuf_append_str(source, open
        ? "const builder=Function;let sample=builder('return {padding:1}')(),scratch=0;"
        : "const S=String;function unknown(v){return Object.create(v)}let sample=unknown({padding:1}),scratch=0;");
    strbuf_append_str(source, prefix);
    strbuf_append_str(source, "for(let trip=0;trip<2;trip++){");
    for (int i = 0; i < reads; i++) strbuf_append_str(source, "scratch=sample.padding;");
    strbuf_append_str(source, body);
    strbuf_append_char(source, '}');
    strbuf_append_str(source, suffix);
    return source;
}

TEST_F(JsMvpLmd, CompactEntryPropertyDescriptorsAndCalls) {
    const char* cases[][3] = {
        {"let receiver=builder('return arguments[0]')({x:1}),sum=0;",
         "sum+=receiver.x;Object.defineProperty(receiver,'x',{get(){return 3},configurable:true});",
         "sum===4&&scratch===1"},
        {"let events='',receiver=builder('return arguments[0]')({}),ok=true;Object.defineProperty(receiver,'run',{get(){events+='g';return function(v){return this===receiver&&v===3}}});",
         "ok=ok&&receiver.run((events+='a',3));", "ok&&events==='gaga'"},
        {"let events='',target={x:7},receiver=new Proxy(target,{get(t,k,r){events+=k;return r===receiver?Reflect.get(t,k,r):0}}),sum=0;",
         "sum+=receiver.x;", "sum===14&&events==='xx'"},
        {"let token={keep:17},receiver=builder('return arguments[0]')({get x(){let garbage=[];for(let k=0;k<12;k++)garbage.push({k});throw token}}),caught=0;",
         "try{receiver.x}catch(e){if(e===token&&e.keep===17)caught++}", "caught===2"},
    };
    for (const auto& probe : cases) {
        StrBuf* source = mvp_compact_entry_source(probe[0], probe[1], probe[2]);
        ASSERT_NE(source, nullptr);
        boolean(source->str); strbuf_free(source);
    }
}

TEST_F(JsMvpLmd, CompactEntryScalarAndSequenceOwnership) {
    const char* cases[][3] = {
        {"let receiver=builder('return arguments[0]')({x:5e-324}),ok=true;",
         "let saved=receiver.x;receiver.x=7;let garbage=[];for(let k=0;k<12;k++)garbage.push({k});ok=ok&&saved===5e-324;receiver.x=5e-324;",
         "ok"},
        {"Object.defineProperty(Number.prototype,'small',{get:function(){'use strict';return this},configurable:true});let ok=true;",
         "let value=builder('return arguments[0]')(trip===0?5e-324:-0),saved=value.small;let garbage=[];for(let k=0;k<12;k++)garbage.push({k});ok=ok&&(trip===0?saved===5e-324:1/saved===-Infinity);",
         "ok"},
        {"let values=[['x'],'\\uD83D\\uDE00'],ok=true;",
         "let value=builder('return arguments[0]')(values[trip]);ok=ok&&(trip===0?value.length===1&&value['0']==='x':value.length===2&&value['0']==='\\uD83D'&&value['1']==='\\uDE00');",
         "ok"},
    };
    for (const auto& probe : cases) {
        StrBuf* source = mvp_compact_entry_source(probe[0], probe[1], probe[2]);
        ASSERT_NE(source, nullptr);
        boolean(source->str); strbuf_free(source);
    }
}

TEST_F(JsMvpLmd, CompactEntryGlobalsAndDeferredKeys) {
    const char* cases[][3] = {
        {"globalThis.compactGlobal=1;let total=0;",
         "total+=compactGlobal;Object.defineProperty(globalThis,'compactGlobal',{get(){return 3},configurable:true});",
         "total===4"},
        {"let events='';Object.setPrototypeOf(globalThis,new Proxy({}, {has(t,k){if(k==='compactMissing'){events+='h';return true}return false},get(t,k){if(k==='compactMissing'){events+='g';return 9}}}));let total=0;",
         "total+=compactMissing;", "total===18&&events==='hghg'"},
        {"let events='',caught=0;function key(){events+='k';return 0.25}",
         "let receiver=builder('return null')();try{receiver[key()]}catch(e){if(e instanceof TypeError)caught++}",
         "caught===2&&events==='kk'"},
        {"let events='',receiver=builder('return arguments[0]')({get x(){events+='g';return 9}}),key={toString(){events+='k';return'x'}},total=0;",
         "total+=receiver[key];", "total===18&&events==='kgkg'"},
    };
    for (const auto& probe : cases) {
        StrBuf* source = mvp_compact_entry_source(probe[0], probe[1], probe[2]);
        ASSERT_NE(source, nullptr);
        boolean(source->str); strbuf_free(source);
    }
}

TEST_F(JsMvpLmd, CompactEntryBoundsGenericCodeGrowth) {
    size_t lines[2] = {}, outlined[2] = {};
    for (int i = 0; i < 2; i++) {
        StrBuf* source = mvp_compact_entry_source("", "", "scratch===1", i ? 128 : 64);
        ASSERT_NE(source, nullptr);
        boolean(source->str); strbuf_free(source);
        char* mir = dump("temp/mvp_lmd_compact_entry_size.mir");
        ASSERT_NE(mir, nullptr);
        const char* body = strstr(mir, "mvp_lmd_program:\tfunc");
        ASSERT_NE(body, nullptr);
        const char* end = strstr(body, "\tendfunc");
        ASSERT_NE(end, nullptr);
        for (const char* at = body; at < end; at++) if (*at == '\n') lines[i]++;
        for (const char* at = body; (at = strstr(at, ", mvp_lmd_property_read,")) && at < end; at++) outlined[i]++;
        mem_free(mir);
    }
    // further sites use one call instead of duplicating a full generic decoder.
    EXPECT_GE(outlined[0], 16u);
    EXPECT_GE(outlined[1], outlined[0] + 64u);
    EXPECT_LT(lines[1], lines[0] + 10000u);
}

TEST_F(JsMvpLmd, CompactEntryRetainsClosedAndFunctionEmission) {
    StrBuf* source = mvp_compact_entry_source("", "", "scratch===1", 64, false);
    ASSERT_NE(source, nullptr);
    boolean(source->str); strbuf_free(source);
    char* mir = dump("temp/mvp_lmd_compact_closed_control.mir");
    ASSERT_NE(mir, nullptr);
    EXPECT_EQ(strstr(mir, "\timport\tmvp_lmd_property_read\n"), nullptr);
    mem_free(mir);

    source = mvp_compact_entry_source("function read(value){return value.x}let receiver=builder('return {x:7}')();",
        "if(read(receiver)!==7)throw 1;", "true");
    ASSERT_NE(source, nullptr);
    boolean(source->str); strbuf_free(source);
    mir = dump("temp/mvp_lmd_compact_function_control.mir");
    ASSERT_NE(mir, nullptr);
    const char* body = strstr(mir, "mvp_lmd_f1:\tfunc");
    ASSERT_NE(body, nullptr);
    const char* end = strstr(body, "\tendfunc");
    ASSERT_NE(end, nullptr);
    const char* outlined = strstr(body, ", mvp_lmd_property_read,");
    EXPECT_TRUE(!outlined || outlined > end);
    EXPECT_NE(strstr(mir, "\timport\tmvp_lmd_property_read\n"), nullptr);
    mem_free(mir);

    boolean("const F=Function;let sample=F('return {x:7}')(),total=0;for(let i=0;i<2;i++)total+=sample.x;total===14");
    mir = dump("temp/mvp_lmd_compact_small_control.mir");
    ASSERT_NE(mir, nullptr);
    EXPECT_EQ(strstr(mir, "\timport\tmvp_lmd_property_read\n"), nullptr);
    mem_free(mir);
}

TEST_F(JsMvpLmd, LegacyTypedConstantNamesReuseIndexClassification) {
    boolean("const S=String;function choose(flag){return flag?new Int32Array(2):'ab'}let value=choose(true);value[0]=-2147483648;value[1]=2147483647;typeof value['0']==='number'&&value['0']===-2147483648&&value['1']===2147483647&&value.length===2");
    boolean("const S=String;function choose(flag){return flag?new Uint8Array(2):'ab'}let value=choose(true);value.fill(7);value['0']===7&&value['1']===7&&value['2']===undefined&&value['-0']===undefined&&value['NaN']===undefined&&value['Infinity']===undefined&&value['4294967295']===undefined&&value['9007199254740991']===undefined&&value['9007199254740992']===undefined");
    boolean("const S=String;function choose(flag){return flag?new Float64Array(2):'ab'}let value=choose(true);value[0]=5e-324;let saved=value['0'];value[0]=7;saved===5e-324&&value.length===2");
    boolean("function choose(flag){return flag?new Int32Array(2):'ab'}let value=choose(true);value['length']===2");
    char* mir = dump("temp/mvp_lmd_constant_legacy_key.mir");
    ASSERT_NE(mir, nullptr);
    EXPECT_NE(strstr(mir, "\timport\tarray_num_new\n"), nullptr);
    EXPECT_EQ(strstr(mir, "\timport\tmvp_lmd_string_key\n"), nullptr);
    mem_free(mir);
}

TEST_F(JsMvpLmd, ImportCallGrammarPositiveControls) {
    const struct { const char* source; JsParseMode mode; } cases[] = {
        {R"JS(let f=()=>import('./unused.js');)JS", JS_PARSE_SCRIPT},
        {R"JS(let f=()=>import('./unused.js',);)JS", JS_PARSE_SCRIPT},
        {R"JS(let f=()=>import('./unused.js',{});)JS", JS_PARSE_SCRIPT},
        {R"JS(let f=()=>import('./unused.js',{},);)JS", JS_PARSE_SCRIPT},
        {R"JS(let f=()=>import(('./a.js','./b.js'));)JS", JS_PARSE_SCRIPT},
        {R"JS(let f=()=>import([...['./unused.js']]);)JS", JS_PARSE_SCRIPT},
        {R"JS(let options={};let f=()=>import('./unused.js',{...options});)JS", JS_PARSE_SCRIPT},
        {R"JS(let f=()=>new (import('./unused.js'));)JS", JS_PARSE_SCRIPT},
        {R"JS(let f=()=>new (import('./unused.js'))();)JS", JS_PARSE_SCRIPT},
        {R"JS(let f=()=>new ((import('./unused.js')))();)JS", JS_PARSE_SCRIPT},
        {R"JS(let f=()=>new (function(){return import('./unused.js')});)JS", JS_PARSE_SCRIPT},
        {R"JS(let f=()=>({import(){}}).import(...['x']);)JS", JS_PARSE_SCRIPT},
        {R"JS(let f=()=>({import(){}}).import('x',{},'extra');)JS", JS_PARSE_SCRIPT},
        {R"JS(let f=()=>new import.meta.C();)JS", JS_PARSE_MODULE},
        {R"JS(let f=()=>new import.meta['C']();)JS", JS_PARSE_MODULE},
        {R"JS(let f=()=>new (import.meta.C)();)JS", JS_PARSE_MODULE},
    };
    for (const auto& test : cases) {
        SCOPED_TRACE(test.source);
        JsParseError error = {};
        EXPECT_EQ(js_parser_parse_source(test.source, strlen(test.source), test.mode,
            NULL, NULL, NULL, &error), JS_PARSE_OK) << (error.message ? error.message : "");
    }
}

TEST_F(JsMvpLmd, ImportCallGrammarRejectsMalformedFormsAtParse) {
    const struct { const char* source; bool module; } cases[] = {
        {R"JS(let f=()=>{import();};)JS", false},
        {R"JS(let f=()=>{import(/*specifier missing*/);};)JS", false},
        {R"JS(let f=()=>{import(...['./unused.js']);};)JS", false},
        {R"JS(let f=()=>{import('./unused.js',...[]);};)JS", false},
        {R"JS(let f=()=>{import('./unused.js',{},'extra');};)JS", false},
        {R"JS(let f=()=>{import('./unused.js',{},'extra',);};)JS", false},
        {R"JS(let f=()=>{new import('./unused.js');};)JS", false},
        {R"JS(let f=()=>{new /*comment*/ import /*comment*/ ('./unused.js');};)JS", false},
        {R"JS(let f=()=>{new import('./unused.js').C();};)JS", false},
        {R"JS(let f=()=>{new new import('./unused.js');};)JS", false},
        {R"JS(function f(a=import()){})JS", false},
        {R"JS(class C{m(){import(...[]);}})JS", false},
        {R"JS(async function f(){await import('./unused.js',{},'extra');})JS", false},
        {R"JS(function* f(){yield import();})JS", false},
        {R"JS(export default ()=>import(...[]);)JS", true},
        {R"JS(export default ()=>new import('./unused.js');)JS", true},
    };
    for (const auto& test : cases) {
        SCOPED_TRACE(test.source);
        mvp_lmd_destroy(execution);
        MvpLmdOptions options = {"temp/import-call-syntax.mjs", test.module};
        execution = mvp_lmd_execute_with_options(test.source, strlen(test.source), NULL, NULL, &options);
        MvpLmdFailurePhase phase = MVP_LMD_SUCCESS;
        const char* type = nullptr;
        EXPECT_NE(mvp_lmd_diagnostic(execution, &phase, &type), nullptr);
        EXPECT_EQ(phase, MVP_LMD_PARSE_FAILURE);
        EXPECT_STREQ(type, "SyntaxError");
    }
}

TEST_F(JsMvpLmd, ImportCallGrammarKeepsUncalledValidBodies) {
    boolean(R"JS(let f=()=>import('./unused.js');typeof f==='function';)JS");
    boolean(R"JS(let f=()=>import('./unused.js',);typeof f==='function';)JS");
    boolean(R"JS(let f=()=>new (import('./unused.js'));typeof f==='function';)JS");
    boolean(R"JS(let f=()=>({import(){return 7}}).import(...['x']);f()===7;)JS");
}


TEST_F(JsMvpLmd, CompactEntryGuardedCallsPreserveSemantics) {
    const char* cases[][3] = {
        {"function read(value){return value.x}let receiver=builder('return arguments[0]')({x:1}),total=0;",
         "total+=read(receiver);receiver.x=3;",
         "total===4"},
        {"function pick(value){return value}let events='',ok=true;",
         "let value=pick((events+='a',builder('globalThis.pick=function(){return 99}')(),3));ok=ok&&(trip===0?value===3:value===99);",
         "ok&&events==='aa'"},
        {"class Reader{read(value){return this.x+value}}let receiver=builder('return arguments[0]')(new Reader()),total=0;receiver.x=4;",
         "total+=receiver.read(3);",
         "total===14"},
        {"function read(value){return value.x}let receiver=builder('return arguments[0]')({x:5e-324}),ok=true;",
         "let saved=read(receiver);receiver.x=7;let garbage=[];for(let k=0;k<12;k++)garbage.push({k});ok=ok&&saved===5e-324;receiver.x=5e-324;",
         "ok"},
        {"function read(value){return value.x}let token={keep:17},caught=0,receiver=builder('return arguments[0]')({get x(){let garbage=[];for(let k=0;k<12;k++)garbage.push({k});throw token}});",
         "try{read(receiver)}catch(e){if(e===token&&e.keep===17)caught++}",
         "caught===2"},
        {"function pick(value){return value}let events='',caught=0;builder('globalThis.pick=1')();",
         "try{pick((events+='a',3))}catch(e){if(e instanceof TypeError)caught++}",
         "caught===2&&events==='aa'"},
    };
    for (const auto& probe : cases) {
        StrBuf* source = mvp_compact_entry_source(probe[0], probe[1], probe[2]);
        ASSERT_NE(source, nullptr);
        boolean(source->str); strbuf_free(source);
    }
}

TEST_F(JsFormalMir, CompactEntrySharedInvocationPreservesCallState) {
    const char* cases[][3] = {
        {"let events='',receiver={get run(){events+='g';return function(v){return this===receiver&&v===3}}},ok=true;",
         "ok=ok&&receiver.run((events+='a',gc(),3));", "ok&&events==='gaga'"},
        {"let receiver={run(v){return v}},ok=true;",
         "let saved=receiver.run(5e-324);gc();ok=ok&&saved===5e-324;", "ok"},
        {"let receiver={run:new Proxy(function(v){return this.x+v},{apply(t,r,a){gc();return Reflect.apply(t,r,a)}}),x:4},total=0;",
         "total+=receiver.run(3);", "total===14"},
        {"let receiver={run:Array.prototype.slice},source=[1,2,3],ok=true;",
         "ok=ok&&receiver.run.call(source,1)[0]===2&&Math.max(1,4,2)===4;", "ok"},
        {"let effects='',receiver={run:1},caught=0;",
         "try{receiver.run((effects+='a',3))}catch(e){if(e instanceof TypeError)caught++}",
         "effects==='aa'&&caught===2"},
        {"let token={x:7},receiver={run(){gc();throw token}},caught=0;",
         "try{receiver.run()}catch(e){if(e===token&&e.x===7)caught++}", "caught===2"},
    };
    for (const auto& probe : cases) {
        StrBuf* source = mvp_compact_entry_source(probe[0], probe[1], probe[2]);
        ASSERT_NE(source, nullptr);
        StrBuf* block = strbuf_new();
        ASSERT_NE(block, nullptr);
        strbuf_append_format(block, "{%s}", source->str);
        Item result = run(block->str);
        strbuf_free(block); strbuf_free(source);
        ASSERT_FALSE(item_is_error(result));
        EXPECT_EQ(result.item, ITEM_TRUE);
    }
}

TEST_F(JsMvpLmd, CompactEntryBoundsInvocationGrowth) {
    size_t lines[2] = {}, calls[2] = {};
    for (int i = 0; i < 2; i++) {
        StrBuf* body = strbuf_new();
        ASSERT_NE(body, nullptr);
        for (int j = 0; j < (i ? 96 : 16); j++) strbuf_append_str(body, "receiver.run(3);");
        StrBuf* source = mvp_compact_entry_source("let receiver=builder('return {run(v){return v}}')();",
            body->str, "scratch===1");
        strbuf_free(body);
        ASSERT_NE(source, nullptr);
        boolean(source->str); strbuf_free(source);
        char* mir = dump("temp/mvp_lmd_compact_call_size.mir");
        ASSERT_NE(mir, nullptr);
        const char* entry = strstr(mir, "mvp_lmd_program:\tfunc");
        ASSERT_NE(entry, nullptr);
        const char* end = strstr(entry, "\tendfunc");
        ASSERT_NE(end, nullptr);
        for (const char* at = entry; at < end; at++) if (*at == '\n') lines[i]++;
        for (const char* at = entry; (at = strstr(at, ", mvp_lmd_class_invoke,")) && at < end; at++) calls[i]++;
        mem_free(mir);
    }
    EXPECT_GE(calls[1], calls[0] + 80u);
    // later generic calls share ABI dispatch rather than clone its control flow.
    EXPECT_LT(lines[1] - lines[0], 80u * 220u);
}

TEST_F(JsMvpLmd, CompactEntryBoundsGuardedInlineGrowth) {
    const char* prefix = "function classify(value,n){if((Int32Array!==undefined&&value instanceof Int32Array)||(Uint8Array!==undefined&&value instanceof Uint8Array))return n+1;return n}let receiver=builder('return new Int32Array(1)')(),total=0;";
    const int calls[] = {1, 4, 12};
    const char* expected[] = {"total===8", "total===32", "total===96"};
    size_t lines[3] = {}, guards[3] = {}, callee_lines[3] = {};
    for (int i = 0; i < 3; i++) {
        StrBuf* body = strbuf_new();
        ASSERT_NE(body, nullptr);
        for (int j = 0; j < calls[i]; j++) strbuf_append_str(body, "total+=classify(receiver,3);");
        StrBuf* source = mvp_compact_entry_source(prefix, body->str, expected[i], i ? 64 : 0);
        strbuf_free(body);
        ASSERT_NE(source, nullptr);
        boolean(source->str); strbuf_free(source);
        char* mir = dump("temp/mvp_lmd_guarded_inline_size.mir");
        ASSERT_NE(mir, nullptr);
        const char* entry = strstr(mir, "mvp_lmd_program:\tfunc");
        ASSERT_NE(entry, nullptr);
        const char* end = strstr(entry, "\tendfunc");
        ASSERT_NE(end, nullptr);
        for (const char* at = entry; at < end; at++) if (*at == '\n') lines[i]++;
        // lowered MIR materializes the guard target with mov before bne.
        for (const char* at = entry; (at = strstr(at, ", mvp_lmd_f1\n")) && at < end; at++) guards[i]++;
        const char* callee = strstr(mir, "mvp_lmd_f1:\tfunc");
        ASSERT_NE(callee, nullptr);
        const char* callee_end = strstr(callee, "\tendfunc");
        ASSERT_NE(callee_end, nullptr);
        for (const char* at = callee; at < callee_end; at++) if (*at == '\n') callee_lines[i]++;
        mem_free(mir);
    }
    EXPECT_GT(guards[0], 0u);
    EXPECT_EQ(guards[1], 0u);
    EXPECT_EQ(guards[2], 0u);
    EXPECT_EQ(callee_lines[1], callee_lines[2]);
    ASSERT_GE(lines[2], lines[1]);
    // each later call must add less code than another complete callee body.
    EXPECT_LT(lines[2] - lines[1], (calls[2] - calls[1]) * callee_lines[2]);

    StrBuf* source = mvp_compact_entry_source(
        "function read(value){return value.x}function outer(value){return read(value)}let receiver=builder('return {x:7}')(),total=0;",
        "total+=outer(receiver);", "total===14");
    ASSERT_NE(source, nullptr);
    boolean(source->str); strbuf_free(source);
    char* mir = dump("temp/mvp_lmd_guarded_inline_function_control.mir");
    ASSERT_NE(mir, nullptr);
    const char* body = strstr(mir, "mvp_lmd_f2:\tfunc");
    ASSERT_NE(body, nullptr);
    const char* end = strstr(body, "\tendfunc");
    ASSERT_NE(end, nullptr);
    const char* guard = strstr(body, ", mvp_lmd_f1\n");
    EXPECT_TRUE(guard && guard < end);
    mem_free(mir);
}

TEST_F(JsMvpLmd, EvalEmptySourceCompletions) {
    boolean(R"JS(
let texts=['',' \t\v\f\r\n\u00A0\u1680\u2000\u2007\u2028\u2029\u202F\u205F\u3000\uFEFF',
  '// arguments new.target super() \uD800\0', '/* arguments new.target super() \uD800\0 */',
  '#!/usr/bin/env lambda\n// tail', '<!-- ignored', '\n--> ignored', '/* first */ \t // second'];
function check(text){let retained=7;const fixed=9;let before=()=>retained+fixed;
  if(eval(text)!==undefined||retained!==7||fixed!==9||before()!==16)throw 1;
  if((0,eval)(text)!==undefined)throw 2;
}
for(let text of texts)check(text);
true;
)JS");
}

TEST_F(JsMvpLmd, EvalCommentLineTerminatorsExecute) {
    boolean(R"JS(
let endings=['\n','\r','\u2028','\u2029'];
function direct(ending){let yy=0;if(eval('//var '+ending+'yy = -1')!==-1||yy!==-1)throw 1;}
for(let ending of endings){
  direct(ending);globalThis.commentEvalSlot=0;
  if((0,eval)('//var '+ending+'commentEvalSlot = -1')!==-1||commentEvalSlot!==-1)throw 2;
}
delete globalThis.commentEvalSlot;
let yy=0;if(eval('/*var \n yy = -1 */')!==undefined||yy!==0)throw 3;
if(eval('// x\n17')!==17||(0,eval)('/* x */ 19')!==19)throw 4;
true;
)JS");
}

TEST_F(JsMvpLmd, EvalCommentErrorsRemainSyntax) {
    boolean(R"JS(
let texts=['/*','/*/','/* never closed\n','#!/ok\n/*',' #! misplaced','\n#! misplaced','/* prefix */#! misplaced'];
for(let source of texts){
  let direct=false,indirect=false;
  try{eval(source)}catch(e){direct=e instanceof SyntaxError}
  try{(0,eval)(source)}catch(e){indirect=e instanceof SyntaxError}
  if(!direct||!indirect)throw source;
}
true;
)JS");
}

TEST_F(JsMvpLmd, EvalCommentsStrictAndHostedCaller) {
    boolean(R"JS(
function strict(){'use strict';let local=5;const held=7;
  if(eval('/* arguments; new.target; super(); */')!==undefined||local+held!==12)throw 1;
  if(eval('// comment\nlocal += 1')!==6||local!==6)throw 2;
}
strict();
let hosted=Function('let x=3;const y=4;if(eval("/* comment */")!==undefined||x+y!==7)throw 3;return eval("// comment\\nx=8")===8&&x===8');
if(!hosted())throw 4;
class C{field=eval('/* arguments super() new.target */');read(){return eval('// arguments new.target super()')}}
let c=new C;if(c.field!==undefined||c.read()!==undefined)throw 5;
true;
)JS");
}

TEST_F(JsMvpLmd, EvalCommentsArgumentAndCalleeEffects) {
    boolean(R"JS(
let order='',marker={toString(){order+='c';return '/* comment */'}};
if(eval(marker)!==marker||(0,eval)(marker)!==marker||order!=='')throw 1;
function argument(){order+='a';return '/* comment */'}
if(eval(argument())!==undefined||order!=='a')throw 2;
function shadow(eval){return eval('/* comment */')}
if(shadow(function(text){order+='s';return text.length})!==13||order!=='as')throw 3;
let other={eval:function(text){order+='m';return text.length}};
if(other.eval('/* comment */')!==13||order!=='asm')throw 4;
true;
)JS");
}

TEST_F(JsMvpLmd, EvalCommentsRepeatedUnicode) {
    boolean(R"JS(
let seen=0;
for(let i=0;i<256;i++){
  let text=String.fromCharCode(i),yy=0;
  let result=eval('//var '+text+'yy = -1');
  if(i===10||i===13){if(result!==-1||yy!==-1)throw i;seen++}
  else if(result!==undefined||yy!==0)throw i;
  if(eval('/*var '+text+'yy = -1*/')!==undefined)throw i;
}
seen===2;
)JS");
}


TEST(JsDynamicEvalSource, BlankProofConsumesAllBytes) {
    struct Case { const char* source; size_t length; bool blank; };
    const Case cases[] = {
        {"", 0, true},
        {"\v\f\xC2\xA0\xEF\xBB\xBF", 7, true},
        {"/*x\0y*/", 7, true},
        {"//x\0y", 5, true},
        {"\0", 1, false},
        {" \0 ", 3, false},
        {"/*x*/\0", 6, false},
        {"/*", 2, false},
        {"/*/", 3, false},
        {"//x\n42", 6, false},
        {"//x\r42", 6, false},
        {"//x\xE2\x80\xA8" "42", 8, false},
        {"//x\xE2\x80\xA9" "42", 8, false},
        {"#!x", 3, true},
        {"#!x\n/*ok*/", 10, true},
        {" #!x", 4, false},
        {"\n#!x", 4, false},
        {"/*x*/#!x", 8, false},
        {"#!x\n#!y", 7, false},
        {"<!--x", 5, true},
        {"\n-->x", 5, true},
        {";", 1, false},
        {"'directive'", 11, false},
    };
    EXPECT_FALSE(js_eval_source_is_blank(NULL));
    for (const Case& entry : cases) {
        alignas(String) char storage[sizeof(String) + 64] = {};
        ASSERT_LT(entry.length, 64u);
        String* source = (String*)storage;
        source->len = (uint32_t)entry.length;
        memcpy(source->chars, entry.source, entry.length);
        EXPECT_EQ(js_eval_source_is_blank(source), entry.blank) << entry.source;
    }
}

TEST_F(JsMvpLmd, ReflectedFunctionIdentityAndMutation) {
    boolean(R"JS(
        globalThis.self=globalThis;
        function tree(n) { return n ? {left:tree(n-1), right:tree(n-1)} : {value:3}; }
        tree(3).left.right.left.value===3 && self.tree===tree;
    )JS");
    boolean("globalThis.self=globalThis;function f(){return 1}let a=f();self.f=function(){return 'changed'};a===1&&f()==='changed'");
    boolean("function f(){return 1}let g=globalThis;let key=['f'][0];g[key]=function(){return 4};f()===4");
    boolean("function f(){return 1}let a=f();Object.assign(globalThis,{f:function(){return 5}});a===1&&f()===5");
    boolean("function f(){return 1}Object.defineProperty(globalThis,'f',{value:function(){return 6}});f()===6");
    boolean("function f(){return 1}let g=globalThis;for(g.f of [function(){return 7}]){}f()===7");
    boolean("function f(){return 1}globalThis.f=2;globalThis.f++;f===3");
    boolean("function f(){return 1}globalThis.self=globalThis;f=function(){return 8};f()===8");
    boolean("function f(){return 1}globalThis.self=globalThis;eval('f=function(){return 9}');f()===9");
    boolean("globalThis.self=globalThis;function length(){return 1}Array.prototype.push.call(self,4);typeof length==='number'");
}

TEST_F(JsMvpLmd, ReflectedIntrinsicFactsRequireClosedMutations) {
    boolean("globalThis.self=globalThis;function f(n){let a=new Array(Math.ceil(n));a[0]=String(n);return a.length===3&&a[0]==='2.5'}f(2.5)");
    boolean("globalThis.self=globalThis;Math={ceil:function(){return 'changed'}};Math.ceil(2)==='changed'");
    boolean("globalThis.self=globalThis;let alias=Math;alias.ceil=function(){return 'alias'};Math.ceil(2)==='alias'");
    boolean("globalThis.self=globalThis;String.fromCharCode=function(){return 7};String.fromCharCode(65)===7");
    boolean("globalThis.self=globalThis;Number.parseInt=function(){return 'number'};Number.parseInt('1')==='number'");
    boolean("globalThis.self=globalThis;String.prototype.substring=function(){return 8};'abc'.substring(1)===8");
    boolean("globalThis.self=globalThis;let key=['Math'][0];self[key]={ceil:function(){return 9}};Math.ceil(2)===9");
    boolean("globalThis.self=globalThis;Object.defineProperty(self,'String',{get:function(){return function(){return 10}}});String(1)===10");
    boolean("globalThis.self=globalThis;let C=Function;self.Function('String=function(){return 11}')();String(1)===11");
}

TEST_F(JsMvpLmd, ReflectedGlobalDomainsAndEscapedCalls) {
    boolean("globalThis.self=globalThis;function f(){return 1}let first=f();for(self.f of [()=>2]){}first===1&&f()===2");
    boolean("globalThis.self=globalThis;var x=1;for(self.x in {text:2}){}x==='text'");
    boolean("globalThis.self=globalThis;for(Math.abs of [()=>7]){}Math.abs(-1)===7");
    boolean("globalThis.self=globalThis;function read(){return x}let before=read();var x=3;before===undefined&&read()===3");
    boolean("globalThis.self=globalThis;var x=1;function change(){x='text'}let before=x;change();before===1&&x==='text'");
    boolean("globalThis.self=globalThis;var x=1;self.x='changed';x==='changed'");
    boolean("globalThis.self=globalThis;function f(x){return x}let first=f(1);self.f('text')==='text'&&first===1");
    boolean("globalThis.self=globalThis;function f(x){return x}let values=Object.values(self);let ok=false;for(let v of values){if(v===f)ok=v('text')==='text'}ok");
    boolean("globalThis.self=globalThis;function f(x){return x}function g(){return f}let first=f(1);g()('text')==='text'&&first===1");
    boolean("globalThis.self=globalThis;function tree(n,tag){return n?{left:tree(n-1,tag),right:tree(n-1,tag)}:{tag:tag}}var depth=3;tree(depth,String(4)).left.right.left.tag==='4'");
}

TEST_F(JsMvpLmd, ForInKeysJoinOtherBindingWrites) {
    boolean("let keys='';for(const key in {a:1,b:2})keys+=key;keys==='ab'");
    boolean("let result=0;for(let key in {a:1,b:2}){result+=typeof key==='string';key=1.25;result+=key}result===4.5");
    boolean("let key=2.5;let initial=key;for(key in {a:1}){}initial===2.5&&key==='a'");
    boolean("let key;for(key of [1.25]){}let initial=key;for(key in {a:1}){}initial===1.25&&key==='a'");
    boolean("let key;for(key in {a:1}){}for(key of [1.25]){}key===1.25");
    boolean("function f(key){let initial=key;for(key in {a:1}){}return initial}f(1.25)===1.25&&f({a:2}).a===2");
    boolean("let read;for(let key in {a:1}){read=()=>key;key=1.25}read()===1.25");
}

TEST_F(JsMvpLmd, ReflectedPrototypeSetupAndEscapes) {
    boolean("function C(x){this.x=x}C.prototype.read=function(){return this.x};globalThis.self=globalThis;let c=new C(3);c.read()===3&&self.C===C");
    boolean("globalThis.self=globalThis;function C(x){this.x=x}C.prototype.read=function(){return this.x};let c=new C(3);c.read()===3");
    boolean("function C(){}C.prototype.x=1;globalThis.self=globalThis;let c=new C();let before=c.x;self.C.prototype.x='later';before===1&&c.x==='later'");
    boolean("function C(){}C.prototype.x=1;globalThis.self=globalThis;let c=new C();let before=c.x;let p=C.prototype;p.x=2;before===1&&c.x===2");
    boolean("function C(){}C.prototype.x=1;globalThis.self=globalThis;let c=new C();let before=c.x;Object.defineProperty(C.prototype,'x',{get:function(){return 4}});before===1&&c.x===4");
    boolean("function C(){}C.prototype.x=1;globalThis.self=globalThis;let c=new C();let before=c.x;eval('C.prototype.x=5');before===1&&c.x===5");
    boolean("function C(x){this.x=x}C.prototype.read=function(){return this.x};globalThis.self=globalThis;let first=new C(1);self.C=function(x){this.x=x+'!'};first.read()===1&&new C('text').x==='text!'");
}

TEST_F(JsMvpLmd, OwnPresenceRetainsAccessorsAndMappedArguments) {
    boolean("let calls=0,o={get x(){calls++;return 3}};let found='x' in o&&Object.hasOwn(o,'x');found&&calls===0&&o.x===3&&calls===1");
    boolean("function f(a){let first=arguments[0];a=4;return first===3&&arguments[0]===4&&'0' in arguments&&Object.hasOwn(arguments,'0')}f(3)");
    boolean("let p={get x(){return this.y}},o=Object.create(p);o.y=5;('x' in o)&&!Object.hasOwn(o,'x')&&o.x===5");
    boolean("let calls=0,p=new Proxy({x:3},{has(t,k){calls++;return false},get(t,k){return 7}});!('x' in p)&&calls===1&&p.x===7");
    boolean("let key=Symbol('x'),o={[key]:1.25};Object.hasOwn(o,key)&&key in o&&o[key]===1.25");
}

TEST_F(JsMvpLmd, OptionalNumericEqualityWithoutBoxing) {
    boolean(R"JS(
        function compare(a,b,i,j) { return [a[i]===b[j],a[i]==b[j],a[i]!==b[j]]; }
        let a=new Float64Array(4),b=new Int32Array(2);
        a[0]=1.25;a[1]=NaN;a[2]=-0;a[3]=2;b[1]=2;
        let missing=compare(a,b,8,8),nan=compare(a,a,1,1),zero=compare(a,b,2,0);
        let different=compare(a,b,0,1),oneMissing=compare(a,b,3,8),same=compare(a,b,3,1);
        missing[0]&&missing[1]&&!missing[2]&&!nan[0]&&!nan[1]&&nan[2]&&
        zero[0]&&zero[1]&&!zero[2]&&!different[0]&&!oneMissing[0]&&same[0];
    )JS");
    boolean("let a=new Float64Array(2);a[0]=1.25;a[1]=NaN;let ok=true;for(let i=0;i<4;i++){let x=a[i];ok=ok&&(x===1.25)===(i===0)&&(x==2)===false}ok");
}

TEST_F(JsMvpLmd, OrderedLoopNumericFactsEndAtWrites) {
    boolean("function f(){let a=new Int32Array(2);a[0]=1;let i=0,count=0;while(i<3){count++;i=a[i];i++;}return count===2&&Number.isNaN(i)}f()");
    boolean("function f(){let a=new Int32Array(1);a.undefined=7;let i=0;while(i<2){let before=typeof i;i=a[8];return before==='number'&&typeof i==='undefined'&&a[i]===7}return false}f()");
    boolean("function f(){let a=new Int32Array(1);let i=0;while(i<2){while(i<1){i=a[8];break}return typeof i==='undefined'}return false}f()");
    boolean("function f(){let a=new Int32Array(1);let i=0;while(i<(i=a[8],2)){return typeof i==='undefined'}return false}f()");
    boolean("function f(){let a=new Int32Array(1);let i=0;function change(){i=a[8]}while(i<2){change();return typeof i==='undefined'}return false}f()");
    boolean("function f(){let a=new Int32Array(1);let i=a[8],count=0;do{count+=typeof i==='undefined';i=1}while(i<1);return count===1}f()");
    boolean("function f(){let a=new Int32Array(1);a.undefined=7;let i=0,j=0,k=0;while(i<2){while(j<2){k+=a[i];i=a[8];j++}break}return k===7}f()");
    boolean("function f(){let a=new Int32Array(1);a.undefined=7;let i=0,k=0;while(i<2){again:for(let j of [0,1]){k+=a[i];i=a[8]}break}return k===7}f()");
    boolean("function f(flag){let a=new Int32Array(1);a.undefined=7;let i=0;while(i<2){if(flag){i=a[9]}else{i=a[8]}return a[i]===7}return false}f(true)&&f(false)");
    boolean("function f(flag){let a=new Int32Array(1);a.undefined=7;let i=0;while(i<2){if(flag){i=a[9]}else{if(a[i]!==0)return false}return a[i]===(flag?7:0)}return false}f(true)&&f(false)");
}

TEST_F(JsMvpLmd, OrderedLoopIntegerFactsPreserveNumberEdges) {
    boolean("function f(){let a=new Int32Array([1,2]);let i=0,n=0;while(i<3){n++;i=a[i];i++}return n===2&&Number.isNaN(i)}f()");
    boolean("function f(){let a=new Float64Array([1.25]);let i=0,n=0;while(i<2){if(n++)return i===1.25;i=a[0]}return false}f()");
    boolean("function f(){let a=new Float64Array([-0]);let i=0,n=0;while(i<1){if(n++)return Object.is(i,-0);i=a[0]}return false}f()");
    boolean("function f(){let a=new Int32Array(1);let i=9007199254740990,n=0;while(i<9007199254740991){n++;if(n>2)i=a[8];i+=9007199254740991}return n===1&&i===18014398509481980}f()");
    boolean("function f(){let a=new Int32Array(1);let i=0,n=0;while(i<1){if(n++)return i===-1;if(n>2)i=a[8];i--}return false}f()");
    boolean("function f(){let a=new Int32Array([0]);let i=Infinity,n=0;while(i<2){n++;i=a[0]}return n===0&&i===Infinity}f()");
    boolean("function f(){let a=new Int32Array([-2147483648]);let i=0,n=0;while(i<2){n++;if(n===2)return i===-2147483648&&a[i]===undefined;i=a[0]}return false}f()");
    boolean("function f(){let a=new Int32Array(1);a.undefined=1.25;let i=0,n=0;while(i<2){if(n++)return i===1.25;i=a[a[8]]}return false}f()");
    boolean("function f(){let a=[5];a[-1]=7;let i=0,n=0,s=0;while(n<2){s+=a[i];i--;n++}return s===12}f()");
    boolean("function f(){let a=[5];a[0.5]=7;let i=0,n=0,s=0;while(n<2){s+=a[i];i+=0.5;n++}return s===12}f()");
    boolean("function f(){let a=[];a[9007199254740991]=7;a[9007199254740992]=9;let i=9007199254740991,n=0,s=0;while(n<2){s+=a[i];i++;n++}return s===16}f()");
    boolean("function f(){let a=[7];let i=-0,n=0;while(n<1){if(a[i]!==7||!Object.is(i,-0))return false;i++;n++}return true}f()");
    boolean("function f(){let jumps=new Int32Array([1]);let i=0,n=0,s=0;while(i<3){s+='abc'.charCodeAt(i);n++;i=jumps[i];i++}return n===2&&s===196&&Number.isNaN(i)}f()");
    boolean("function f(){let jumps=new Int32Array(1);let i=-1;while(i<2){let code='abc'.charCodeAt(i),bits=(i+1)|0;i=jumps[4];return Number.isNaN(code)&&bits===0&&'abc'.charCodeAt(i)===97}return false}f()");
    boolean("function f(){let i=4294967297,n=0;while(i<4294967298){n=(i|0);i++}return n===1&&Number.isNaN('abc'.charCodeAt(i))}f()");
}

TEST_F(JsMvpLmd, IntrinsicArrayResultsRetainSpeciesAndOwnership) {
    boolean(R"JS(
        let hits=0,a=[1,2],r=a.map(x=>{
            Object.defineProperty(Array.prototype,'0',{get(){return 99},set(v){hits++},configurable:true});
            return x*2;
        });
        delete Array.prototype[0];r[0]===2&&r[1]===4&&hits===0;
    )JS");
    boolean(R"JS(
        let keys='',a=[1,2];
        function Species(){return new Proxy({}, {defineProperty(t,k,d){keys+=k;return Reflect.defineProperty(t,k,d)}})}
        a.constructor={[Symbol.species]:Species};let r=a.map(x=>x+1);
        keys==='01'&&r[0]===2&&r[1]===3&&!Array.isArray(r);
    )JS");
    boolean(R"JS(
        let target=[],a=[1],caught=false;
        a.constructor={[Symbol.species]:function(){return target}};
        try{a.map(x=>{Object.defineProperty(target,'0',{value:9,writable:false,configurable:false});return x})}
        catch(e){caught=e instanceof TypeError}caught&&target[0]===9;
    )JS");
    boolean(R"JS(
        let a=[5e-324,-0,NaN,1.25],b=a.map(x=>x),c=a.filter(()=>true),d=a.slice(),e=a.concat([]),f=a.splice(0,4);
        let ok=true;for(let r of [b,c,d,e,f])ok=ok&&r[0]===5e-324&&Object.is(r[1],-0)&&Number.isNaN(r[2])&&r[3]===1.25;
        ok;
    )JS");
}

TEST_F(JsMvpLmd, ArrayEditsObserveNumericPrototypeShapeChanges) {
    boolean("let p={},a=[];Object.setPrototypeOf(a,p);Array.prototype.push.call(a,1);let hits=0;Object.defineProperty(p,'1',{set(v){hits+=v},configurable:true});Array.prototype.push.call(a,3);hits===3&&a.length===2&&!Object.hasOwn(a,'1')");
    boolean("let p={},a=[];Object.setPrototypeOf(a,p);Array.prototype.push.call(a,1);Object.defineProperty(p,'1',{value:7,writable:false});let caught=false;try{Array.prototype.push.call(a,3)}catch(e){caught=e instanceof TypeError}caught&&a.length===1&&a[1]===7");
    boolean("let p={},a=[1,2];Object.setPrototypeOf(a,p);Array.prototype.unshift.call(a,0);p[3]=8;delete a[2];let r=Array.prototype.pop.call(a);r===undefined&&a.length===2&&p[3]===8");
    boolean("let p={foo:1},q={foo:1};p.bar=2;q[0]=9;let a=[],b=[];Object.setPrototypeOf(a,p);Object.setPrototypeOf(b,q);Array.prototype.push.call(a,3);Array.prototype.push.call(b,4);a[0]===3&&b[0]===4&&p[0]===undefined&&q[0]===9");
    boolean("let p={'01':8},a=[];p[Symbol('0')]=9;Object.setPrototypeOf(a,p);Array.prototype.push.call(a,3);delete p['01'];Array.prototype.unshift.call(a,4);a.length===2&&a[0]===4&&a[1]===3");
    boolean("let p={},a=[1];Object.setPrototypeOf(a,p);Array.prototype.push.call(a,2);Object.setPrototypeOf(p,{2:9});a.length=3;Array.prototype.unshift.call(a,0);a.length===4&&a[3]===9");
    for (const char* source : {"({first:1,last:2})", "({'0':1,last:2})"}) {
        Item object = run(source);
        ASSERT_EQ(get_type_id(object), LMD_TYPE_MAP);
        TypeMap* shape = (TypeMap*)object.map->type;
        EXPECT_EQ((int64_t)shape->digit_key_length, shape->length + 1);
        EXPECT_EQ(shape->has_digit_key, source[2] == '\'');
    }
}

TEST_F(JsMvpLmd, ReflectedCompatibleFieldStorage) {
    boolean("globalThis.self=globalThis;var x=1.25;Object.defineProperty(self,'x',{enumerable:true});let ok=true;for(let v of [0,2,5e-324,-0,NaN,Infinity,3]){x=v;let d=Object.getOwnPropertyDescriptor(self,'x');ok=ok&&Object.is(d.value,v)&&Object.is(x,v)&&d.writable&&d.enumerable}ok");
    boolean("globalThis.self=globalThis;var x={a:1};let old=x;Object.defineProperty(self,'x',{writable:true});x=null;let empty=x===null;x={b:2};empty&&old.a===1&&x.b===2");
    boolean("let o={x:1.25};Object.defineProperty(o,'x',{writable:false});!Reflect.set(o,'x',2)&&o.x===1.25");
    boolean("let hits=0,o={set x(v){hits+=v}};Object.defineProperty(o,'x',{configurable:true});o.x=2;hits===2");
    Item result = run("globalThis.self=globalThis;var x=1.25;Object.defineProperty(self,'x',{writable:true});x=2;self");
    ASSERT_EQ(get_type_id(result), LMD_TYPE_MAP);
    ShapeEntry* field = typemap_hash_lookup((TypeMap*)result.map->type, "x", 1);
    ASSERT_NE(field, nullptr);
    EXPECT_EQ(field->type, &TYPE_FLOAT);
}

TEST_F(JsMvpLmd, FreshFlatteningRetainsSpeciesAndSourceEffects) {
    boolean("let calls=0,a=[[1],[2]];Object.defineProperty(a,'0',{get(){calls++;delete a[1];return [3]}});let r=a.flat();calls===1&&r.length===1&&r[0]===3");
    boolean("let keys='',a=[[1],[2]];a.constructor={[Symbol.species]:function(){return new Proxy({}, {defineProperty(t,k,d){keys+=k;return Reflect.defineProperty(t,k,d)}})}};let r=a.flat();keys==='01'&&r[0]===1&&r[1]===2&&!Array.isArray(r)");
    boolean("let a=[[1],[2]];a.constructor={[Symbol.species]:function(){return a}};let r=a.flat();r===a&&a.length===2&&a[0]===1&&a[1]===2");
    boolean("let hits=0,a=[1,2];let r=a.flatMap(x=>{Object.defineProperty(Array.prototype,'0',{set(v){hits++},configurable:true});return [x,x+1]});delete Array.prototype[0];hits===0&&r.join(',')==='1,2,2,3'");
    boolean("let a=[5e-324,-0,NaN,1.25],b=[a].flat(),c=a.flatMap(x=>[x]);let ok=true;for(let r of [b,c])ok=ok&&r[0]===5e-324&&Object.is(r[1],-0)&&Number.isNaN(r[2])&&r[3]===1.25;ok");
}

TEST_F(JsMvpLmd, CheckedGlobalReadRetainsResolutionOrder) {
    boolean("globalThis.self=globalThis;let calls=0;Object.defineProperty(self,'checkedValue',{get(){calls++;return 7},configurable:true});checkedValue===7&&calls===1");
    boolean("globalThis.self=globalThis;let caught=false;try{missingCheckedName}catch(e){caught=e instanceof ReferenceError}caught&&typeof missingCheckedName==='undefined'");
    boolean(R"JS(
        let log='';
        Object.setPrototypeOf(globalThis,new Proxy({}, {
            has(t,k){if(k==='checkedInherited'){log+='h';return true}return false},
            get(t,k){if(k==='checkedInherited'){log+='g';return 8}}
        }));
        checkedInherited===8&&log==='hg';
    )JS");
    boolean(R"JS(
        let log='';
        Object.setPrototypeOf(globalThis,new Proxy({}, {
            has(t,k){if(k==='checkedShadow'){log+='h';Object.defineProperty(globalThis,k,{value:9});return true}return false},
            get(t,k){log+='g';return 8}
        }));
        checkedShadow===9&&log==='h';
    )JS");
    boolean("let caught=false;Object.setPrototypeOf(globalThis,new Proxy({},{has(){throw 17}}));try{missingCheckedName}catch(e){caught=e===17}caught");
}

TEST_F(JsMvpLmd, BareLexicalInitializationRemainsInNumericDomain) {
    boolean(R"JS(
        function scan(s) {
            let out='';
            for(let i=0;i<s.length;i++) {
                let remaining;
                for(i++;i<s.length;i++) {
                    if(remaining) { remaining--; }
                    else if(s[i]==='u') { remaining=2; }
                    else out+=s[i];
                }
            }
            return out;
        }
        scan('Xhello')==='hello' && scan('Xu12z')==='z';
    )JS");
    boolean("let result=0;for(let i=0;i<3;i++){let x;result+=x===undefined?1:100;x=i+1;if(x)result+=x;}result===9");
    boolean("function f(){let x;let read=()=>x;let absent=read()===undefined;x=4;return absent&&read()===4}f()");
    boolean("function f(){var x=3;var x;return x}f()===3");
}
TEST_F(JsMvpLmd, FixedLiteralGenericSlotsRetainValuesAndOrder) {
    boolean(R"JS(
        function make(value) { return {flag:true, value:value, tail:7}; }
        let values=[undefined,null,true,9,1.25,-0,5e-324,NaN,'text',{},[],function(){},4n,Symbol('x')];
        let ok=true;
        for(let value of values) {
            let object=make(value), read=object.value;
            object.value='changed';
            ok=ok&&Object.is(read,value)&&object.flag&&object.tail===7&&object.value==='changed';
        }
        ok;
    )JS");
    boolean(R"JS(
        let order='';
        function value(n) { order+=n; return n===1?5e-324:{n:n}; }
        function make() { return {first:value(1), second:value(2)}; }
        let a=make(),b=make();
        a.first=7;
        order==='1212'&&b.first===5e-324&&a.second.n===2&&b.second.n===2&&a.second!==b.second;
    )JS");
    boolean("function f(x){return {value:x,method(){return super.toString.call(this)}}}let o=f(3);o.method()==='[object Object]'&&o.value===3");
    boolean("function f(x){let o={value:x};return [o.value,(o.value=7),o.value]}let a=f(5e-324);a[0]===5e-324&&a[1]===7&&a[2]===7");
}
TEST_F(JsMvpLmd, RawTypedOptionalKeysPreserveNumericPayloads) {
    boolean("function f(){let a=new Int32Array(2),keys=[0,1];a[0]=7;a[keys[9]]=1.25;"
        "let x=a[keys[9]],y=a[keys[0]],z=a[8];return x===1.25&&y===7&&z===undefined}f()");
    boolean("function f(){let a=new Uint8Array(2),keys=[0];a[keys[3]]=NaN;let x=a[keys[3]];"
        "return x!==x&&a[9]===undefined&&a[keys[0]]===0}f()");
    boolean("function f(){let a=new Int32Array(2),keys=[0];a[keys[3]]=-0;let x=a[keys[3]];"
        "return 1/x===-Infinity&&a[keys[0]]===0}f()");
    boolean("function f(){let a=new Int32Array(2),keys=[0];a[keys[3]]=5e-324;let x=a[keys[3]];"
        "a[keys[3]]=7;return x===5e-324&&a[keys[3]]===7}f()");
    boolean("function f(){let a=new Int32Array(2),keys=[0];a[keys[3]]='value';"
        "let x=a[keys[3]];return x==='value'&&a[keys[0]]===0}f()");
}
TEST_F(JsMvpLmd, FullParityReplaceAllProtocolsAndUtf16) {
    boolean(R"JS('\ud83d\ude00'.replaceAll('', '_') === '_\ud83d_\ude00_')JS");
    boolean("let positions=[];let s='aba'.replaceAll('a',(m,p,source)=>{positions.push(p);return source.length+p});s==='3b5'&&positions.join(',')==='0,2'");
    boolean("let order='';let pattern={get [Symbol.match](){order+='m';return true},get flags(){order+='f';return 'g'},get [Symbol.replace](){order+='r';return function(s,v){return s===receiver&&v===7}}};let receiver={toString(){throw 9}};String.prototype.replaceAll.call(receiver,pattern,7)&&order==='mfr'");
    boolean("Object.defineProperty(Number.prototype,Symbol.replace,{get(){throw 7}});'a1b1'.replaceAll(1,'x')==='axbx'");
    error("'a'.replaceAll(/a/,'x')", "TypeError");
}
TEST_F(JsMvpLmd, FullParityNumericFormattingAndBigIntWidths) {
    boolean("let number=new Number(1.25),digits={valueOf(){number.valueOf=()=>7;return 2}};number.toPrecision(digits)==='1.3'&&(1.25).toExponential(2)==='1.25e+0'&&Infinity.toPrecision(Infinity)==='Infinity'");
    boolean("let order='';let bits={valueOf(){order+='b';return 8}},value={valueOf(){order+='v';return 255n}};BigInt.asIntN(bits,value)===-1n&&order==='bv'&&BigInt.asUintN(8,-1n)===255n");
    boolean("let ran=false;try{BigInt.asIntN(-1,{valueOf(){ran=true;return 0n}})}catch(e){if(!(e instanceof RangeError))throw e}!ran");
    error("(1).toPrecision(0)", "RangeError");
    error("BigInt.asUintN(8,1)", "TypeError");
    boolean(R"JS(escape('a \ud83d\ude00')==='a%20%uD83D%uDE00'&&unescape('%uD83D%uDE00')==='\ud83d\ude00')JS");
}
TEST_F(JsMvpLmd, FullParityLegacyAccessorDescriptors) {
    boolean("let key=Symbol(),o={},getter=()=>7,setter=function(v){this.value=v};o.__defineGetter__(key,getter);o.__defineSetter__(key,setter);let d=Object.getOwnPropertyDescriptor(o,key);o[key]=9;d.get===getter&&d.set===setter&&d.enumerable&&d.configurable&&o[key]===7&&o.value===9&&o.__lookupGetter__(key)===getter&&Object.create(o).__lookupSetter__(key)===setter");
    boolean("let p={get x(){return 1}},o=Object.create(p);Object.defineProperty(o,'x',{value:3});o.__lookupGetter__('x')===undefined");
    boolean("let seen=0;let key={toString(){seen++;throw 7}};try{Object.prototype.__defineGetter__.call({},key,0)}catch(e){if(!(e instanceof TypeError))throw e}seen===0");
    error("Object.prototype.__defineGetter__.call(null,'x',()=>1)", "TypeError");
    boolean("Object.prototype.get=()=>7;let o={x:3};o.__lookupGetter__('x')===undefined");
}

TEST_F(JsMvpLmd, FullParityReflectMethodNames) {
    boolean("let ok=true;for(let key of ['apply','construct','defineProperty','deleteProperty','get','getOwnPropertyDescriptor','getPrototypeOf','has','isExtensible','ownKeys','preventExtensions','set','setPrototypeOf']){let f=Reflect[key],d=Object.getOwnPropertyDescriptor(f,'name'),p=Object.getOwnPropertyDescriptor(Reflect,key);ok=ok&&f.name===key&&!d.writable&&!d.enumerable&&d.configurable&&p.writable&&!p.enumerable&&p.configurable}ok");
}

TEST_F(JsMvpLmd, FullParityRegExpUnicodeSetsAndProperties) {
    boolean(R"JS(/^\d+$/v.test('0123') && !/^\d+$/v.test('abc'))JS");
    boolean(R"JS(/[[a-z]--[aeiou]]/v.test('b') && !/[[a-z]--[aeiou]]/v.test('a') && /[[a-z]&&[bcd]]/v.test('c'))JS");
    boolean(R"JS(/[\q{ab|cd}]/v.exec('zcd')[0]==='cd')JS");
    boolean(R"JS(let r=/./v;r.unicode===false&&r.unicodeSets===true&&r.flags==='v'&&r.exec('\uD83D\uDE00')[0].length===2)JS");
    boolean(R"JS('\uD83D\uDE00'.match(/(?:)/gv).length===2&&'\uD83D\uDE00'.replace(/(?:)/gv,'-')==='-\uD83D\uDE00-')JS");
    boolean(R"JS(/[\p{ASCII}]/u.test('7')&&!/[\p{ASCII}]/u.test('\uD83D\uDE00')&&/[\P{ASCII}]/u.test('\uD83D\uDE00')&&!/[\P{ASCII}]/u.test('A'))JS");
    boolean(R"JS(/[\p{ASCII}\p{Script=Greek}]/u.test('\u03A9')&&/\p{ASCII}/.test('p{ASCII}'))JS");
    boolean(R"JS(/k/iu.test('\u212A')&&/[a-z]/iu.test('\u017F')&&/[\u03c2]/iu.test('\u03a3')&&!/k/i.test('\u212A')&&!/i/iu.test('\u0130'))JS");
    boolean(R"JS(/[\u0390]/iu.test('\u1fd3')&&/[\u1fd3]/iu.test('\u0390')&&/[\u03b0]/iu.test('\u1fe3')&&/[\u1fe3]/iu.test('\u03b0')&&/[\ufb05]/iu.test('\ufb06')&&/[\ufb06]/iu.test('\ufb05'))JS");
    boolean(R"JS(/^(\u0390)\1$/iu.test('\u1fd3\u0390')&&!/^\u0390$/iu.test('\u03b9\u0308\u0301')&&!/^\ufb05$/iu.test('st'))JS");
    boolean(R"JS(/(?<=\1(\u0390))$/iu.test('\u0390\u1fd3')&&/(?<=\1(\u0390))$/iu.test('\u1fd3\u0390')&&/^(k)\1$/iu.test('\u212Ak')&&/(?<=\1(k))$/iu.test('k\u212A'))JS");
    boolean(R"JS(/\b\w\b/iu.test('\u017F')&&!/\b\w\b/i.test('\u017F')&&/[\-\p{ASCII}]/u.test('7')&&/[-\p{ASCII}]/u.test('-')&&/[\p{ASCII}-]/u.test('-'))JS");
    boolean(R"JS(let bad=['(',')','{','}','/','-','|','&&','!!','##','$$','%%','**','++',',,','..','::',';;','<<','==','>>','??','@@','^^^','``','~~'];let ok=true;for(let text of bad){try{new RegExp('['+text+']','v');ok=false}catch(e){ok=ok&&e instanceof SyntaxError}}ok&&/[\!\-\&]/v.test('!')&&/[a-z]/v.test('m')&&/[^^]/v.test('a'))JS");
    boolean(R"JS(let r=new RegExp('\\c\u0410');r.test('\\c\u0410')&&!r.test('c\u0410')&&/[\c!]/.test('\\')&&/[\c!]/.test('c')&&/[\c!]/.test('!')&&/[\c1]/.test('\x11')&&/[\c_]/.test('\x1f'))JS");
    error(R"JS(new RegExp('[\\B]', 'u'))JS", "SyntaxError");
    error(R"JS(new RegExp('[^\\p{Basic_Emoji}]', 'v'))JS", "SyntaxError");
    error(R"JS(new RegExp('[^[\\q{ab}]--[\\q{ab}]]', 'v'))JS", "SyntaxError");
    boolean(R"JS(/[^\q{a}]/v.test('b')&&/[^[[\q{ab}]&&[a]]]/v.test('c'))JS");
    error(R"JS(new RegExp('\\p{ASCII=F}', 'u'))JS", "SyntaxError");
    error(R"JS(new RegExp('[\\p{ASCII=F}]', 'u'))JS", "SyntaxError");
}
TEST_F(JsMvpLmd, FullParityFromEntriesClosingAndScalarOwnership) {
    boolean("let key=Symbol(),o=Object.fromEntries([['__proto__',7],[key,5e-324],['a',1],['a',2]]);Object.getPrototypeOf(o)===Object.prototype&&o.__proto__===7&&o[key]===5e-324&&o.a===2");
    boolean("let closed=0,thrown={};function* entries(){try{yield {get 0(){throw thrown}}}finally{closed++}}let same=false;try{Object.fromEntries(entries())}catch(e){same=e===thrown}same&&closed===1");
    boolean("let order='';let entry={get 0(){order+='k';return {toString(){order+='c';return 'x'}}},get 1(){order+='v';return 5e-324}};let o=Object.fromEntries([entry]);o.x===5e-324&&order==='kvc'");
}
TEST_F(JsMvpLmd, FullParityInterpretedRegExpOwnership) {
    boolean(R"JS(let r=eval('/a+/g');r instanceof RegExp&&r.exec('baa')[0]==='aa'&&r.lastIndex===3)JS");
    boolean(R"JS(let r=(0,eval)('/[a-z]/v');r instanceof RegExp&&r.unicodeSets&&r.test('q'))JS");
    boolean(R"JS(let r=Function('return /(?<letter>a)/d')();let m=r.exec('ba');m.groups.letter==='a'&&m.indices.groups.letter[0]===1&&m.indices.groups.letter[1]===2)JS");
    boolean(R"JS(let Original=RegExp;RegExp=function(){throw 7};let r=eval('/a/');r instanceof Original&&r.test('a'))JS");
    boolean(R"JS(let ok=false;try{eval('/[!!]/v')}catch(e){ok=e instanceof SyntaxError}ok)JS");
}
TEST_F(JsMvpLmd, FullParityStatementPositionsRejectBeforeExecution) {
    const char* sources[] = {
        "'use strict';if(true) function f(){}",
        "if(true) async function f(){}",
        "if(true) function* f(){}",
        "if(true) label:function f(){}",
        "while(false) function f(){}",
        "for(;;) let x=1;",
        "do class C{} while(false);",
        "'use strict';label:function f(){}",
        "if(true)const x=1;else 0",
        "{async function f(){} function f(){}}",
        "{function* f(){} function f(){}}",
        "{let x;var x;}",
        "{var x;class x{}}",
        "{let x;{var x;}}",
        "let x;var x;",
        "if({1}){}",
        "({'x'})",
        "let x=1;({[x]})",
        R"JS(throw 7; /\p{ASCII=F}/u)JS",
        R"JS(if(false){ /[\uFFFF-\p{Hex}]/u })JS",
        R"JS(function f(){ /a/uv })JS",
        R"JS(throw 7; /[!!]/v)JS",
        R"JS(throw 7; /[^\p{Basic_Emoji}]/v)JS",
        R"JS(function f(){ /[^[\q{ab}]--[\q{ab}]]/v })JS"
    };
    for (const char* source : sources) {
        mvp_lmd_destroy(execution);
        execution = mvp_lmd_execute(source, strlen(source));
        MvpLmdFailurePhase phase = MVP_LMD_SUCCESS;
        const char* type = NULL;
        ASSERT_NE(mvp_lmd_diagnostic(execution, &phase, &type), nullptr) << source;
        EXPECT_EQ(phase, MVP_LMD_PARSE_FAILURE) << source;
        ASSERT_NE(type, nullptr);
        EXPECT_STREQ(type, "SyntaxError");
    }
    numeric("var n=0;if(true)function f(){return 7} f()", 7);
    numeric("label:function f(){return 9} f()", 9);
    numeric("if(true){let x=1;}7", 7);
    numeric("{function f(){return 1}function f(){return 2}}f()", 2);
    numeric("var x=7;{let x=1;}x", 7);
}
TEST_F(JsMvpLmd, FullParityJsonReviverOrderAndOwnership) {
    boolean(R"JS(let order=[];let o=JSON.parse('{"a":[1,2],"b":3}',function(k,v){order.push(k);if(k==='0')return 5e-324;if(k==='1')return undefined;return v});order.join(',')==='0,1,a,b,'&&o.a[0]===5e-324&&!(1 in o.a)&&o.a.length===2&&o.b===3)JS");
    boolean(R"JS(let calls=0;JSON.parse('null',function(k,v){calls++;return k===''&&v===null?7:0})===7&&calls===1&&JSON.parse('3',{})===3)JS");
    boolean(R"JS(let o=JSON.parse('{"a":1,"b":2}',function(k,v){if(k==='a'){Object.defineProperty(this,'b',{value:9,configurable:false});return v}if(k==='b')return undefined;return v});o.a===1&&o.b===9)JS");
    boolean(R"JS(let error={};let same=false;try{JSON.parse('[0,0]',function(k,v){if(k==='0')this[1]=new Proxy([],{get(t,k){if(k==='length')throw error;return t[k]}});return v})}catch(e){same=e===error}same)JS");
    boolean(R"JS(let error={};let same=false;try{JSON.parse('{"a":1}',function(k,v){if(k==='a')throw error;return v})}catch(e){same=e===error}same)JS");
}
TEST_F(JsMvpLmd, FullParityJsonReplacerSpacingAndOwnership) {
    boolean("let d=Object.getOwnPropertyDescriptor(JSON,Symbol.toStringTag);d.value==='JSON'&&!d.writable&&!d.enumerable&&d.configurable&&Object.prototype.toString.call(JSON)==='[object JSON]'");
    boolean(R"JS(let seen=[];let text=JSON.stringify({a:1,b:[2,undefined]},function(k,v){seen.push(k);return k==='a'?5e-324:v},2);text==='{\n  "a": 5e-324,\n  "b": [\n    2,\n    null\n  ]\n}'&&seen.join(',')===',a,b,0,1')JS");
    boolean(R"JS(let order='';let list=new Proxy(['b','a','b',new String('a')],{get(t,k){order+=k+';';return t[k]}});let gap={};let space=new Number(1);space.valueOf=()=>{order+='space';return 1};let text=JSON.stringify({a:1,b:2,c:3},list,space);text==='{\n "b": 2,\n "a": 1\n}'&&order==='length;0;1;2;3;space')JS");
    boolean(R"JS(let text=JSON.stringify({a:1},null,'\0');text==='{\n\0"a": 1\n}'&&JSON.stringify([1],null,Infinity)==='[\n          1\n]'&&JSON.stringify([1],null,NaN)==='[1]')JS");
    boolean(R"JS(let gap='\ud83d\ude00'.repeat(6);let text=JSON.stringify({a:1},null,gap);text==='{\n'+'\ud83d\ude00'.repeat(5)+'"a": 1\n}')JS");
    boolean(R"JS(let held={a:5e-324,b:-0};let text=JSON.stringify(held,function(k,v){if(k==='a'){this.a=7;JSON.stringify({x:3.25});}return v},0);text==='{"a":5e-324,"b":0}'&&held.a===7)JS");
    boolean(R"JS(let seen='';let o={a:{toJSON(k){seen+='j'+k;return 3}}};JSON.stringify(o,function(k,v){seen+='r'+k;return v},0)==='{"a":3}'&&seen==='rjara')JS");
    boolean(R"JS(BigInt.prototype.toJSON=function(k){return k+':'+this.toString()};JSON.stringify([2n],null,0)==='["0:2"]'&&JSON.stringify(3n)==='":3"')JS");
    error("let a={};a.self=a;JSON.stringify(a,null,1)", "TypeError");
    boolean("let p=Proxy.revocable([],{});p.revoke();let caught=false;try{JSON.stringify({},p.proxy)}catch(e){caught=e instanceof TypeError}caught");
}
