// S7.10.7: invalid query inputs are absent; valid calls retain precise types.
fn text_slice(s: string, a: int, b: int) string? => slice(s, a, b)
fn text_take(s: string, n: int) string? => take(s, n)
fn text_drop(s: string, n: int) string? => drop(s, n)
fn text_or_blank(s: string, a: int, b: int) string => slice(s, a, b) or ""
fn text_character(s: string, at: int) string => text_or_blank(s, at, at + 1)
fn text_parts(s: string, separator: string) string[] => split(s, separator)
fn open_parts(s, separator: string) string[]? => split(s, separator)
fn text_replace(s: string, needle: string, replacement: string) string => replace(s, needle, replacement)
fn optional_replace(s: string?, needle: string, replacement: string) string? => replace(s, needle, replacement)
fn checked_replace(s: string?) string => replace(s, "a", "b")
fn query_matches(s, needle) array? => find(s, needle)
fn lower_marks(s) => [for (m in find(lower(s), "a")) m.index]
fn admitted_error(s: any) array? | error => find(s, "a")
fn text_matches(s: string, needle: string) array => find(s, needle)
fn whitespace_parts(s: string, keep: bool) string[]? => split(s, null, keep)
fn byte_take(s: binary, n: int) binary? => take(s, n)
fn count_matches(s, needle) int => len(find(s, needle))
fn materialized_count(s, needle) int { let matches = find(s, needle); len(matches) }
fn count_options(s, needle, options) int => len(find(s, needle, options))
type SymbolPattern = \('x'+);

[text_slice("abc", nan, 2) is null, text_slice("abc", inf, 2) is null,
 slice("abc", 0.5) is null, slice("abc", "0", 2) is null,
 slice(7, 0, 1) is null, slice("abc", null, 2) is null];
[text_take("abc", -1) is null, text_drop("abc", nan) is null,
 take([1, 2], -1) is null, drop([1, 2], inf) is null,
 take(null, 1) is null, drop(7, 1) is null];
[text_slice("abc", -10, 20) == "abc", text_slice("abc", 1, 1) == "",
 text_take("abc", 2) == "ab", text_drop("abc", 1) == "bc",
 slice([1, 2], 1, 1) == [], byte_take(b'\x010203', 2) == b'\x0102',
 byte_take(b'\x0102', -1) is null, text_or_blank("abc", nan, 1) == "",
 text_character("abc", 1) == "b"];
[query_matches(7, "x") is null, find("abc", 7) is null,
 find("abc", SymbolPattern) is null, find("abc", "a", {limit: -1}) is null,
 find("abc", "a", {limit: 1, last: 1}) is null,
 find("abc", "a", {ignore_case: 1}) is null, find("abc", "x") == [],
 find(null, 7) is null, find(null, SymbolPattern) is null, find("abc", null) is null];
[replace(7, "a", "b") is null, replace(7, null, "b") is null,
 replace("abc", "a", 7) is null, replace("abc", SymbolPattern, "x") is null,
 replace("abc", "a", "x", {last: -1}) is null,
 replace("abc", "a", "x", 7) is null,
 text_replace("abc", "x", "y") == "abc", optional_replace(null, "a", "b") is null];
[split(7, ",") is null, split("abc", 7) is null,
 split("abc", SymbolPattern) is null, split("a,b", ",", 7) is null,
 split([1, 2], 3) is null, split([1, 2], 0) is null,
 split([1, 2], 2, 9) is null, split([1, 2], 1.5) is null,
 text_parts("a,b", ",") == ["a", "b"], open_parts(7, ",") is null,
 whitespace_parts("a b", true) is null,
 whitespace_parts("a b", false) == ["a", "b"], text_matches("abc", "x") == [],
 split(null, 7) is null, split(null, SymbolPattern) is null, split(null, null, true) is null];
[split("", ",") == [""], split("", "") == [],
 split("a,b", ",", true) == ["a", ",", "b"],
 split([1, 2, 3, 4], 2)[0] == [1, 2],
 take(reshape([1, 2, 3, 4], [2, 2]), nan) is null,
 slice(reshape([1, 2, 3, 4], [2, 2]), nan, 1) is null];
[count_matches(7, "a") == 0, count_matches("abc", 7) == 0,
 count_matches("abc", SymbolPattern) == 0,
 count_options("abc", "a", {limit: -1}) == 0,
 count_matches(7, "a") == materialized_count(7, "a"),
 count_matches("aba", "a") == materialized_count("aba", "a"), lower_marks("Aa") == [0, 1]];
[(slice("abc", error("offset"), 2) ^ { ^.message }) == "offset",
 (take(error("take"), 1) ^ { ^.message }) == "take",
 (drop("abc", error("drop")) ^ { ^.message }) == "drop",
 (find("abc", error("find")) ^ { ^.message }) == "find",
 (replace("abc", "a", "x", error("replace")) ^ { ^.message }) == "replace",
 (split("abc", ",", error("split")) ^ { ^.message }) == "split",
 (len(find(error("count"), "a")) ^ { ^.message }) == "count",
 (admitted_error(error("admitted")) ^ { ^.message }) == "admitted",
 checked_replace(null) is error, (checked_replace("abc") ^ { "" }) == "bbc"]
