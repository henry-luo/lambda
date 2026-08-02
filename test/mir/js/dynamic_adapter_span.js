// Dynamic-call adapter regression: wrapper operands may be padded or
// rest-lowered, but `arguments` must retain every source actual.  The long
// rest call also verifies that the adapter has no fixed storage capacity.

function invoke2(fn, a, b) {
    return fn(a, b);
}

function invoke0(fn) {
    return fn();
}

function invokeMany(fn) {
    return fn(
        {id: 0}, {id: 1}, {id: 2}, {id: 3}, {id: 4},
        {id: 5}, {id: 6}, {id: 7}, {id: 8}, {id: 9},
        {id: 10}, {id: 11}, {id: 12}, {id: 13}, {id: 14},
        {id: 15}, {id: 16}, {id: 17}, {id: 18}, {id: 19},
        {id: 20}, {id: 21}, {id: 22}, {id: 23}, {id: 24},
        {id: 25}, {id: 26}, {id: 27}, {id: 28}, {id: 29},
        {id: 30}, {id: 31}, {id: 32}, {id: 33}, {id: 34},
        {id: 35}, {id: 36}, {id: 37}, {id: 38}, {id: 39}
    );
}

function padded(a, b, c) {
    console.log("padded", arguments.length, a.id, b.id, String(c));
}

function collectRest(a, ...tail) {
    console.log("rest", arguments.length, tail.length,
        arguments[32].id, tail[31].id);
}

function allMissing(a, b) {
    console.log("all-missing", arguments.length, String(a), String(b));
}

function emptyRest(...tail) {
    console.log("empty-rest", arguments.length, tail.length);
}

invoke2(padded, {id: 1}, {id: 2});
invoke0(allMissing);
invoke0(emptyRest);
invokeMany(collectRest);

const callbackResult = [{id: 7}].map(function (...items) {
    return arguments.length + ":" + items.length + ":" + items[0].id;
});
console.log(callbackResult[0]);
