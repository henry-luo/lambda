// JavaScript values with possible undefined remain boxed through property
// access. A null write must not become missing/undefined after a shaped slot.
class Box {
    constructor(value) {
        this.value = value;
    }
}

let box = new Box(7);
box.value = null;
console.log(box.value === null, box.value === undefined);
console.log(box.missing === undefined, box.missing === null);
