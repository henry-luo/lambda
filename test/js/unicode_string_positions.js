// UTF-16 positions must survive forward/backward seeks and cache replacement.
const units = ['é', '\uD83D', '\uDE00', 'a', '\uD800', 'z', '\uDC00'];
const text = 'é😀a\uD800z\uDC00'.repeat(64);
if (text.length !== units.length * 64) throw new Error('Unicode length changed');

for (let direction of [1, -1]) {
    for (let step = 0; step < text.length; step++) {
        const index = direction > 0 ? step : text.length - step - 1;
        const expected = units[index % units.length];
        if (text[index] !== expected || text.charAt(index) !== expected ||
            text.charCodeAt(index) !== expected.charCodeAt(0) ||
            text.substring(index, index + 1) !== expected ||
            text.substring(index + 1, index) !== expected ||
            text.slice(index, index + 1) !== expected ||
            text.substr(index, 1) !== expected) {
            throw new Error('Unicode seek lost a code unit at ' + index);
        }
    }
}
console.log('unicode-seeks-ok');

for (let index = 0; index < text.length; index += units.length) {
    if (text.substring(index + 1, index + 3) !== '😀' ||
        text.codePointAt(index + 1) !== 0x1F600 ||
        text.codePointAt(index + 2) !== 0xDE00) {
        throw new Error('Unicode seek lost a surrogate boundary');
    }
}
if (text.charAt(text.length) !== '' || text[text.length] !== undefined ||
    !Number.isNaN(text.charCodeAt(text.length))) {
    throw new Error('Unicode seek crossed the string end');
}
console.log('unicode-boundaries-ok');

for (let pass = 0; pass < 12; pass++) {
    const source = ('😀é' + pass).repeat(8);
    const suffix = '' + pass;
    if (source.slice(-suffix.length) !== suffix || source.charAt(0) !== '\uD83D' ||
        source.charAt(1) !== '\uDE00' || source.charAt(2) !== 'é' ||
        text.substring(1, 3) !== '😀') {
        throw new Error('Unicode position cache retained another source');
    }
}
console.log('unicode-source-replacement-ok');
