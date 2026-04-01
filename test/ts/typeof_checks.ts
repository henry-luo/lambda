// typeof in type guard expressions

function describe(x: number | string): string {
  if (typeof x === "number") {
    return "got number";
  }
  return "got string";
}

console.log(describe(42));
console.log(describe("hi"));

// typeof with boolean
function check(val: any): string {
  if (typeof val === "boolean") {
    return "bool";
  } else if (typeof val === "string") {
    return "str";
  }
  return "other";
}

console.log(check(true));
console.log(check("hello"));
console.log(check(123));

// type() with various values
console.log(type(undefined));
console.log(type({x: 1, y: 2}));
