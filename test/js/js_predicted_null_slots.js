// Published nulls and later type transitions must not reinterpret sibling slots.
function expect(ok, label) {
    if (!ok) throw new Error(label);
}
function make(value) { return { value: value, tail: 7 }; }
var values = [null, 1, 2.5, true, false, undefined, "text", { n: 9 }, [3],
              null, -0, NaN, Infinity];
var objects = [];
for (var i = 0; i < values.length; i++) objects.push(make(values[i]));
for (var i = 0; i < objects.length; i++) {
    expect(Object.is(objects[i].value, values[i]), "sibling value " + i);
    expect(objects[i].tail === 7, "sibling tail " + i);
    expect(Object.keys(objects[i]).join(",") === "value,tail", "keys " + i);
    var d = Object.getOwnPropertyDescriptor(objects[i], "value");
    expect(d.writable && d.enumerable && d.configurable, "attributes " + i);
    expect(Object.is(d.value, values[i]), "descriptor value " + i);
}
console.log("null-first mixed lanes ok");

// Both transition directions must preserve live references and null siblings.
objects[0].value = { kept: "live" };
objects[1].value = null;
objects[7].value = 42;
expect(objects[0].value.kept === "live", "null to object");
expect(objects[1].value === null, "number to null");
expect(objects[7].value === 42 && values[7].n === 9, "object to number");
expect(objects[9].value === null && objects[2].value === 2.5, "unchanged siblings");
console.log("mutations preserve siblings ok");

// A late null must seal the blueprint even after another slot learned its type.
function late(value) { return { head: 11, value: value, tail: 13 }; }
var lateNumber = late(3), lateNull = late(null), lateString = late("later");
expect(lateNumber.value === 3 && lateNull.value === null &&
       lateString.value === "later", "late null");
expect(lateNull.head === 11 && lateString.tail === 13, "late null neighbors");
console.log("number-first null transition ok");

// Descriptors, deletion and inherited setters retain their semantic paths.
var frozen = make(null), ordinary = make(null);
Object.freeze(frozen);
frozen.value = 88;
Object.defineProperty(ordinary, "value", { get: function () { return 19; } });
expect(frozen.value === null && Object.isFrozen(frozen), "frozen null");
expect(ordinary.value === 19 && make(null).value === null, "accessor isolation");
var deleted = make(null);
delete deleted.value;
deleted.value = "new";
expect(Object.keys(deleted).join(",") === "tail,value", "delete insertion order");
var setterCalls = 0;
Object.defineProperty(Object.prototype, "value", {
    set: function (_) { setterCalls++; }, configurable: true
});
var own = make(null), ownNumber = make(22);
delete Object.prototype.value;
expect(setterCalls === 0 && own.value === null && ownNumber.value === 22,
       "CreateDataProperty inherited setter");
console.log("property semantics ok");

// An abrupt field evaluation must not alter an earlier completed instance.
function fail() { throw new Error("field failure"); }
function maybeThrow(value, shouldFail) {
    return { value: value, tail: shouldFail ? fail() : 17 };
}
var completed = maybeThrow(null, false);
var caught = false;
try { maybeThrow(5, true); } catch (e) { caught = e.message === "field failure"; }
var afterFailure = maybeThrow("after", false);
expect(caught && completed.value === null && completed.tail === 17 &&
       afterFailure.value === "after", "abrupt initialization");
console.log("abrupt initialization ok");

// Re-enter one literal site while its outer instance is still being initialized.
function recursive(depth) {
    return { left: depth === 0 ? null : recursive(depth - 1),
             right: depth === 0 ? null : recursive(depth - 1) };
}
function check(tree) {
    if (tree.left === null) {
        expect(tree.right === null, "leaf right");
        return 1;
    }
    return 1 + check(tree.left) + check(tree.right);
}
var retained = recursive(7);
for (var i = 0; i < 40; i++) expect(check(recursive(6)) === 127, "tree allocation");
expect(check(retained) === 255, "retained tree");
console.log("recursive allocation ok");
