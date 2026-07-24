// GC rooting of the with-scope machinery. Three distinct collectors are
// exercised here, each a confirmed use-after-free before its fix:
//   1. js_with_stack + the memoized with-binding cache (js_globals.cpp): a with
//      operand referenced ONLY by the with stack must survive a body-collection.
//   2. the primitive-wrapper builders (js_new_string/number/boolean_wrapper and
//      the Symbol/BigInt cases of js_to_object): the half-built wrapper is
//      referenced only by an unrooted local across many allocating steps.
//   3. the call-boundary saved with-scope (js_call_function_impl): a with scope
//      must not leak into a callee, so the caller's scope is copied to a native
//      array while the callee runs — that copy must be a GC root or a GC inside
//      the callee frees the caller's still-live scope object.
// Run under LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1 (test-gc-rooting-core)
// to make any missed root deterministic.

function churn() {
    var j = [];
    for (var i = 0; i < 30; i++) { j.push({x: i}); }
    return j.length;
}

// (1) operand held only by the with stack, own-property read after GC
function onlyRefInWithStack() {
    var junk = [];
    with ({secret: 12345}) {
        for (var i = 0; i < 40; i++) { junk.push({x: i}); }
        gc();
        return secret;
    }
}

// (1) call-result operand (freshly GC-allocated, no other reference) + a nested
// function call inside the body — exercises the call-boundary save/restore.
function callResultOperand() {
    function mk() { var o = {}; o.secret = 7; return o; }
    with (mk()) {
        churn();
        gc();
        return secret;
    }
}

// (2) primitive-string operand → js_new_string_wrapper; inherited method lookup
// walks the wrapper's prototype after GC, and a call inside the body crosses the
// call boundary while the temp-only wrapper is the caller's active with scope.
function primitiveStringWrapper() {
    with ("abcde") {
        churn();
        gc();
        return length + charAt(1);   // own length + inherited String.prototype.charAt
    }
}

// (2) primitive-number operand → js_new_number_wrapper; inherited toFixed.
function primitiveNumberWrapper() {
    with (Object(3.14)) {
        churn();
        gc();
        return toFixed(1);
    }
}

// (2) direct wrapper construction under GC, wrapper held in a rooted var.
function directWrapperConstruction() {
    var s = new String("hello");
    var n = new Number(42);
    var b = new Boolean(true);
    churn();
    gc();
    return s.length + "," + s.valueOf() + "," + n.valueOf() + "," + b.valueOf();
}

function nestedWithUnwind() {
    var result = [];
    with ({a: 1}) {
        with ({b: 2}) {
            gc();
            result.push(a + b);
        }
        gc();
        result.push(a);
    }
    return result.join(",");
}

function withExceptionUnwind() {
    try {
        with ({c: 3}) { throw new Error("unwind"); }
    } catch (e) {}
    // after exception unwinding, a fresh with scope must still survive GC
    var junk = [];
    with ({d: 4}) {
        for (var j = 0; j < 20; j++) { junk.push({y: j}); }
        gc();
        return d;
    }
}

// (2) other builders with the same "unrooted obj across allocating
// construction" shape: TextEncoder, WeakRef, FinalizationRegistry. Before the
// fix, FinalizationRegistry lost its prototype (register() became undefined)
// under forced GC.
function collectionBuilders() {
    var te = new TextEncoder();
    var wr = new WeakRef({ v: 9 });
    var fr = new FinalizationRegistry(function () {});
    churn();
    gc();
    return te.encoding + "," + wr.deref().v + "," + (typeof fr.register);
}

function methodThisViaWith() {
    // unqualified call inside with: the memoized binding cache holds the scope
    // object as the call's `this` base while argument evaluation allocates
    with ({ val: 7, m: function(ignored) { return this.val; } }) {
        return m({junk: 1});
    }
}

console.log(onlyRefInWithStack() === 12345);
console.log(callResultOperand() === 7);
console.log(primitiveStringWrapper() === "5b");
console.log(primitiveNumberWrapper() === "3.1");
console.log(directWrapperConstruction() === "5,hello,42,true");
console.log(collectionBuilders() === "utf-8,9,function");
console.log(nestedWithUnwind() === "3,1");
console.log(withExceptionUnwind() === 4);
console.log(methodThisViaWith() === 7);
