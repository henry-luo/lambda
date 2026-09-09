// Group 257 matched during the first repetition but not the last one. It must
// be undefined rather than retain the stale capture from the earlier match.
let prefix = '';
for (let i = 0; i < 255; i++) prefix += '()';
const match = new RegExp(prefix + '((a)|(b))+').exec('ab');
console.log(match[257] === undefined, match[258]);
