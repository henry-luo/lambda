// A visible concrete call can specialize the raw body while a separate
// deferred direct call continues through the source-equivalent boxed lane.
fn dynamic(value) any { value }
fn twice(value) { value + value }

pn main() {
    print(string([twice(3), twice(dynamic(2.5))]) ++ "\n")
}
