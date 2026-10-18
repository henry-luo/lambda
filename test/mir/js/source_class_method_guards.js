// D8.4.1v2: a fixed class candidate must still observe live shadows and prototypes.
class Counter {
  constructor(x) { this.x = x; }
  read(n) { return this.x + n; }
}
class Derived extends Counter {}
function invoke(value, n) { return value.read(n); }

const counter = new Counter(2);
for (let i = 0; i < 4; i++) {
  if (invoke(counter, i) !== 2 + i) throw 1;
}
if (invoke(new Derived(3), 1) !== 4) throw 2;
counter.read = function(n) { return 10 + n; };
if (invoke(counter, 1) !== 11) throw 3;
delete counter.read;
if (invoke(counter, 1) !== 3) throw 4;

function replaceDuringArgument() {
  Counter.prototype.read = function(n) { return 20 + n; };
  return 1;
}
if (counter.read(replaceDuringArgument()) !== 3 || invoke(counter, 1) !== 21) throw 5;
Counter.prototype.read = null;
let rejected = false;
try { invoke(counter, 1); } catch (e) { rejected = e instanceof TypeError; }
if (!rejected) throw 6;

let gets = 0;
Object.setPrototypeOf(counter, { get read() {
  gets++;
  return function(n) { return this.x + n + 30; };
} });
if (invoke(counter, 1) !== 33 || gets !== 1) throw 7;
console.log('source-class-method-guards-ok');
