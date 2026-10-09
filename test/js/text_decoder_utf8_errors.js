function check(condition, message) {
    if (!condition) throw new Error(message);
}
const cases = [
    [[0xE2, 0x82], "\ufffd"],
    [[0xE2, 0x82, 0x41], "\ufffdA"],
    [[0xED, 0xA0, 0x80], "\ufffd\ufffd\ufffd"],
    [[0xF0, 0x80, 0x80, 0x80], "\ufffd\ufffd\ufffd\ufffd"],
    [[0xF4, 0x90, 0x80, 0x80], "\ufffd\ufffd\ufffd\ufffd"],
    [[0xC2, 0x7F], "\ufffd\x7f"],
    [[0xEF, 0xBB, 0xBF, 0x41], "A"],
    [[0xF0, 0x9F, 0x98, 0x80], "\ud83d\ude00"],
    [[0, 0x41], "\x00A"]
];
for (let i = 0; i < cases.length; i++) {
    const input = new Uint8Array(cases[i][0]);
    const expected = cases[i][1];
    check(new TextDecoder().decode(input) === expected, "view decode case " + i);
    check(new TextDecoder().decode(input.buffer) === expected, "buffer decode case " + i);
}
check(new TextDecoder("utf-8", {ignoreBOM: true}).decode(
    new Uint8Array([0xEF, 0xBB, 0xBF, 0x41])) === "\ufeffA", "ignoreBOM changed");
let rejected = false;
try { new TextDecoder("utf-8", {fatal: true}).decode(new Uint8Array([0xE2, 0x82])); }
catch (error) { rejected = error instanceof TypeError; }
check(rejected, "fatal decoder accepted invalid UTF-8");
console.log("UTF-8 decoder replacement, BOM, and fatal handling passed");
