// Global functions tests: parseInt, parseFloat, isNaN, isFinite, charCodeAt, String.fromCharCode

// parseInt
var pi1 = parseInt("42");
var pi2 = parseInt("  100  ");
var pi3 = parseInt("-7");

// parseFloat
var pf1 = parseFloat("3.14");
var pf2 = parseFloat("  2.718  ");
var pf3 = parseFloat("-0.5");

// isNaN
var nan1 = isNaN(NaN);
var nan2 = isNaN(42);
var nan3 = isNaN(0);

// isFinite
var fin1 = isFinite(42);
var fin2 = isFinite(3.14);
var fin3 = isFinite(0);

// charCodeAt
var ch1 = "A".charCodeAt(0);
var ch2 = "Hello".charCodeAt(1);
var ch3 = "Z".charCodeAt(0);

// String.fromCharCode
var fc1 = String.fromCharCode(65);
var fc2 = String.fromCharCode(72);
var fc3 = String.fromCharCode(97);

const result = {
  pi1: pi1,
  pi2: pi2,
  pi3: pi3,
  pf1: pf1,
  pf2: pf2,
  pf3: pf3,
  nan1: nan1,
  nan2: nan2,
  nan3: nan3,
  fin1: fin1,
  fin2: fin2,
  fin3: fin3,
  ch1: ch1,
  ch2: ch2,
  ch3: ch3,
  fc1: fc1,
  fc2: fc2,
  fc3: fc3
};
result;
