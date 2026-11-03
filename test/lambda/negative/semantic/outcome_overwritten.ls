// S7.5.5: a later produced value cannot erase an unacknowledged error.
pn main() {
    error("unacknowledged")
    let no_result = 1
    0
}
