// D1.3/D8.4.1v2: a proven Number key crosses the JS semantic boundary in a
// native carrier, while partial keys and prototype-visible holes retain the kernel.
function nativeRead(value) {
  return value[0];
}

function numericSum(value) {
  let total = 0;
  for (let i = 0; i < value.length; i++) total += value[i];
  return total;
}

function negativeZeroRead(value) {
  return value[-0];
}

function negativeRead(value) {
  return value[-1];
}

function fractionalRead(value) {
  const index = 1.5;
  return value[index];
}

function nanRead(value) {
  return value[0 / 0];
}

function infinityRead(value) {
  return value[1 / 0];
}

function largeRead(value) {
  return value[4294967295];
}

function stringRead(value) {
  return value[1];
}

function partialRead(value, index) {
  return value[index];
}

function packedTaggedRead() {
  const values = ["dense", "read"];
  let text = "";
  for (let i = 0; i < values.length; i++) text += values[i];
  return text;
}

function accessorFallbackRead() {
  const values = ["stale"];
  Object.defineProperty(values, "0", {
    get: function() { return "accessor"; }, configurable: true
  });
  return values[0];
}

const array = [4, 5];
array[-1] = "negative";
array[1.5] = "fractional";
array[NaN] = "nan";
array[Infinity] = "infinity";
array[4294967295] = "large";
const hole = new Array(1);
Object.prototype[0] = "proto-index";
const result = nativeRead(array) === 4 && numericSum(array) === 9 &&
  numericSum(new Uint8Array([4, 5])) === 9 &&
  fractionalRead(array) === "fractional" && negativeZeroRead(array) === 4 &&
  negativeRead(array) === "negative" && nanRead(array) === "nan" &&
  infinityRead(array) === "infinity" && largeRead(array) === "large" &&
  largeRead(new Uint8Array(1)) === undefined && stringRead("abc") === "b" &&
  partialRead(array, 1) === 5 && packedTaggedRead() === "denseread" &&
  accessorFallbackRead() === "accessor" && nativeRead(hole) === "proto-index";
delete Object.prototype[0];
if (!result) throw new Error("native index changed semantics");
console.log("native-index-plan-ok");
