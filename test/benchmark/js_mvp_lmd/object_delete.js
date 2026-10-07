function work() {
    let o = {a: 1, b: 2};
    let sum = 0;
    for (let i = 0; i < 20000; i++) { delete o.a; o.a = i; sum += o.a + o.b; }
    return sum;
}
work()
