// @expect-error: E103
// @description: an array suffix may follow an array suffix (S11.1.1v3), but a
// retired count keeps its teaching error there too (S11.1.6v2): `int[][2+]` is
// not an array of at least two `int[]`s; that is `[int[]{2+}]`.

type Rows = int[][2+]
