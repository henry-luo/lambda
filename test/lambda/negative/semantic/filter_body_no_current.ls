// @expect-error: E238
// @description: S10.1.6: `|:` is single-mode. A body with no free `~` would
// read as whole-value application under `|>`, so the filter rejects it; the
// idiom is `xs |: is_even(~)`.

fn is_even(n: int) bool => n % 2 == 0;
[1, 2, 3, 4] |: is_even
