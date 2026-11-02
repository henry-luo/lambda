// S7.5.4/S11.4.2v3: retaining the callee error still requires a fallible return.
fn failed_target() any^ => error("target failure")
fn clean() => failed_target()()
clean()
