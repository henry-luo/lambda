// Procedural function with while loop
pn count_to(n: int) {
    var x = 0;
    while (x < n) {
        x = x + 1;
    }
    x
}

// With break
pn find_first_even(arr) {
    var result = -1;
    var i = 0;
    while (i < len(arr)) {
        if (arr[i] % 2 == 0) {
            result = arr[i];
            break;
        }
        i = i + 1;
    }
    result
}

// With continue
pn sum_odd(n: int) {
    var total = 0;
    var i = 0;
    while (i < n) {
        i = i + 1;
        if (i % 2 == 0) continue;
        total = total + i;
    }
    total
}

// With early return
pn factorial_proc(n: int) {
    if (n < 0) return -1;
    if (n <= 1) return 1;
    var result = 1;
    var i = 2;
    while (i <= n) {
        result = result * i;
        i = i + 1;
    }
    return result;
}

[count_to(10), find_first_even([1, 3, 5, 4, 7]), sum_odd(10), factorial_proc(5)]
