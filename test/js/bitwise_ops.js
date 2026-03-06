// Bitwise operations and comments in objects tests

// Bitwise AND
var band = 12 & 10;

// Bitwise OR
var bor = 12 | 10;

// Bitwise XOR
var bxor = 12 ^ 10;

// Bitwise NOT
var bnot = ~5;

// Left shift
var lsh = 1 << 4;

// Right shift
var rsh = (-16) >> 2;

// Unsigned right shift
var urs1 = (-1) >>> 24;
var urs2 = 255 >>> 4;

// Object with comments (was causing SIGSEGV)
var obj = {
  // line comment
  a: 1,
  /* block comment */
  b: 2,
  // trailing comment
  c: 3
};

const result = {
  band: band,
  bor: bor,
  bxor: bxor,
  bnot: bnot,
  lsh: lsh,
  rsh: rsh,
  urs1: urs1,
  urs2: urs2,
  objA: obj.a,
  objB: obj.b,
  objC: obj.c
};
result;
