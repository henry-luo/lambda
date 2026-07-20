// Structural MIR probe: closures, classes, generators, and async bodies.
function tuneClosure(initial) {
    let current = initial;
    return function add(delta) {
        current += delta;
        return current;
    };
}
function tuneDefaults(first = 1, second = 2) { return first + second; }
function tuneRest(first, ...rest) { return first + rest.length; }
class TuneCounter {
    constructor(value) { this.value = value; }
    increment(delta) {
        this.value += delta;
        return this.value;
    }
}
function* tuneGenerator(limit) {
    let index = 0;
    while (index < limit) {
        yield index;
        index++;
    }
    return index;
}
async function tuneAsync(promise) {
    const value = await promise;
    return value + 1;
}
function tuneBuiltins(values) {
    return values.map(function(value) { return value * 2; })
        .filter(function(value) { return value > 2; });
}
const tuneAdd = tuneClosure(1);
tuneAdd(2);
tuneDefaults(undefined, 4);
tuneRest(1, 2, 3);
const tuneCounter = new TuneCounter(3);
tuneCounter.increment(2);
const tuneIterator = tuneGenerator(2);
tuneIterator.next();
tuneBuiltins([1, 2, 3]);
void tuneAsync;
