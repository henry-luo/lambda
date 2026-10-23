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

// RegExp uses the same source cursor in both directions and through captures.
const scan = ('é😀a,').repeat(128) + '終';
const expression = /(é)(😀)a,/gu;
let match;
let count = 0;
while ((match = expression.exec(scan)) !== null) {
    if (match.index !== count * 5 || expression.lastIndex !== count * 5 + 5 ||
            match[1] !== 'é' || match[2] !== '😀') throw new Error('RegExp cursor lost UTF-16 coordinates');
    count++;
}
if (count !== 128 || expression.lastIndex !== 0) throw new Error('RegExp cursor failed at end');
expression.lastIndex = 5;
if (expression.exec(scan).index !== 5) throw new Error('RegExp cursor failed after backward seek');
console.log('unicode-regexp-cursor-ok');
const split = scan.split(/,/);
if (split.length !== 129 || split[0] !== 'é😀a' || split[128] !== '終')
    throw new Error('RegExp split lost a code unit');
if (scan.split(/(,)/).length !== 257 || '😀b'.split(/(?:)/).join('|') !== '\uD83D|\uDE00|b')
    throw new Error('RegExp split lost captures or a surrogate boundary');
console.log('unicode-regexp-split-ok');

// retain an earlier value while large concatenations cross allocation safepoints.
const concatChunk = 'é😀x'.repeat(64);
let assembled = 'prefix:';
let earlier = '';
for (let index = 0; index < 64; index++) {
    if (index === 32) earlier = assembled;
    assembled += concatChunk;
}
if (assembled.length !== 7 + 64 * 256 || earlier.length !== 7 + 32 * 256 ||
        assembled.slice(7, 11) !== 'é😀x' || earlier.slice(-4) !== 'é😀x' ||
        ('' + assembled) !== assembled || (earlier + '') !== earlier)
    throw new Error('String concatenation lost immutable payloads');
console.log('unicode-concat-allocation-ok');
