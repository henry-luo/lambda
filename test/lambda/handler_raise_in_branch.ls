// A handler whose error arm raises sits inside an if branch: the arm runs only
// on the error path, so the branch's value must survive on the normal path
// (D8.3.4). MIR Direct had let the arm's `raise` mark the whole branch as
// returned, and the if-join then discarded the handled value.
fn checked(x) int^ => if (x < 0) raise error("negative " ++ string(x)) else x
fn wrap(v) map => {v: v}

fn in_else(x) map^ {
  if (x == 99) raise error("reserved")
  else wrap(checked(x) ^ { raise error("wrapped: " ++ ^.message) })
}
fn in_then(x) any^ => if (x != 99) checked(x) ^ { raise error("then: " ++ ^.message) } else 0
fn in_block(x) any^ {
  if (x == 99) 0
  else { let v = checked(x) ^ { raise error("block: " ++ ^.message) }; [v] }
}
fn two_arms(x) any^ =>
  if (x == 99) 0
  else checked(x) ^ { raise error("two: " ++ ^.message) } ~ { ~ * 10 }

"normal path:";
[(in_else(1) ^ { "error: " ++ ^.message }), (in_then(2) ^ { "error: " ++ ^.message }), (in_block(3) ^ { "error: " ++ ^.message }), (two_arms(4) ^ { "error: " ++ ^.message })];
"error arm raises:";
[(in_else(-1) ^ { "error: " ++ ^.message }), (in_then(-2) ^ { "error: " ++ ^.message }), (in_block(-3) ^ { "error: " ++ ^.message }), (two_arms(-4) ^ { "error: " ++ ^.message })];
"other branch:";
[(in_else(99) ^ { "error: " ++ ^.message }), (in_then(99) ^ { "error: " ++ ^.message }), (in_block(99) ^ { "error: " ++ ^.message }), (two_arms(99) ^ { "error: " ++ ^.message })]
