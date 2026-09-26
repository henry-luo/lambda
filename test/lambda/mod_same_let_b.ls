// module for import_pub_let_same_name.ls: exports the same names as mod_same_let_a
pub let label = "from-b"
pub let sizes = [3, 4, 5]
pub fn who() => "b"
pub fn framed() => "[" ++ label ++ "]"
