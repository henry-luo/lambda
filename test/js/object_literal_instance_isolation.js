// Object literals evaluated repeatedly at one call site must stay fully
// independent: mutation, delete, defineProperty, freeze and null-writes on one
// instance must never be observable on another. Pins the invariant that any
// shape-sharing scheme (see vibe/Lambda_Impl_Tune6.md J2) has to preserve.
// 1. type-changing write on one instance only
var made = [];
for (var i = 0; i < 5; i++) made.push({ a: 1, b: 2 });
made[2].a = "changed";
console.log(made.map(function (o) { return o.a; }).join(','));
console.log(made.map(function (o) { return o.b; }).join(','));

// 2. delete on one instance only
var dels = [];
for (var i = 0; i < 4; i++) dels.push({ x: 10, y: 20 });
delete dels[1].x;
console.log(dels.map(function (o) { return o.x === undefined ? 'gone' : o.x; }).join(','));
console.log(dels.map(function (o) { return 'x' in o; }).join(','));

// 3. adding a property to one instance only (shape transition off a shared base)
var adds = [];
for (var i = 0; i < 3; i++) adds.push({ p: i });
adds[0].extra = 'only-first';
console.log(adds.map(function (o) { return o.extra === undefined ? 'none' : o.extra; }).join(','));
console.log(Object.keys(adds[0]).join('|') + ' / ' + Object.keys(adds[1]).join('|'));

// 4. defineProperty on one instance only
var defs = [];
for (var i = 0; i < 3; i++) defs.push({ v: i });
Object.defineProperty(defs[1], 'v', { writable: false });
defs[0].v = 100;
defs[1].v = 200;
defs[2].v = 300;
console.log(defs.map(function (o) { return o.v; }).join(','));

// 5. seal/freeze on one instance only
var seals = [];
for (var i = 0; i < 3; i++) seals.push({ s: 1 });
Object.freeze(seals[1]);
seals[0].s = 9;
seals[1].s = 9;
seals[2].s = 9;
console.log(seals.map(function (o) { return o.s; }).join(','));
console.log(Object.isFrozen(seals[0]) + ',' + Object.isFrozen(seals[1]));

// 6. per-slot type divergence across evaluations of the same literal site
var mixed = [];
for (var i = 0; i < 6; i++) mixed.push({ k: i % 2 === 0 ? i : 'str' + i });
console.log(mixed.map(function (o) { return typeof o.k + ':' + o.k; }).join(' '));

// 7. values remain independent (no data-buffer sharing)
var indep = [];
for (var i = 0; i < 4; i++) indep.push({ n: i * 10, o: { deep: i } });
console.log(indep.map(function (e) { return e.n + '/' + e.o.deep; }).join(','));

// 8. property order preserved across shared evaluations
var ord = { z: 1, a: 2, m: 3 };
var ord2 = { z: 4, a: 5, m: 6 };
console.log(Object.keys(ord).join('') + ' ' + Object.keys(ord2).join(''));

// 9. null written to a slot a sibling tagged with a real type must stay null
//    (regression: the shared blueprint kept the sibling's tag and read the null
//    word back as a zero-valued value of that type, e.g. `false`)
var nulls = [];
for (var i = 0; i < 4; i++) nulls.push({ v: i === 1 ? null : false, w: i === 2 ? null : 7 });
console.log(nulls.map(function (o) { return o.v === null ? 'null' : String(o.v); }).join(','));
console.log(nulls.map(function (o) { return o.w === null ? 'null' : String(o.w); }).join(','));
var undefs = [];
for (var i = 0; i < 3; i++) undefs.push({ u: i === 1 ? undefined : 5 });
console.log(undefs.map(function (o) { return o.u === undefined ? 'undef' : String(o.u); }).join(','));

// 10. shared-shape objects still compare as distinct identities
var id1 = { q: 1 }, id2 = { q: 1 };
console.log((id1 === id2) + ',' + (id1.q === id2.q));
