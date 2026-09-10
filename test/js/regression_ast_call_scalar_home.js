// D5.3.4: a call's raw scalar payload must outlive its temporary Item roots.
function identity(value) { gc(); return value; }
const tiny = Number.MIN_VALUE;
const saved = identity(tiny);
identity(-tiny);
console.log(saved === tiny, identity(-tiny) === -tiny);
console.log(Number(tiny) === tiny, Object(tiny).valueOf() === tiny);
const view = new DataView(new ArrayBuffer(8));
view.setFloat64(0, tiny);
const loaded = view.getFloat64(0);
gc();
console.log(loaded === tiny, identity(...[tiny]) === tiny);
