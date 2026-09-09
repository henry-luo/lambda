// JS Tune10 T10-4: the arguments object is built without descriptor objects.
// Its own properties must keep their exact descriptors and attributes, and the
// internal `__strict_arguments__` marker must stay non-enumerable. Verified
// byte-identical to Node.
//
// Known pre-existing deviation, deliberately not covered here: after
// `arguments.length = 5`, Array.prototype.slice.call(arguments) returns 3
// elements rather than 5 — the written length is not honoured. Present on v38.
function sloppy(a, b) {
  const d = Object.getOwnPropertyDescriptor(arguments, 'length');
  console.log('len', arguments.length, d.value, d.writable, d.enumerable, d.configurable);
  const c = Object.getOwnPropertyDescriptor(arguments, 'callee');
  console.log('callee', c.value === sloppy, c.writable, c.enumerable, c.configurable);
  console.log('keys', Object.keys(arguments).join(','));
  console.log('tag', Object.prototype.toString.call(arguments));
  console.log('iter', typeof arguments[Symbol.iterator], [...arguments].join('|'));
  console.log('idx', arguments[0], arguments[1], arguments[2]);
  a = 99; console.log('mapped a', arguments[0]);
  arguments[1] = 77; console.log('mapped b', b);
  return Array.prototype.slice.call(arguments).join(',');
}
console.log(sloppy(1, 2, 3));

function strict(a, b) {
  'use strict';
  const d = Object.getOwnPropertyDescriptor(arguments, 'length');
  console.log('s len', arguments.length, d.value, d.writable, d.enumerable, d.configurable);
  const c = Object.getOwnPropertyDescriptor(arguments, 'callee');
  console.log('s callee accessor', typeof c.get, typeof c.set, c.enumerable, c.configurable);
  try { arguments.callee; } catch (e) { console.log('s callee throws', e instanceof TypeError); }
  a = 42; console.log('s unmapped', arguments[0]);
  console.log('s keys', Object.keys(arguments).join(','));
  console.log('s tag', Object.prototype.toString.call(arguments));
  return [...arguments].join('|');
}
console.log(strict(1, 2));

function zero() { return arguments; }
const z = zero();
console.log('zero', z.length, Object.keys(z).length, Object.prototype.toString.call(z));
function esc() { return arguments; }
const e1 = esc('x', 'y');
console.log('escaped', e1.length, e1[0], e1[1], JSON.stringify(Array.from(e1)));
function outer() { const f = () => arguments.length; return f(); }
console.log('arrow', outer(1, 2, 3));
function del() { delete arguments[0]; return [arguments.length, arguments[0]]; }
console.log('delete', JSON.stringify(del('a', 'b')));
function redef() { Object.defineProperty(arguments, 'length', { value: 9 }); return arguments.length; }
console.log('redef len', redef(1));
function sum() { let s = 0; for (let i = 0; i < arguments.length; i++) s += arguments[i]; return s; }
console.log('sum', sum(1,2,3,4), sum.apply(null, [5,6]), sum(...[7,8,9]));

// Engine bookkeeping must never be reachable as a property. Strictness used to
// be stored as an own `__strict_arguments__` slot, visible to Object.keys and
// to Object.getOwnPropertyNames; it now lives in a container header bit.
// Phrased as "no internal-looking own key" so the assertion is Node-exact
// despite unrelated pre-existing differences in the arguments own-key set.
function ownInternal(o) { return Object.getOwnPropertyNames(o).filter(k => k.startsWith('__')).join(','); }
function strictProbe() { 'use strict'; return arguments; }
function sloppyProbe() { return arguments; }
console.log('internal keys', JSON.stringify(ownInternal(strictProbe(1))), JSON.stringify(ownInternal(sloppyProbe(1))));
console.log('enumerable', JSON.stringify(Object.keys(strictProbe(1, 2))), JSON.stringify(Object.keys(sloppyProbe(1, 2))));
