// test namespace IIFE lowering

namespace MathUtils {
  export function add(a: number, b: number): number {
    return a + b;
  }
  export const PI: number = 3;
}

console.log(MathUtils.add(2, 3));
console.log(MathUtils.PI);
