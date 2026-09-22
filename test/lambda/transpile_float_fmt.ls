// Native float formatting must box scalar values before generic conversions.
fn fmt(num) {
    let rounded = float(int(num * 100.0)) / 100.0
    if (rounded == float(int(rounded))) { string(int(rounded)) }
    else { string(rounded) }
}

fmt(12.345)
