// GC rooting of the with-scope stack (js_with_stack) and the memoized
// with-binding cache: the with operand — or the js_to_object wrapper created
// for a primitive operand — may be referenced ONLY by the with stack while the
// body runs, so a collection inside the body must not free the scope object.
// Run under LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1 (test-gc-rooting-core)
// to make a missed root deterministic.

function onlyRefInWithStack() {
    var junk = [];
    with ({secret: 12345}) {
        for (var i = 0; i < 40; i++) { junk.push({x: i}); }
        gc();
        return secret;
    }
}

// NOTE: `with ("abc")` (primitive operand → js_to_object wrapper) is NOT
// covered here: String wrapper objects have a separate pre-existing GC bug —
// the wrapped primitive is untraced, so `new String("abc")` loses its value
// under forced GC even when the wrapper itself is rooted (task_ce716fbc).

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

function methodThisViaWith() {
    // unqualified call inside with: the memoized binding cache holds the scope
    // object as the call's `this` base while argument evaluation allocates
    with ({ val: 7, m: function(ignored) { return this.val; } }) {
        return m({junk: 1});
    }
}

console.log(onlyRefInWithStack() === 12345);
console.log(nestedWithUnwind() === "3,1");
console.log(withExceptionUnwind() === 4);
console.log(methodThisViaWith() === 7);
