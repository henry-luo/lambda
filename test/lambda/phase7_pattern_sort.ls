{
  exact_type_match: ([5] is [int])
}
{
  exact_type_miss: ([1, 2] is [int])
}
{
  mixed_pattern_match: ([1, 2, "ok"] is [1, int, "ok"])
}
{
  mixed_pattern_miss: ([1, "x", "ok"] is [1, int, "ok"])
}
sort(["b", "a", "c"])
sort([3.0, nan, 1.0])
sort([true, null, false, "a", 2])
sort(["b", "a", "c"], "desc")
