// S7.5.1v6: refreshing a forward call must still reject its unacknowledged error.
fn caller(value) bool^ {
    var i = 0
    while (i < 1) {
        if (not later(value)) { return false }
        i = i+1
    }
    return true
}
fn later(value) bool^ => if (value < 0) raise error("negative") else true
caller(1)^
