// Present nullable pointers keep their source tag when admitted into a boxed union.
fn kind(value: string | binary) string => if (value is binary) "binary" else "string"
fn maybe_text(present: bool) string? => if (present) "abc" else null
fn converted(value: string | binary) binary^ => binary(value);
[kind(binary("abc")^), kind(maybe_text(true)),
 kind(maybe_text(false)) ^ { "absent" },
 converted(binary("abc")^)^ == binary("abc")^]
