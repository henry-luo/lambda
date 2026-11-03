// S4.1.2, S4.2.1: constant propagation includes all flag writes, poison and evaluation order.
let fixed = 7
pn latch(a: float, b: float) int {
    var flag: int = 0
    if (a > 0.0) { flag = 1 }
    if (b > 0.0) { flag = 1 }
    return flag
}
pn clear(a: float) int {
    var flag: int = 1
    if (a == a) { flag = 0 }
    if (a < 0.0) { flag = 0 }
    return flag
}
pn wider(initial: int, test: bool) int {
    var flag: int = initial
    if (test) { flag = 1 }
    return flag
}
pn effect(test: bool) bool { print("condition "); return test }
pn effectful(test: bool) int {
    var flag: int = 0
    if (effect(test)) { flag = 1 }
    return flag
}
pn constants() int {
    var local: int = 5
    if (true) { local = 5 }
    return fixed + local
}
pn loop_latch(n: int, level: float) int {
    var flag: int = 0
    var i: int = 0
    while (i < n and flag == 0) {
        if (level > 0.0) { flag = 1 }
        i = i + 1
    }
    return i * 10 + flag
}
pn loop_clear(n: int, level: float) int {
    var flag: int = 1
    var i: int = 0
    while (i < n and flag == 1) {
        if (level == level) { flag = 0 }
        i = i + 1
    }
    return i * 10 + flag
}
pn repeated_writes() int {
    var flag: int = 0
    var i: int = 0
    while (i < 3 and flag == 0) {
        if (i >= 0) { flag = 1 }
        flag = 0
        i = i + 1
    }
    return i * 10 + flag
}
pn nested_latch() int {
    var flag: int = 0
    var i: int = 0
    while (i < 3 and flag == 0) {
        var j: int = 0
        while (j < 2) {
            if (j == 0) { flag = 1 }
            j = j + 1
        }
        i = i + 1
    }
    return i * 10 + flag
}
pn toggle(flag: bool) bool {
    if (flag) { flag = false } else { flag = true }
    return flag
}
pn select_flag(level: float) bool {
    var flag: bool = false
    if (level > 0.0) { flag = false } else { flag = true }
    return flag
}
pn main() {
    print(latch(0.0, 0.0)); print(" "); print(latch(1.0, -1.0)); print(" ")
    print(latch(-1.0, 1.0)); print(" "); print(latch(1.0, 1.0)); print("\n")
    print(latch(nan, nan)); print(" "); print(latch(inf, -inf)); print(" ")
    print(clear(nan)); print(" "); print(clear(-1.0)); print(" "); print(clear(1.0)); print("\n")
    print(wider(7, false)); print(" "); print(wider(7, true)); print(" "); print(wider(int(nan), false)); print("\n")
    print(effectful(false)); print(" "); print(effectful(true)); print("\n")
    print(constants()); print("\n")
    print(loop_latch(5, 1.0)); print(" "); print(loop_latch(5, -1.0)); print(" ")
    print(loop_latch(5, nan)); print(" "); print(loop_latch(int(nan), 1.0)); print("\n")
    print(loop_clear(5, 1.0)); print(" "); print(loop_clear(5, nan)); print("\n")
    print(repeated_writes()); print(" "); print(nested_latch()); print("\n")
    print(toggle(false)); print(" "); print(toggle(true)); print(" ")
    print(select_flag(1.0)); print(" "); print(select_flag(-1.0)); print(" "); print(select_flag(nan)); print("\n")
}
