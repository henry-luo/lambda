'use strict';

var texts = ['abcabc', 'a\u0000bc\u0000d', 'éxé', '😀x😀', '\ud800x\udc00', ''];
var needles = ['bc', '\u0000', 'é', '😀', '\udc00', ''];
for (var i = 0; i < texts.length; i++) {
    var text = texts[i], needle = needles[i];
    console.log('string', i, text.indexOf(needle), text.indexOf(needle, 1),
        text.lastIndexOf(needle), text.lastIndexOf(needle, 1), text.includes(needle));
}
console.log('bounds', 'abc'.indexOf('a', -9), 'abc'.indexOf('', 99),
    'abc'.lastIndexOf('', -9), 'abc'.indexOf('longer'));
console.log('surrogates', '😀x😀'.indexOf('\ud83d'), '😀x😀'.indexOf('\ude00'),
    '😀x😀'.lastIndexOf('\ude00'), '😀x😀'.indexOf('😀', 1),
    '😀x😀'.startsWith('\ude00', 1), '😀x😀'.endsWith('\ud83d', 4));
console.log('positions', 'abc'.indexOf('a', Infinity), 'abc'.lastIndexOf('a', NaN),
    'abc'.lastIndexOf('a', -Infinity), 'abc'.startsWith('', Infinity),
    'abc'.endsWith('c', Infinity), 'abc'.endsWith('', -Infinity));
console.log('missing', 'undefined'.indexOf(), 'undefined'.lastIndexOf(),
    'undefined'.includes(), 'undefined'.startsWith(), 'undefined'.endsWith());
var order = [];
var search = { toString: function() { order.push('search'); return 'b'; } };
var position = { valueOf: function() { order.push('position'); return 1; } };
console.log('coercion', 'abc'.indexOf(search, position), order.join(','));
var b = Buffer.from([0, 255, 2, 0, 255, 2, 9]);
var n = Buffer.from([0, 255, 2]);
console.log('buffer', b.indexOf(n), b.indexOf(n, 1), b.lastIndexOf(n), b.lastIndexOf(n, 2));
console.log('byte', b.indexOf(255), b.lastIndexOf(255), b.indexOf(255, -3), b.includes(9));
console.log('empty-needle', b.indexOf(''), b.indexOf('', 4), b.lastIndexOf('', 4));
var utf16 = Buffer.from('ababa', 'utf16le');
console.log('utf16-buffer', utf16.indexOf('ba', 0, 'utf16le'), utf16.lastIndexOf('ba', undefined, 'utf16le'));
console.log('absent', b.indexOf(Buffer.from([8, 7])), b.lastIndexOf(Buffer.alloc(20)), b.indexOf(2, 99));
