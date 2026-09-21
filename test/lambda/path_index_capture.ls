// S2.4.2v5: a computed key step of a path literal reads its key like any
// expression: a closure captures it, a loop variable feeds it, and `~` in it
// makes a mapping pipe. The literal's static steps after it stay key steps.
let k = 3;
let mk = (n) => (() => \.a[n].b[k]);
let f = mk(7);
fn g(x) { \[x].name }
[f(), g(2), for (i in [1, 2]) \.r[i].~~, [5, 6] |> \.p[~]]
