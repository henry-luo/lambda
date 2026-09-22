// A promoted direct-callee cluster must preserve fmt's scalar call ABI.
fn fmt(num) {
    let rounded = float(int(num * 100.0)) / 100.0
    if (rounded == float(int(rounded))) { string(int(rounded)) }
    else { string(rounded) }
}

fn render_many(count, result) {
    if (count <= 0) { result }
    else { render_many(count - 1, fmt(12.345)) }
}

render_many(96, "")
