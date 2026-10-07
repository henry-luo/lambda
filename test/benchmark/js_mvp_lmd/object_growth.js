function work() {
    let sum = 0;
    for (let round = 0; round < 100; round++) {
        let o = {};
        for (let i = 0; i < 200; i++) o['k' + i] = i;
        for (let value of Object.values(o)) sum += value;
    }
    return sum;
}
work()
