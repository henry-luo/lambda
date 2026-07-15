pub pn worker(value) {
    sleep(1)^
    return value + 1
}

pub pn immediate(value) {
    return value * 2
}

pub pn fail() {
    sleep(-1)^
}
