// Nullable sized-integer arrays widen their native word to i64; the null
// sentinel must not contaminate the covariant non-null i8[] source.
fn dynamic(value) any { value }

pn main() {
    var source: i8[] = [1i8, 2i8]
    var target: i8?[] = source
    target[1] = dynamic(null)
    print(string([source[1], target[0], target[1], target[2]]) ++ "\n")
}
