// DG-5: decimal poison is a decimal value with a canonical, parseable spelling.
let zero = 0m
let one = 1m
let positive = one div zero
let negative = -one % zero
let not_a_number = zero div zero
let poison_map = map([positive, "decimal", inf, "float"]);

[
    [type(positive), positive, one % zero, negative, not_a_number, zero % zero],
    [decimal.inf, decimal.nan, -decimal.inf],
    [positive == decimal.inf, positive == inf,
        not_a_number == decimal.nan, not_a_number is nan],
    [positive < decimal.inf, positive <= decimal.inf,
        not_a_number < 1m, not_a_number >= 1m],
    [positive + 1m, 0m * positive],
    [string(positive), string(not_a_number)],
    [len(poison_map), poison_map[positive], poison_map[inf]],
    sort([decimal.inf, 2m, decimal.nan, -decimal.inf])
]
