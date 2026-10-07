function work() {
    let o = {x: 1, y: true};
    let sum = 0;
    for (let i = 0; i < 100000; i++) {
        o.x = i;
        sum += o.x;
        o.x = 'text';
        o.x = i;
    }
    return sum;
}
work()
