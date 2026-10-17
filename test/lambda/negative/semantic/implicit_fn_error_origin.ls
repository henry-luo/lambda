// A handled call in an earlier branch must not be blamed for a later error.
fn handled() int | error => error("handled")
fn first() int | error => error("first")
fn second() int | error => error("second")
fn branches(which: int) => if (which == 0) (handled() ^ { first() }) else second()
fn builtin(value: string) => int(value)
branches(0)
