var util = require('node:util');

var nested = { a: { b: { c: 1 } } };
console.log('depth0:', util.inspect(nested, { depth: 0 }));
console.log('depth1:', util.inspect(nested, { depth: 1 }));

var hidden = { visible: 1 };
Object.defineProperty(hidden, 'secret', { value: 2, enumerable: false });
console.log('hidden off:', util.inspect(hidden));
console.log('hidden on:', util.inspect(hidden, { showHidden: true }));

var colored = util.inspect({ value: 1 }, { colors: true });
console.log('color number:', colored.indexOf('\x1b[33m1\x1b[39m') >= 0);

var cycle = { name: 'root' };
cycle.self = cycle;
console.log('circular:', util.inspect(cycle, { depth: null }).indexOf('[Circular]') >= 0);
