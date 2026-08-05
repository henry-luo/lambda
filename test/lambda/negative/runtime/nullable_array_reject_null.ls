// A non-null int[] must reject a dynamically supplied null instead of widening.
fn dynamic(value) any { value }

pn main() {
    var values: int[] = [1, 2]
    values[0] = dynamic(null)
}
