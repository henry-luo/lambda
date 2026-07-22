// MT5 fixture: a class field arrow nested inside an IIFE wrapper must capture
// `this` through its environment. Losing it made a dispatched handler see
// `this === undefined` and crash (Stage 4C LambdaJS bugs #1/#2, where the first
// fix was too narrow to cover arrows with mixed captures).
// Checked by class_field_arrow_this.mir-check.

(function () {
  class Counter {
    constructor(n) { this.n = n; }
    bump = () => { return this.n + 1; };
  }
  const c = new Counter(41);
  console.log(c.bump());
})();
