// S7.5.5/S7.6.3: an early propagated outcome is part of an inferred return.
fn failed() int^ => error("failed")
pn escaping() { failed()^; 7 }
pn main() { escaping(); 42 }
