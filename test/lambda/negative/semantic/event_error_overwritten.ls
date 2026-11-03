// S12.1.3/S7.5.5: a handler's return cannot acknowledge an earlier result.
fn outcome() int^ => error("event")
view <button> {}
on click(evt) { outcome(); 42 }
