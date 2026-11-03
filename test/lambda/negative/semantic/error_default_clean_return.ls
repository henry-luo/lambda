// S7.6.3v3: a default expression cannot propagate through a clean return.
fn failed() int^ => error("default failure")
fn clean(value = failed()^) int => value
clean()
