// S12.1.3/S7.5.5: an event handler receives its own final pn outcome.
fn outcome() int^ => error("event")
view <button> {}
on click(evt) { outcome() }
on keydown(evt) { return outcome() }
true
