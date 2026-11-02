// S7.5.1v6: an any join does not erase an actual ordinary-error alternative.
fn opaque(value) => value
fn hidden(flag: bool) {
    let failure = error("hidden");
    if (flag) failure else opaque(7)
}
fn forward_any(value: any) => value
fn underlying(value: error) => value.source
hidden(true)
