// Regression: a call inside a typed-array subscript loop can resize and move
// the backing store, so P4h must not retain the pre-loop data pointer or length.
const forBuffer = new ArrayBuffer(2, { maxByteLength: 1048576 });
const forView = new Uint8Array(forBuffer);
for (let i = 0; i < 4; i++) {
    if (i === 1) forBuffer.resize(1048576);
    forView[i] = i + 1;
}
console.log("for=" + forView[0] + "," + forView[1] + "," + forView[2] + "," + forView[3]);

const whileBuffer = new ArrayBuffer(2, { maxByteLength: 1048576 });
const whileView = new Uint8Array(whileBuffer);
let j = 0;
while (j < 4) {
    if (j === 1) whileBuffer.resize(1048576);
    whileView[j] = j + 5;
    j++;
}
console.log("while=" + whileView[0] + "," + whileView[1] + "," + whileView[2] + "," + whileView[3]);
