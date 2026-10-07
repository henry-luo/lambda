function work() {
    let m = new Map();
    for (let i = 0; i < 1000; i++) m.set(i, i);
    let sum = 0;
    for (let i = 0; i < 200000; i++) {
        let key = i % 1000;
        m.set(key, m.get(key) + 1);
        sum += m.get(key);
    }
    return sum;
}
work()
