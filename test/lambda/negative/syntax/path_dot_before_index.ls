// S2.4.1v2: a dot must be followed by a path step, so `/.[1]` is an error;
// the rooted index step is `/[1]`, i.e. `(/)[1]`.
let p = /.[1];
[p]
