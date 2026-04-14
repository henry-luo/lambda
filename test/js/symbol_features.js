// Symbol well-known symbols tests

// --- Test 1: Symbol.iterator on custom class ---
class Range {
  constructor(start, end) { this.start = start; this.end = end; }
  [Symbol.iterator]() {
    let current = this.start;
    var end = this.end;
    return {
      next() {
        if (current <= end) return { value: current++, done: false };
        return { done: true };
      }
    };
  }
}
var vals = [];
for (var v of new Range(1, 5)) vals.push(v);
console.log("t1:" + vals.join(","));

// --- Test 2: Symbol.iterator with spread ---
var arr = [...new Range(10, 13)];
console.log("t2:" + arr.join(","));

// --- Test 3: Symbol.iterator with destructuring ---
var [a, b, c] = new Range(7, 9);
console.log("t3:" + a + "," + b + "," + c);

// --- Test 4: Symbol.toPrimitive ---
class Money {
  constructor(amount, currency) {
    this.amount = amount;
    this.currency = currency;
  }
  [Symbol.toPrimitive](hint) {
    if (hint === "number") return this.amount;
    if (hint === "string") return this.amount + " " + this.currency;
    return this.amount;
  }
}
var m = new Money(100, "USD");
console.log("t4a:" + (+m));
console.log("t4b:" + (m + 50));
console.log("t4c:" + String(m));

// --- Test 5: Symbol.hasInstance ---
class Even {
  static [Symbol.hasInstance](num) {
    return typeof(num) === "number" && num % 2 === 0;
  }
}
console.log("t5:" + (2 instanceof Even) + "," + (3 instanceof Even) + "," + (4 instanceof Even));

// --- Test 6: Symbol.toStringTag ---
class MyCollection {
  get [Symbol.toStringTag]() { return "MyCollection"; }
}
var mc = new MyCollection();
console.log("t6:" + mc[Symbol.toStringTag]);

// --- Test 7: Symbol as object key ---
var s1 = Symbol("myKey");
var obj = {};
obj[s1] = "secret";
console.log("t7:" + obj[s1]);
console.log("t7b:" + (s1.description === "myKey"));

// --- Test 8: unique symbols ---
var s2 = Symbol("test");
var s3 = Symbol("test");
console.log("t8:" + (s2 === s3));
console.log("t8b:" + typeof(s2));
