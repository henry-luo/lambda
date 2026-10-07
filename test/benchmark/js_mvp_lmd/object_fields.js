function work() {
    let o = {x: 1, y: 2};
    let sum = 0;
    for (let i = 0; i < 500000; i++) { o.x = i; sum += o.x + o.y; }
    return sum;
}
work()
