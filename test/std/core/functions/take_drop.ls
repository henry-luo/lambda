// Test: Take and Drop
// Layer: 3 | Category: function | Covers: take, drop

take([1, 2, 3, 4, 5], 3)
take([1, 2, 3], 5)
take([1, 2, 3], 0)
take([], 3)
drop([1, 2, 3, 4, 5], 2)
drop([1, 2, 3], 0)
drop([1, 2, 3], 5)
drop([], 2)

// Arity must survive strings and nulls: the result is a plain collection, not
// element content, so adjacent strings must not merge and null is an element.
// Held in a map because a bare result would splice into the script's own
// top-level content (S16.7) and hide the structure under test.
{
    take_str: take(["a", "b", "c"], 2), take_str_len: len(take(["a", "b", "c"], 2)),
    drop_str: drop(["a", "b", "c"], 1), drop_str_len: len(drop(["a", "b", "c"], 1)),
    take_null: take([1, null, 2], 2),   take_null_len: len(take([1, null, 2], 2)),
    drop_null: drop([1, null, 2], 1),   drop_null_len: len(drop([1, null, 2], 1))
}
