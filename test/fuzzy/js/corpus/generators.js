// Edge case: generators and iterators
function* range(start, end) {
  for (var i = start; i < end; i++) {
    yield i;
  }
}

var g = range(0, 5);
g.next();
g.next();
g.next();

// Spread generator
var arr = [...range(0, 3)];

// for..of with generator
var sum = 0;
for (var v of range(1, 4)) {
  sum += v;
}

// Generator delegation
function* inner() {
  yield 1;
  yield 2;
}
function* outer() {
  yield 0;
  yield* inner();
  yield 3;
}
var arr2 = [...outer()];

// Generator return
function* gen() {
  yield 1;
  yield 2;
  yield 3;
}
var g2 = gen();
g2.next();
g2.return(42);
g2.next();

// Generator throw
function* gen2() {
  try {
    yield 1;
    yield 2;
  } catch(e) {
    yield "caught: " + e;
  }
}
var g3 = gen2();
g3.next();
g3.throw("error");

// Infinite generator (bounded usage)
function* naturals() {
  var n = 0;
  while (true) yield n++;
}
var g4 = naturals();
g4.next(); g4.next(); g4.next();

// Custom iterable
var iterable = {
  [Symbol.iterator]() {
    var i = 0;
    return {
      next() {
        return i < 3 ? {value: i++, done: false} : {done: true};
      }
    };
  }
};
for (var v of iterable) {}
[...iterable];
