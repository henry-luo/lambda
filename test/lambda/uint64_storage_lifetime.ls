import .mod_uint64_storage_lifetime

// Churn full-width scalar homes after imported-module initialization. A BSS
// binding that retained the producer's activation pointer would be overwritten.
let churn = [
    [2i64, 9223372036854775806i64, 2u64, 18446744073709551614u64],
    [3i64, 9223372036854775805i64, 3u64, 18446744073709551613u64]
]

[small_i, wide_i, small_u, wide_u, storage_snapshot(), churn[1]]
