let immediate;
let resumed;
async function async_body() {
    const {increment} = {increment: value => value + 1};
    let value = 4;
    function read() { return increment(value); }
    immediate = read;
    await null;
    value = 6;
    resumed = read;
}
const completed = async_body();
console.log('async immediate', immediate());
completed.then(() => console.log('async resumed', resumed(), immediate()));

function* generator_body() {
    const {increment} = {increment: value => value + 2};
    let value = 8;
    function read() { return increment(value); }
    yield read;
    value = 10;
    yield read;
}
const generator = generator_body();
const before = generator.next().value;
console.log('generator first', before());
const after = generator.next().value;
console.log('generator resumed', after(), before());
console.log('generator complete', generator.next().done, before());
