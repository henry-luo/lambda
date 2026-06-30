const assert = require('assert');
const { enableCompileCache } = require('module');

for (const invalid of [0, null, false, 1, NaN, true, Symbol(0)]) {
  assert.throws(() => enableCompileCache(invalid), { code: 'ERR_INVALID_ARG_TYPE' });
}

console.log('compile cache invalid args throw: true');
