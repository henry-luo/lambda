// Structural MIR probe: objects, arrays, properties, and call forms.
function tuneMakeRecord(a, b) { return {first: a, second: b, sum: a + b}; }
function tuneReadWrite(object, value) {
    let previous = object.value;
    object.value = value;
    return previous + object.value;
}
function tuneArray(a, b) {
    let values = [a, b];
    values.push(a + b);
    return values[1];
}
function tuneCallback(callback, value) {
    let prepared = value + 1;
    return callback(value, prepared);
}
function tuneMethod(object, value) { return object.increment(value); }
function tuneDestructure(object) {
    const {first, second = 2} = object;
    return first + second;
}
function tuneArrayDestructure(values) {
    const [first, second] = values;
    return first + second;
}
function tuneSpreadCall(callback, values) { return callback(...values); }
function tuneOptional(object) { return object?.nested?.value ?? 7; }
const tuneObject = {
    value: 2,
    nested: {value: 9},
    increment(value) { return value + 1; }
};
const tunePair = [3, 4];
const tuneAdder = function(a, b) { return a + b; };
tuneMakeRecord(1, 2);
tuneReadWrite(tuneObject, 5);
tuneArray(1, 2);
tuneCallback(tuneAdder, 3);
tuneMethod(tuneObject, 4);
tuneDestructure({first: 3});
tuneArrayDestructure(tunePair);
tuneSpreadCall(tuneAdder, tunePair);
tuneOptional(tuneObject);
