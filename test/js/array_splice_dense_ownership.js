function check(condition, message) {
    if (!condition) throw new Error(message);
}

const first = {name: "first"};
const last = {name: "last"};
const values = [first, 1, 2, last];
const removed = values.splice(1, 2, undefined, null, first);
check(removed.join(",") === "1,2" && values.length === 5 &&
    values[0] === first && values[4] === last && values[3] === first &&
    values[1] === undefined && 1 in values && values[2] === null,
    "dense insertion lost identity or presence");
check(values.splice(1, 3).length === 3 && values[1] === last,
    "dense deletion lost the tail");
check(values.splice(-1, Infinity, first)[0] === last && values[1] === first,
    "relative index coercion changed");

const growing = [];
for (let i = 0; i < 64; i++) growing.splice(0, 0, {index: i});
const middle = growing.splice(16, 32, last);
check(growing.length === 33 && growing[16] === last &&
    growing[17].index === 15 && middle[0].index === 47 &&
    middle[31].index === 16, "growth or deletion collected a live entry");

const wide = [-Number.MIN_VALUE, -0, NaN, Infinity];
wide.splice(1, 0, -Number.MIN_VALUE);
check(wide[0] === -Number.MIN_VALUE && wide[1] === -Number.MIN_VALUE &&
    Object.is(wide[2], -0) && Number.isNaN(wide[3]) && wide[4] === Infinity,
    "scalar ownership changed during shifting");

const holes = [1, , 3];
const holeResult = holes.splice(0, 2, first);
check(holeResult.length === 2 && !(1 in holeResult) && holes[1] === 3,
    "splice materialized a hole");

let seen = 0;
const inherited = [1, 2];
const prototype = Object.create(Array.prototype);
Object.defineProperty(prototype, "3", {
    get() { return 99; }, set(value) { seen = value; }, configurable: true
});
Object.setPrototypeOf(inherited, prototype);
inherited.splice(0, 0, 7, 8);
check(seen === 2 && inherited[3] === 99 && !Object.hasOwn(inherited, "3") &&
    inherited[2] === 1, "splice bypassed an inherited setter");

const mutated = [1, 2, 3];
const start = {valueOf() { mutated.pop(); return 0; }};
const changed = mutated.splice(start, 1, 9);
check(changed[0] === 1 && mutated.length === 3 && mutated[1] === 2 &&
    !(2 in mutated), "splice ignored coercion's mutation");

const speciesSource = [1, 2, 3];
const speciesDescriptor = Object.getOwnPropertyDescriptor(Array, Symbol.species);
try {
    Object.defineProperty(Array, Symbol.species, {
        get() { speciesSource.pop(); return Array; }, configurable: true
    });
    const result = speciesSource.splice(0, 1, last);
    check(result[0] === 1 && speciesSource.length === 3 &&
        speciesSource[0] === last && !(2 in speciesSource),
        "splice ignored species' mutation");
} finally {
    Object.defineProperty(Array, Symbol.species, speciesDescriptor);
}

const custom = [1, 2, 3];
let speciesLength = -1;
custom.constructor = {[Symbol.species]: function(length) {
    speciesLength = length;
    return {marker: first};
}};
const customResult = custom.splice(1, 1, last);
check(speciesLength === 1 && customResult.marker === first &&
    customResult[0] === 2 && customResult.length === 1 && custom[1] === last,
    "splice bypassed a custom species");
console.log("splice ownership and observable fallbacks passed");
