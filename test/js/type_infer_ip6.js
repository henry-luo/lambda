// IP6 fixtures [Type_Infer TIG13/TIG14]: JS expressions publish the type the
// operator actually produces. Every binary used to be typed `float`.
function probe(a, b) {
  const eq = a === b;        // bool, not float
  const lt = a < b;          // bool
  const isIn = "k" in {k: 1}; // bool
  const cat = "x" + "y";     // string, not float
  const sum = 1.5 + 2.5;     // float
  const bits = 3 | 0;        // float (JS number; ToInt32 is a value rule)
  const neg = -a;            // float
  const not = !a;            // bool
  const ty = typeof a;       // string
  return [eq, lt, isIn, cat, sum, bits, neg, not, ty];
}
const r = probe(1, 2);
console.log(r.length + " " + r[0] + " " + r[3] + " " + r[8]);
