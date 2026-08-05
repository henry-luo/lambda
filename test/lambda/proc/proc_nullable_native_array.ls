// Nullable primitive arrays use native lane words. Assigning null to the
// nullable target must not alter its covariant int[] source.
fn dynamic(value) any { value }

pn main() {
    var source: int[] = [1, 2]
    var target: int?[] = source
    target[1] = dynamic(null)
    print(string([source[1], target[0], target[1], target[2]]) ++ "\n")
}
