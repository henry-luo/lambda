// Test: Zip
// Layer: 3 | Category: function | Covers: zip

zip([1, 2, 3], ["a", "b", "c"])
zip([1, 2], ["a", "b", "c"])
zip([], [])
zip([1], ["a"])

// Both halves of a pair may be strings, and either may be null — a pair is
// always two elements. Held in a map so the pairs cannot splice into the
// script's own top-level content (S16.7).
{
    both_strings: zip(["a", "b"], ["c", "d"]),
    with_null: zip([1, null], [2, 3]),
    pair_count: len(zip(["a", "b"], ["c", "d"])),
    pair_len: len(zip(["a", "b"], ["c", "d"])[0])
}
