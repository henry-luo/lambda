// A hot function that names a top-level class is refused by P2 satellite
// promotion ("module binding form"). The refusal used to leave the satellite's
// MIR module open, and tearing down its context then aborted the process
// (or crashed on a freed module name), so scripts like jqjs died at load time.
class Box {
    constructor(v) { this.v = v; }
}

function make(v) {
    return new Box(v);
}

let sum = 0;
for (let i = 0; i < 50; i++) sum += make(i).v;
console.log("sum " + sum);

// the refused definition keeps running in the interpreter afterwards
let again = 0;
for (let i = 0; i < 50; i++) again += make(2 * i).v;
console.log("again " + again);
