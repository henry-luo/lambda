// DG-5: decimal poison is a decimal value with a canonical, parseable spelling.
let zero = 0m
let one = 1m
let positive = one div zero
let negative = -one % zero
let not_a_number = zero div zero
let exact_key_map = map([1, "int", 1.0m, "decimal"]);

[
    [type(positive), positive, one % zero, negative, not_a_number, zero % zero],
    [decimal.inf, decimal.nan, -decimal.inf],
    [positive == decimal.inf, positive == inf,
        not_a_number == decimal.nan, not_a_number is nan],
    [positive < decimal.inf, positive <= decimal.inf,
        not_a_number < 1m, not_a_number >= 1m],
    [positive + 1m, 0m * positive],
    [string(positive), string(not_a_number)],
    [len(exact_key_map), exact_key_map[1], exact_key_map[1n]],
    sort([decimal.inf, 2m, decimal.nan, -decimal.inf])
]
