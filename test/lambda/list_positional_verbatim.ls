// D2.6.5v3: a positional runtime structure stores a list value as ONE item
// (the verbatim append); only sequence building splices a list (S2.5.1v2).
fn args_of(...) => varg()
fn count_args(...) => len(varg())

// a rest list keeps a list argument whole (LR09-9)
args_of(for (x in [1, 2]) x);
count_args(for (x in [1, 2]) x, 9);

// an order key that is a list is one key per row
[for (x in [3, 1, 2] order by (x, 0)) x];
[for (x in [3, 1, 2] order by (0 - x, 1) desc) x];

// a group key that is a list is one key per row: two groups of two
[for (x in [1, 2, 3, 4] group by (x % 2, 7) as k into g) len(content(g))];
[for (x in [1, 2, 3, 4] group by (x % 2, 7) as k into g) len(g.k)];

// element-copying transforms keep a list element whole
len(reverse(args_of(for (x in [1, 2]) x, 5)));
len(take(args_of(for (x in [1, 2]) x, 5), 1));
len(unique(args_of(for (x in [1, 2]) x, 5)))
