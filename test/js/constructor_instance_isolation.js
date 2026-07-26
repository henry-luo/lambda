// Constructor-created instances sharing one `new` call site's blueprint shape
// must stay fully independent per slot. Companion to
// object_literal_instance_isolation.js, which pins the same invariant for the
// object-literal side.
// Regression: a null written to a slot that a sibling instance had already
// tagged with a real type kept the sibling's tag, so the null word read back as
// a zero-valued value of that type (`false` for BOOL, `0` for INT).

// 1. null vs bool, and null vs int, across instances of one call site
function P(v) { this.x = v; }
var a = [];
for (var i = 0; i < 3; i++) a.push(new P(i === 1 ? null : false));
console.log(a.map(function (o) { return o.x === null ? 'null' : String(o.x); }).join(','));
var b = [];
for (var i = 0; i < 3; i++) b.push(new P(i === 1 ? null : 7));
console.log(b.map(function (o) { return o.x === null ? 'null' : String(o.x); }).join(','));

// 2. null vs string / float, and undefined vs a real value
var c = [];
for (var i = 0; i < 4; i++) c.push(new P(i === 2 ? null : 'sv' + i));
console.log(c.map(function (o) { return o.x === null ? 'null' : o.x; }).join(','));
var d = [];
for (var i = 0; i < 4; i++) d.push(new P(i === 0 ? null : i + 0.5));
console.log(d.map(function (o) { return o.x === null ? 'null' : String(o.x); }).join(','));
var u = [];
for (var i = 0; i < 3; i++) u.push(new P(i === 1 ? undefined : 5));
console.log(u.map(function (o) { return o.x === undefined ? 'undef' : String(o.x); }).join(','));

// 3. multi-slot constructor: nulling one slot must not disturb the others
function Q(x, y, z) { this.x = x; this.y = y; this.z = z; }
var q = [];
for (var i = 0; i < 4; i++) {
  q.push(new Q(i === 1 ? null : i, i === 2 ? null : 'y' + i, i === 3 ? null : true));
}
console.log(q.map(function (o) { return o.x === null ? 'null' : String(o.x); }).join(','));
console.log(q.map(function (o) { return o.y === null ? 'null' : o.y; }).join(','));
console.log(q.map(function (o) { return o.z === null ? 'null' : String(o.z); }).join(','));

// 4. container slot nulled on one instance: siblings keep tracing their own
//    containers (the GC-tracing reason the T->NULL retag used to be suppressed)
function R(v) { this.arr = v; }
var r = [];
for (var i = 0; i < 5; i++) r.push(new R(i === 2 ? null : [i, i + 1, i + 2]));
var churn = [];
for (var i = 0; i < 2000; i++) churn.push({ pad: i, s: 'churn' + i });
console.log(r.map(function (o) { return o.arr === null ? 'null' : o.arr.join('-'); }).join(','));
console.log(churn.length + ',' + churn[1999].s);

// 5. post-construction null write on one instance only
var s = [];
for (var i = 0; i < 4; i++) s.push(new P(i * 3));
s[1].x = null;
s[3].x = null;
console.log(s.map(function (o) { return o.x === null ? 'null' : String(o.x); }).join(','));
s[1].x = 'back';
console.log(s.map(function (o) { return o.x === null ? 'null' : String(o.x); }).join(','));

// 6. null written first, real types after — blueprint establishment still works
var t = [];
for (var i = 0; i < 4; i++) t.push(new P(i === 0 ? null : null));
t[1].x = 42;
t[2].x = 'str';
console.log(t.map(function (o) { return o.x === null ? 'null' : String(o.x); }).join(','));

// 7. per-instance property added on top of a shared constructor shape
var e = [];
for (var i = 0; i < 3; i++) e.push(new P(i));
e[0].extra = 'only-first';
e[1].x = null;
console.log(e.map(function (o) { return o.extra === undefined ? 'none' : o.extra; }).join(','));
console.log(e.map(function (o) { return o.x === null ? 'null' : String(o.x); }).join(','));

// 8. inherited constructor chain: null in a base-assigned slot
function Base(v) { this.b = v; }
function Derived(v, w) { Base.call(this, v); this.d = w; }
Derived.prototype = Object.create(Base.prototype);
Derived.prototype.constructor = Derived;
var dd = [];
for (var i = 0; i < 3; i++) dd.push(new Derived(i === 1 ? null : i, 'w' + i));
console.log(dd.map(function (o) { return o.b === null ? 'null' : String(o.b); }).join(','));
console.log(dd.map(function (o) { return o.d; }).join(','));

// 9. instances stay distinct identities and keep their own key order
var i1 = new Q(1, 2, 3), i2 = new Q(1, 2, 3);
console.log((i1 === i2) + ',' + (i1.x === i2.x));
console.log(Object.keys(i1).join('|') + ' / ' + Object.keys(i2).join('|'));
