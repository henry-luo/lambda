// Store IC must not write a constructor slot that is still reserved on the
// receiving instance. A base constructor can lose its own shaped-slot path
// (a sibling branch with an implicit constructor disables the whole chain)
// while a derived class still pre-shapes instances with reserved slots. The
// first construction misses the cold IC and takes the slow path, which clears
// the reservation; later constructions hit the mono IC and must not write the
// raw slot behind the still-set reservation, or the value is stored but stays
// invisible to reads and enumeration.

class Base {
  constructor(tag) { this.strength = mkStrength(tag); }
}

class Binary extends Base {
  constructor(a, b, tag) { super(tag); this.v1 = a; this.v2 = b; this.direction = null; }
}

class Unary extends Base {
  constructor(v, tag) { super(tag); this.output = v; this.satisfied = false; }
}

// implicit constructor: disables the composed ctor shape for Unary and Base
class Edit extends Unary {}

class Equality extends Binary {
  constructor(a, b, tag) { super(a, b, tag); }
}

function mkStrength(tag) { return { tag: tag }; }

const made = [];
for (let i = 0; i < 3; i++) made.push(new Equality(i, i + 1, i));

for (let i = 0; i < made.length; i++) {
  const o = made[i];
  console.log(i, typeof o.strength, o.strength === undefined ? "-" : o.strength.tag,
    o["strength"] === undefined ? "-" : o["strength"].tag,
    Object.keys(o).join("|"), "strength" in o, o.hasOwnProperty("strength"));
}

// the sibling branch that lost its composed shape must still work
const e = new Edit(7, 42);
console.log("edit", typeof e.strength, e.strength.tag, e.output, e.satisfied);
