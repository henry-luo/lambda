// Template literal tests

// Simple variable interpolation
var name = "World";
var greeting = `Hello, ${name}!`;

// Expression interpolation
var a = 10;
var b = 20;
var expr = `Sum: ${a + b}`;

// Multiple interpolations
var first = "Jane";
var last = "Doe";
var full = `Name: ${first} ${last}`;

// Interpolation with function call
function double(x) { return x * 2; }
var withFunc = `Double of 7 is ${double(7)}`;

// No interpolation (plain template)
var plain = `just a plain string`;

// Nested expressions
var nested = `Result: ${a > b ? "bigger" : "smaller"}`;

// Template with numbers
var age = 25;
var info = `Age: ${age}, next year: ${age + 1}`;

// Multi-arg console.log
console.log("hello", "world");
console.log("sum", a + b);

const result = {
  greeting: greeting,
  expr: expr,
  full: full,
  withFunc: withFunc,
  plain: plain,
  nested: nested,
  info: info
};
result;
