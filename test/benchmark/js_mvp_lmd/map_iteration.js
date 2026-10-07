function work() {
    let m = new Map();
    for (let i = 0; i < 1000; i++) m.set(i, i + 1);
    let sum = 0;
    for (let round = 0; round < 200; round++) {
        for (let [key, value] of m) sum += key + value;
    }
    return sum;
}
work()
