// A dynamically supplied ItemNull becomes the dedicated float? lane marker.
fn dynamic(value) any { value }

pn main() {
    var absent: float? = dynamic(null)
    var present: float? = 1.5
    print(string([absent, present]) ++ "\n")
}
