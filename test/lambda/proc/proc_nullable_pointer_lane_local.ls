// Nullable pointer locals keep a raw pointer lane; null is a zero pointer
// until this list construction boxes it back to ItemNull.
fn dynamic(value) any { value }

pn main() {
    var missing: string? = null
    var present: string? = "yes"
    missing = dynamic(null)
    print(string([missing, present]) ++ "\n")
}
