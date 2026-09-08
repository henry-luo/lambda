// JS Tune10 T10-3: the ordinary "create a new own data property" kernel must be
// observationally identical to the descriptor path it replaces. Verified
// byte-identical against Node.
function P(x, y) { this.x = x; this.y = y; }
const p = new P(1, 2);
console.log(p.x, p.y, JSON.stringify(p), Object.keys(p).join('|'));
const d = Object.getOwnPropertyDescriptor(p, 'x');
console.log(d.writable, d.enumerable, d.configurable, d.value);
// an inherited setter still governs the Set
const proto = {};
Object.defineProperty(proto, 's', { set(v) { this._s = v * 2; }, get() { return this._s; } });
const o = Object.create(proto); o.s = 21;
console.log(o.s, o._s, Object.keys(o).join('|'), o.hasOwnProperty('s'));
// an inherited non-writable data property blocks the add
const proto2 = {}; Object.defineProperty(proto2, 'n', { value: 1, writable: false });
const o2 = Object.create(proto2); o2.n = 5; console.log(o2.n, o2.hasOwnProperty('n'));
(function () { 'use strict';
  try { const o3 = Object.create(proto2); o3.n = 5; console.log("no throw"); }
  catch (e) { console.log("strict throws:", e instanceof TypeError); } })();
// extensibility markers are own properties, so the kernel must read them
const e1 = {}; Object.preventExtensions(e1); e1.a = 1;
console.log("prevExt:", e1.a, Object.isExtensible(e1));
const e2 = { b: 1 }; Object.seal(e2); e2.c = 2; e2.b = 9;
console.log("seal:", e2.b, e2.c, JSON.stringify(e2));
const e3 = { d: 1 }; Object.freeze(e3); e3.d = 9; e3.e = 2;
console.log("freeze:", e3.d, e3.e);
const e4 = {}; Object.freeze(e4); e4.f = 1;
console.log("freeze-empty:", e4.f, Object.isExtensible(e4));
(function () { 'use strict'; const s = {}; Object.preventExtensions(s);
  try { s.z = 1; console.log("no throw2"); }
  catch (e) { console.log("strict prevExt throws:", e instanceof TypeError); } })();
// __proto__ is an accessor, never an own data slot
const q = {}; q.__proto__ = proto;
console.log(Object.getPrototypeOf(q) === proto, q.hasOwnProperty('__proto__'));
// an accessor on Object.prototype shadows the add
Object.defineProperty(Object.prototype, 'hooked', { set(v) { this._h = v; }, configurable: true });
const r = {}; r.hooked = 7; console.log(r._h, r.hasOwnProperty('hooked'));
delete Object.prototype.hooked;
// symbol, Proxy, private-field and non-Map receivers stay on the generic path
const sym = Symbol('s'); const t = {}; t[sym] = 3;
console.log(t[sym], Object.getOwnPropertySymbols(t).length);
const pr = new Proxy({}, { set(tg, k, v) { tg[k] = v * 10; return true; } });
pr.w = 4; console.log(pr.w);
class C { #priv = 1; constructor() { this.pub = 2; } get p() { return this.#priv; } }
const c = new C(); console.log(c.p, c.pub, Object.keys(c).join('|'));
const arr = []; arr.foo = 1; console.log(arr.foo, arr.length);
function fx() {} fx.bar = 2; console.log(fx.bar);
// a prototype accessor defined after the instance exists still governs
function Q() {} const q1 = new Q();
Object.defineProperty(Q.prototype, 'later', { set(v) { this._l = v; } });
q1.later = 5; console.log(q1._l, q1.hasOwnProperty('later'));
// adds through a deep ordinary prototype chain, and after an attribute change
const o4 = {}; o4.a = 1; Object.defineProperty(o4, 'b', { value: 2, enumerable: false }); o4.c = 3;
console.log(JSON.stringify(o4), o4.b, Object.keys(o4).join('|'));
let deep = Object.create(null); for (let i = 0; i < 6; i++) deep = Object.create(deep);
deep.deep = 1; console.log(deep.deep, Object.keys(deep).join(','));
const root = {}; for (let i = 0; i < 5; i++) { root['k' + i] = { n: i }; }
console.log(Object.keys(root).join(','), root.k3.n, JSON.stringify(root.k0));
