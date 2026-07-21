// Imported module bindings must own boxed wide-scalar payloads after module init.
pub small_i = 1i64
pub wide_i = 9223372036854775807i64
pub small_u = 1u64
pub wide_u = 18446744073709551615u64

pub fn storage_snapshot() => [small_i, wide_i, small_u, wide_u]
