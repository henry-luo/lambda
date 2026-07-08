{
  last_index: [1, 2, 3][last],
  arithmetic: [1, 2, 3][last - 1],
  slice: [1, 2, 3][1 to last],
  empty_is_null: [][last] is null,
  nested_innermost: [[10, 11], [20, 21]][last][0],
  string_last: "abc"[last],
  tail_limit: for (x in 1 to 100 limit last 3) x,
  replace_first: replace("a,b,c", ",", "+", {limit: 1}),
  replace_last: replace("a,b,c", ",", "+", {last: 1}),
  replace_zero: replace("a,b,c", ",", "+", {limit: 0}),
  find_last: find("a,b,c", ",", {last: 1}),
  map_key_last: {last: 3}.last,
  take_negative_is_error: take([1, 2], -1) is error,
  drop_negative_is_error: drop([1, 2], -1) is error
}
