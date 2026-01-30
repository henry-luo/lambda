// Test input() with large integers and null values
// This verifies the map_put memory fix for NULL fields and 56-bit INT storage

"===== INPUT LARGE INTEGER JSON TEST ====="

let data = input("test/input/large_int_test.json", 'json')

"Large integers section:"
data.large_integers

"Individual large integer values:"
data.large_integers.billion
data.large_integers.ten_billion
data.large_integers.scientific
data.large_integers.max_safe
data.large_integers.min_safe

"With nulls section (tests null field memory alignment):"
data.with_nulls

"Nested section:"
data.nested
data.nested.level1.level2.big_value

"Many fields section:"
data.many_fields

"Arithmetic with loaded large integers:"
data.large_integers.ten_billion + 1
data.large_integers.scientific * 2

"Comparison tests:"
data.large_integers.ten_billion == 10000000000
data.large_integers.max_safe == 9007199254740991

"===== INPUT LARGE INTEGER JSON TEST COMPLETE ====="
