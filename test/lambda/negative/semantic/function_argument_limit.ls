// @expect-error: E230
// A Core Lambda definition reserves one direct ABI operand per formal.
fn too_many(a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p, q) => a
