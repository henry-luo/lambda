const retainedSet = new Set([
    ['first', '-value'].join(''),
    ['second', '-value'].join('')
]);
const retainedMap = new Map([
    [['first', '-key'].join(''), ['first', '-map-value'].join('')],
    [['second', '-key'].join(''), {label: ['second', '-map-value'].join('')}]
]);

for (let codePoint = 0; codePoint <= 65535; codePoint++) {
    const temporary = String.fromCharCode(codePoint).normalize('NFKD').toLowerCase();
    temporary.replace(/[a-z]/gu, value => value.toUpperCase());
}

console.log(Array.from(retainedSet).join(','));
console.log(retainedMap.get(['first', '-key'].join('')));
console.log(retainedMap.get(['second', '-key'].join('')).label);
