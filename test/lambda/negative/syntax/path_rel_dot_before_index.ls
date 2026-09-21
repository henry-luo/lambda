// S2.4.1v2: `\` is the relative root and a dot needs a step, so `\.[1]` is an
// error like `/.[1]`; the relative index step is `\[1]`, the path `\.1`.
let p = \.[1].x;
[p]
