{
  numeric_lt: 1 < 2,
  string_lt: "a" < "b",
  string_ge: "b" >= "a",
  null_lt_is_null: (null < 5) is null,
  date_lt: t'2025-01-01' < t'2025-06-01',
  sort_symbols_still_total: sort(['b', 'a'])
}
