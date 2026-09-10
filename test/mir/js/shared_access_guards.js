// D8.4.1v2: source candidates choose guards; every miss retains JS semantics.
function makeRecord(value) { return { number: value, link: null }; }
function readRecord(record) { return record.number; }
function writeRecord(record, value) { record.number = value; return record.number; }
function numericRegion(array) {
    let total = 0.5;
    for (let i = 0; i < array.length; i++) {
        const index = i | 0;
        total += array[index] * 0.5;
        array[index] = total;
    }
    return total;
}
const record = makeRecord(3.5);
console.log(readRecord(record), writeRecord(record, 4.5));
console.log(numericRegion([1.5, 2.5]), numericRegion(new Float64Array([1.5, 2.5])));
console.log(numericRegion(new Int32Array([-2147483648, 2147483647])));
