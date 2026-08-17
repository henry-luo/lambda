// S7.6.1v3: directly raised pn calls branch without an intermediate binding.
pn pn_func(value, fail) int^ {
    if (fail) {
        raise error("pn failure")
    }
    return value
}

pn main() {
    pn_func(7, false) ^ {
        print(["error", ^.message])
    } ~ {
        print(["value", ~ * 2])
    }

    pn_func(7, true) ^ {
        print(["error", ^.message])
    } ~ {
        print(["value", ~ * 2])
    }
}
