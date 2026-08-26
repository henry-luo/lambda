// Regex engine benchmark — gate for the RE2 -> backtracker routing flip.
// Lambda routes a pattern to the spec backtracker (js_bt_regex.cpp) when it
// uses a backreference, a multiline unescaped anchor, a nested lookaround, or
// a lookahead containing a capture group; otherwise it stays on RE2 (directly,
// or via the wrapper's post-filter for plain lookaround). Each pair below is
// chosen so the two members do comparable matching work but land on different
// engines, which is what the flip would change.
function bench(label, fn, iters) {
  fn(Math.max(1, iters / 100) | 0);                    // warm
  const t0 = Date.now(); fn(iters); const dt = Date.now() - t0;
  console.log(label.padEnd(44) + (dt * 1e6 / iters).toFixed(0).padStart(7) + ' ns/op');
}
const hay = ('the quick brown fox jumps over the lazy dog 12345 ').repeat(20);
const N = 200000;

// --- control: plain patterns, RE2 both before and after a flip ---
bench('plain literal        (RE2)',    n => { const r=/brown fox/g;  for(let i=0;i<n;i++){r.lastIndex=0;r.test(hay);} }, N);
bench('plain class+quant    (RE2)',    n => { const r=/[a-z]+\d+/g;  for(let i=0;i<n;i++){r.lastIndex=0;r.test(hay);} }, N);

// --- the category the flip moves: trailing lookahead, capture-free ---
bench('lookahead capture-free (wrapper)', n => { const r=/fox(?= jumps)/g; for(let i=0;i<n;i++){r.lastIndex=0;r.test(hay);} }, N);
// same shape but a capture inside the lookahead forces the backtracker today
bench('lookahead w/ capture   (bt)',      n => { const r=/fox(?=( jumps))/g; for(let i=0;i<n;i++){r.lastIndex=0;r.test(hay);} }, N);

// --- fixed lookbehind: wrapper today, backtracker after the flip ---
bench('lookbehind fixed     (wrapper)', n => { const r=/(?<=quick )brown/g; for(let i=0;i<n;i++){r.lastIndex=0;r.test(hay);} }, N);
// variable-length lookbehind is already routed to the backtracker
bench('lookbehind variable  (bt)',      n => { const r=/(?<=quick\s+)brown/g; for(let i=0;i<n;i++){r.lastIndex=0;r.test(hay);} }, N);

// --- already on the backtracker: backreference lane ---
bench('backreference        (bt)',     n => { const r=/([a-z]+) \1/g; for(let i=0;i<n;i++){r.lastIndex=0;r.test(hay);} }, N);
// --- already on the backtracker: multiline anchor ---
bench('multiline anchor     (bt)',     n => { const r=/^the/gm;      for(let i=0;i<n;i++){r.lastIndex=0;r.test(hay);} }, N);
