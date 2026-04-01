// test type guard emission (typeof)

function describe(x: number | string): string {
  if (typeof x === "number") {
    return "num";
  }
  return "str";
}

console.log(describe(42));
console.log(describe("hi"));
