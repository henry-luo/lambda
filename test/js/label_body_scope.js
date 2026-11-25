// labels belong to one function or class static block, while outer targets survive parsing them.
outer: {
    function declared() {
        let count = 0;
        outer: for (let i = 0; i < 3; i++) { count++; if (i === 1) break outer; }
        return count;
    }
    console.log(declared() === 2);
    break outer;
}
outer: {
    const expression = function() { outer: { break outer; } return 3; };
    console.log(expression() === 3);
    break outer;
}
outer: {
    const arrow = () => { outer: { break outer; } return 4; };
    console.log(arrow() === 4);
    break outer;
}
outer: {
    const object = {method() { outer: { break outer; } return 5; }};
    console.log(object.method() === 5);
    break outer;
}
outer: {
    const object = {get value() { outer: { break outer; } return 6; }};
    console.log(object.value === 6);
    break outer;
}
outer: {
    class Example { method() { outer: { break outer; } return 7; } }
    console.log(new Example().method() === 7);
    break outer;
}
outer: {
    function* values() { outer: { yield 8; break outer; } yield 9; }
    console.log(Array.from(values()).join(',') === '8,9');
    break outer;
}
outer: {
    let value = 0;
    class Example { static { outer: { value = 10; break outer; } value++; } }
    console.log(value === 11);
    break outer;
}
function afterStatic() { class Example { static {} } return 12; }
console.log(afterStatic() === 12);
let iterations = 0;
outer: for (let i = 0; i < 2; i++) {
    const unused = () => { outer: { break outer; } };
    iterations++;
    continue outer;
}
console.log(iterations === 2);
