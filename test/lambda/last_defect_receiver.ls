// S7.2.2: a defect-capable call still binds `last` to its own result.
fn words(value) any | error => split(value,"/")
words("one/two/three")[last] == "three"
split("a/b","/")[last] == "b"
split("a/b/c","/")[last-1] == "b"
