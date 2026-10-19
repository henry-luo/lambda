class Bag {
    constructor() { this.items = [1, 2, 3]; }
    each(fn) { for (let i = 0; i < this.items.length; i++) fn(this.items[i]); }
    some(fn) { for (let i = 0; i < this.items.length; i++) if (fn(this.items[i])) return true; return false; }
    escape(fn) { return fn; }
    inspect(fn) { return fn.length; }
}
function visit(bag) {
    let total = 0;
    bag.each((value) => { total += value; });
    return total;
}
function contains(bag, sought) { return bag.some((value) => value === sought); }
function escape(bag) { return bag.escape((value) => value + 1); }
function extra(bag, effect) { let sum = 0; bag.each((value) => { sum += value; }, effect()); return sum; }
function recurse(bag, depth) {
    let total = 0;
    bag.each((value) => { total += depth ? recurse(bag, depth - 1) : value; });
    return total;
}
class Owner {
    constructor() { this.total = 0; }
    visit(bag) { bag.each((value) => { this.total += value; }); return this.total; }
}
const bag = new Bag();
if (visit(bag) !== 6 || !contains(bag, 2) || contains(bag, 9) ||
    new Owner().visit(bag) !== 6 || recurse(bag, 2) !== 54 ||
    escape(bag) === escape(bag) || escape(bag)(2) !== 3)
    throw new Error('callback specialization');
let effects = 0;
if (extra(bag, () => { effects++; }) !== 6 || effects !== 1)
    throw new Error('extra argument effects');
const original = Bag.prototype.each;
Bag.prototype.each = function(fn) { fn(7); };
if (visit(bag) !== 7) throw new Error('prototype replacement');
Bag.prototype.each = original;
bag.each = function(fn) { fn(8); };
if (visit(bag) !== 8) throw new Error('own shadow');
delete bag.each;
Object.defineProperty(bag, 'each', { configurable: true, get() {
    Bag.prototype.each = function(fn) { fn(9); };
    return original;
} });
if (visit(bag) !== 6) throw new Error('captured getter result');
delete bag.each;
Bag.prototype.each = original;
let caught = false;
function throwOnTwo(value) { if (value === 2) throw value; }
function inlineFinally(bag) {
    try { bag.each((value) => throwOnTwo(value)); }
    finally { effects++; }
}
try { inlineFinally(bag); }
catch (error) { caught = error === 2; }
if (!caught || effects !== 2 || bag.inspect((a, b = 1, ...rest) => a) !== 1)
    throw new Error('callback completion or metadata');
console.log('literal callback: PASS');
