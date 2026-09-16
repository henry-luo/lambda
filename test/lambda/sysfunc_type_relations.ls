// S11.4.9/D3.3.5: registry relations instantiate result contracts from calls.
fn first_int(values: int[]) int => values[0] or 0;
fn first_text(values: string[]) string => values[0] or "";
fn expect_text(value: string) string => value;
fn open_fill(value: any) any => fill(2, value);
fn make_range() range => 0 to 3;

[
  first_int(fill(3, 1)),
  first_int(slice(fill(3, 2), 1)),
  len(take(make_range(), 2)),
  first_int(reverse(fill(2, 1))),
  first_int(unique(fill(2, 2))),
  first_int(drop(fill(3, 3), 1)),
  first_int(sort([2, 1])),
  first_text(sort(fill(2, "b"), "asc")),
  expect_text(replace("aba", "a", "z")),
  slice(null, 0, 1)
]
