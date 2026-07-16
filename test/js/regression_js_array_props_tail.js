function check(label, condition) {
    console.log(label + ":" + condition);
}

const wideFirst = [Number.MIN_VALUE, -Number.MIN_VALUE];
const identity = wideFirst;
wideFirst.meta = {answer: 42};
for (let i = 0; i < 256; i++) wideFirst.push(i + 0.25);
gc();
check("wide-first-identity", identity === wideFirst);
check("wide-first-props", wideFirst.meta.answer === 42);
check("wide-first-values",
    wideFirst[0] === Number.MIN_VALUE && wideFirst[1] === -Number.MIN_VALUE);
check("wide-first-growth", wideFirst.length === 258 && wideFirst[257] === 255.25);

const propsFirst = [];
propsFirst.meta = {alive: "yes"};
propsFirst.push(Number.MIN_VALUE);
propsFirst.push(-Number.MIN_VALUE);
for (let i = 0; i < 128; i++) propsFirst.push(i + 0.5);
propsFirst[0] = -Number.MIN_VALUE;
gc();
check("props-first-props", propsFirst.meta.alive === "yes");
check("props-first-values",
    propsFirst[0] === -Number.MIN_VALUE && propsFirst[1] === -Number.MIN_VALUE);
check("props-first-growth", propsFirst[129] === 127.5);

const sparse = [Number.MIN_VALUE];
sparse.named = {kept: true};
sparse[100000] = -Number.MIN_VALUE;
gc();
check("sparse-props", sparse.named.kept === true);
check("sparse-values",
    sparse[0] === Number.MIN_VALUE && sparse[100000] === -Number.MIN_VALUE);

const sliced = wideFirst.slice(0, 2);
const concatenated = propsFirst.concat([Number.MIN_VALUE]);
function makeArrayFromClone() {
    const source = [Number.MIN_VALUE, -Number.MIN_VALUE];
    source.meta = {temporary: true};
    return Array.from(source);
}
const fromClone = makeArrayFromClone();
gc();
check("clone-values",
    sliced[0] === Number.MIN_VALUE && sliced[1] === -Number.MIN_VALUE);
check("concat-values", concatenated[concatenated.length - 1] === Number.MIN_VALUE);
check("array-from-values",
    fromClone[0] === Number.MIN_VALUE && fromClone[1] === -Number.MIN_VALUE);
