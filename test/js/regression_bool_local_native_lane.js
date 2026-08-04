// Invariant: a boolean local must stay a boolean. P6 inference once typed a
// boolean literal as LMD_TYPE_INT, which admitted `let flag = false` into the
// native numeric local lane; a later `flag = true` then came back as the
// number 1, so `typeof` said "number" and `=== true` was false.
function loopFlag(n) {
  let flag = false;
  for (let i = 0; i < n; i++) { if (i > 5) flag = true; }
  return flag;
}
function loopFlagFalse(n) {
  let flag = true;
  for (let i = 0; i < n; i++) { if (i > 5) flag = false; }
  return flag;
}
const a = loopFlag(10);
console.log(a, typeof a, a === true, JSON.stringify(a));
const b = loopFlagFalse(10);
console.log(b, typeof b, b === false, JSON.stringify(b));
console.log(loopFlag(2), typeof loopFlag(2), loopFlag(2) === false);
