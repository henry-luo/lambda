// S2.5.4: a declaration produces no item; every other item of the list stays.
// S2.5.5v2: the list then collapses by its item count.

// one item: the list is that item
(let x = 5, x + 1);
// two items after the binding: a list, which spreads as content
(let x = 1, x, 2);
// the binding may sit between items
(1, let x = 2, x);
// a list expression in an item position splices its items
[(let x = 1, x, 2), 3];
[0, (let a = 1, let b = 2, a, b), 3];
// a bound list is a value: it keeps both items
len((let x = 1, x, 2));
type((let x = 1, x, 2));
// only declarations: zero items, which splices nothing in an item position
[(let x = 5), 9];
// the block form follows the same rule
[{ let y = 1; y; y + 1 }]
