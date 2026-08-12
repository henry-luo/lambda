// Tune5 D4.6.1v2/D4.6.2v2 lock: direct MIR property reads and writes enter
// the final public lane shells instead of legacy access helpers.
function lane(box) {
    box.value = 2;
    return box.value;
}

let box = { value: 1 };
console.log(lane(box));
