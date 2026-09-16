// T12-6: predicted own String slots use their direct carrier only while the
// source shape and the assigned representation remain valid.
function direct_string_store() {
    const point = { label: "first" };
    point.label = "second";
    return point.label;
}

function changed_carrier_fallback() {
    const point = { label: "first" };
    point.label = 7;
    return point.label;
}

function accessor_fallback() {
    const point = { label: "first" };
    let observed = "";
    Object.defineProperty(point, "label", {
        set: function(value) { observed = "set:" + value; },
        get: function() { return observed; }, configurable: true
    });
    point.label = "third";
    return point.label;
}

console.log("field:" + direct_string_store());
console.log("carrier:" + changed_carrier_fallback());
console.log("accessor:" + accessor_fallback());
