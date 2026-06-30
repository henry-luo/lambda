const assert = require('assert');
const vm = require('vm');

const source = '(function cachedToken() { return 42; })';
const script = new vm.Script(source);
const cachedData = script.createCachedData();

console.log('cache is buffer:', Buffer.isBuffer(cachedData));
console.log('cache length:', cachedData.length > 0);

const accepted = new vm.Script(source, { cachedData });
console.log('accepted rejected flag:', accepted.cachedDataRejected);
console.log('accepted result:', accepted.runInThisContext()());

const rejected = new vm.Script('(function cachedToken() { return 43; })', { cachedData });
console.log('changed source rejected:', rejected.cachedDataRejected);
console.log('changed source result:', rejected.runInThisContext()());

const produced = new vm.Script(source, { produceCachedData: true });
console.log('produce flag:', produced.cachedDataProduced);
console.log('produced buffer:', Buffer.isBuffer(produced.cachedData));

assert.throws(() => new vm.Script(source, { cachedData: 'not cache data' }), {
  code: 'ERR_INVALID_ARG_TYPE'
});
console.log('invalid type throws: true');
