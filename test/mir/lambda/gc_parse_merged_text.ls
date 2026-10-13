// A parsed document's text belongs to its Input, not to the GC heap. The
// Markdown inline parser flushes a run in pieces and `list_push` merges the
// adjacent strings (S2.6.4); when `parse()` ran from a script, that merge
// took the script's eval context for the owner and allocated the merged run
// on the heap, inside an arena-owned element nothing traces. The run was
// swept at the next collection and read back as garbage. Under the GC-stress
// harness (forced collections + poisoned frees) the runs below are poisoned
// unless they live in the Input's pool.

fn runs(doc) {
  let body = [for (c in content(doc) where type(c) == element and name(c) == 'body') c][0]
  let para0 = [for (c in content(body) where type(c) == element) c][0]
  let para = if (name(content(para0)[0]) == 'span') content(para0)[0] else para0;
  [for (k in content(para)) if (type(k) == string) k else name(k)]
}

// allocation between the parse and the first read of its text
fn churn(n) => len([for (i in 1 to n) {a: i, b: "x"}])

let emphasis = parse("Inline *x* and **y** then `z`.\n", {type: 'markdown', sourcepos: true}) or null
let c1 = churn(20000);
runs(emphasis);

let math = parse("Inline $\\frac{1}{2}$ and $\\sqrt{x}$.\n", {type: 'markdown', sourcepos: true}) or null
let c2 = churn(20000);
runs(math);

// the runs read the same after a second round of allocation
let c3 = churn(20000);
[runs(emphasis), runs(math)]
