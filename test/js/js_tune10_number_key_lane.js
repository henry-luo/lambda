// JS Tune10 T10-1: the numeric computed-key lane must be admitted by the key's
// physical carrier, never by an inferred static type. Every line below was
// verified byte-identical against Node; the `o[m]` case is the regression that
// an inference-based admission rule got wrong (it read o["NaN"], not o["x"]).
const a = [1, 2, 3];
const o = { "0": 'z', x: 9, "1.5": 'h', "-1": 'm', "NaN": 'n', "Infinity": 'i' };
let k = 0; console.log(a[k], o[k]);
k = 1; console.log(a[k]);
let s = 1.5; console.log(o[s]);
let neg = -1; console.log(o[neg]);
let nan = NaN; console.log(o[nan]);
let inf = Infinity; console.log(o[inf]);
let big = 2 ** 53; console.log(a[big], o[big]);
let u32 = 4294967295; console.log(a[u32]);
// key that starts numeric and is rebound to a string: must NOT be coerced
let m = 0; for (let i = 0; i < 2; i++) m = m + 1; m = "x"; console.log(o[m]);
// a call whose return type varies between number and string
function pick(i) { return i > 0 ? 1 : "x"; }
console.log(a[pick(1)], o[pick(-1)]);
// compound assignment keeps the lane across read and write
const c = [1, 2, 3]; let j = 1; c[j] += 10; c[j]++; c[j] *= 2; console.log(c.join(','));
console.log(1 in a, delete a[1], 1 in a, a.length, typeof a[1]);
// accessors installed on numeric indices still run
const g = {}; Object.defineProperty(g, '2', { get() { return 'got2'; }, configurable: true });
let two = 2; console.log(g[two]);
let seen = null; Object.defineProperty(g, '3', { set(v) { seen = v; }, configurable: true });
let three = 3; g[three] = 'set3'; console.log(seen);
// string, typed-array and arguments receivers
const st = "hello"; let idx = 1; console.log(st[idx], st[99]);
const ta = new Float64Array(2); ta[idx] = 1.5; console.log(ta[idx], ta[7]);
function f() { let i = 0; return arguments[i] + arguments[1]; } console.log(f(3, 4));
// BigInt and Symbol keys must stay off the numeric lane
const bo = {}; const bk = 1n; bo[bk] = 'big'; console.log(bo[bk], bo["1"]);
// a valueOf-bearing key object stays observable exactly once
let calls = 0; const vk = { valueOf() { calls++; return 1; } };
console.log(a[vk], calls);
const sp = []; let n5 = 5; sp[n5] = 1; console.log(sp.length, JSON.stringify(sp));
// T10-0: integral doubles below 2^53 format through the integer path
const arr2 = [1, 2, 3];
arr2[1.5] = 'x'; arr2[-0] = 'z'; arr2[3] = 4;
console.log(JSON.stringify(arr2), arr2[1.5], arr2[0], arr2.length);
const ko = {}; ko[1] = 'a'; ko[1.5] = 'b'; ko[-1] = 'c'; ko[NaN] = 'd'; ko[Infinity] = 'e';
console.log(JSON.stringify(ko), Object.keys(ko).join('|'));
console.log(String(1e21), String(1e-7), String(0.1), String(-0), String(123456789012345678901234567890));
console.log(String(9007199254740991), String(9007199254740992), String(1e15), String(-42));
try { const z = null; z[0]; } catch (e) { console.log("TypeError:", e instanceof TypeError); }
try { const z = undefined; z[1] = 2; } catch (e) { console.log("TypeError set:", e instanceof TypeError); }
