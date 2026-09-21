// S2.4.2v5: a numeric member step is an integer key, so `d.1.2` is keys 1
// then 2 (the lexer used to read `1.2` as one float and reject the chain), and
// `.1` after `~` or `~~` is a step rather than a juxtaposed float 0.1.
let d = [[1, 2, 3], [4, 5, 6]];
[d.1.2, d.0.1, d |> ~.1, d |> ~~.1, d.1.0 + d.0.2]
