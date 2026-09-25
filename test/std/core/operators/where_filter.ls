// Test: Filter stage |: (formerly spelled where, then that)
// Layer: 3 | Category: operator | Covers: |: filtering

[1, 2, 3, 4, 5] |: (~ > 3);
[1, 2, 3, 4, 5] |: (~ % 2 == 0);
[1, 2, 3, 4, 5] |: (~ == 3);
[1, 2, 3] |: (~ > 10);
[1, 2, 3] |: (~ > 0);
[1, 2, 3, 4, 5] |> ~ * 2 |: (~ > 5)
