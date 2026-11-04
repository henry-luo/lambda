// D7.1.2: every explicit return participates in the result ABI proof.
fn mixed(flag) {
    if (flag) { return 1 }
    return "ok"
}

fn numeric(flag) {
    if (flag) { return 1.25 }
    return 7
}

fn falling(flag) {
    if (flag) { return 2.5 }
    3
}

[mixed(true), mixed(false), numeric(true), numeric(false), falling(true), falling(false)]
