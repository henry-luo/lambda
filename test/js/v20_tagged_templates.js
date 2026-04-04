// Tagged template literals
function tag(strings) {
  var result = strings[0];
  for (var i = 1; i < strings.length; i++) {
    result += "[" + arguments[i] + "]";
    result += strings[i];
  }
  return result;
}

// Basic tagged template
console.log(tag`hello`);

// With interpolation
var name = "world";
console.log(tag`hello ${name}!`);

// Multiple interpolations
var a = 1, b = 2;
console.log(tag`${a} + ${b} = ${a + b}`);

// Template object has correct length
function checkLength(strings) {
  return strings.length;
}
console.log(checkLength`a${1}b${2}c`);

// Raw property exists
function hasRaw(strings) {
  return typeof strings.raw;
}
console.log(hasRaw`test`);

// Raw property has correct length
function rawLen(strings) {
  return strings.raw.length;
}
console.log(rawLen`a${1}b`);
