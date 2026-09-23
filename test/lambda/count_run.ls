// S8.3.3v3: count(x) is the size of the run x is (S2.5.5v2, S11.1.6v2) --
// 0 for null, len(x) for a list, 1 for any other value (an array or text is
// one item) -- and an error propagates, as for len (S8.3.1v3). It is the
// match count of a query (S8.2.4), which neither len idiom can express.
// Golden written from the ruling, not from the runtime.

"-- the size of a run --";
[count(null), count(5), count("ab"), count((1, 2)), count((1, 2, 3))];
"-- an array or text is one item, whatever it holds --";
[count([]), count([1, 2, 3]), count(""), count(<e "x" 1>), count({a: 1})];
"-- a query: none, one, several --";
let page = <div <img src: "a"> <img src: "b"> <p [1, 2, 3]>>;
[count(page?<table>), count(page?<p>), count(page?<img>)];
"-- a lone array match is ONE match (len([*q]) would count its 3 items) --";
[count(page?array), len([*page?array])];
"-- the method form --";
[page?<img>.count(), null.count()];
"-- the literal law: len([x1..xn]) = sum of count(xi or 1) --";
let r = for (x in [2, 3]) x;
[len([1, r, 4]), count(1 or 1) + count(r or 1) + count(4 or 1)];
[len([null, 1]), count(null or 1) + count(1 or 1)]
