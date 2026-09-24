// Fixture of vibe/impl/Lambda_List_Fixes (done).md, green since P4 (2026-09-23) on
// both tiers. Golden written from the rulings, not from the runtime.
// S2.5.8: text is placed as one value and walked as a sequence; select and
// reorder keep the text kind; a mapping pipe yields the text kind when every
// result is of that kind and an array otherwise; `in` is code-point
// membership; S7.2.1: an out-of-range subscript is null.

"-- select / reorder keep the text kind --";
[reverse("abc"), sort("cba"), unique("aab"), take("abc", 2), drop("abc", 1), slice("abc", 1, 3), "abc"[1 to 2]];
[reverse('abc'), reverse(b'\x0102')];
[type(reverse("abc")), type(reverse('abc')), type(reverse(b'\x0102'))]
"-- a binary walks as its bytes, and rebuilds as a binary --";
[take(b'\x010203', 2), drop(b'\x010203', 1), slice(b'\x010203', 1, 3), b'\x010203'[0], b'\x010203'[3] == null]
"-- filter --";
["abc" that ~ != "b", type("abc" that ~ != "b"), "abc" that ~ == "z", ("abc" that ~ == "z") == ""]
"-- mapping pipe: text when every result is text, else an array --";
["abc" |> upper(~), "abc" |> ~ ++ "-", "abc" |> ord(~), "abc" |> if (~ == "b") 1 else ~];
[type("abc" |> upper(~)), type("abc" |> ord(~))]
"-- membership is by code point; contains is the substring test --";
["a" in "cat", "at" in "cat", contains("cat", "at"), "x" in "cat"]
"-- iteration and length (S8.3.1v3) --";
[len("héllo"), for (c in "héllo") c]
"-- indexing: out of range is null (S7.2.1) --";
["abc"[0], "abc"[2], "abc"[3] == null, "abc"[-1] == null]
"-- placed as one value (S10.6.1) --";
[[1] ++ "ab", "ab" ++ [1], ["ab", "cd"]]
"-- non-ASCII symbols walk by code point too (LR05-15) --";
['café'[3], reverse('café'), sort('bé'), len('café'), 'café'[4] == null]
