// Regression: a typed-array variable rebound via a destructuring swap inside a loop
// ([prev,curr]=[curr,prev]) must not keep using a stale hoisted data pointer (P4h
// loop-invariant typed-array pointer hoist). kostya "levenshtein" used to return the
// wrong edit distance on the cost-0 (matching-character) DP path because of this.
function lev(a, b) {
  var la = a.length, lb = b.length;
  var prev = new Int32Array(lb + 1);
  var curr = new Int32Array(lb + 1);
  for (var k = 0; k <= lb; k++) prev[k] = k;
  for (var i = 1; i <= la; i++) {
    curr[0] = i;
    for (var j = 1; j <= lb; j++) {
      var cost = (a.charCodeAt(i - 1) === b.charCodeAt(j - 1)) ? 0 : 1;
      var del = prev[j] + 1;
      var ins = curr[j - 1] + 1;
      var sub = prev[j - 1] + cost;
      var m = del < ins ? del : ins;
      curr[j] = m < sub ? m : sub;
    }
    [prev, curr] = [curr, prev];   // destructuring swap of the two Int32Arrays
  }
  return prev[lb];
}
console.log("kitten->sitting=" + lev("kitten", "sitting"));
console.log("saturday->sunday=" + lev("saturday", "sunday"));
console.log("flaw->lawn=" + lev("flaw", "lawn"));
