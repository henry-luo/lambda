function check(name, condition) {
  if (!condition) throw new Error('URLSearchParams constructor: ' + name);
  console.log(name + ': pass');
}
const source = new URLSearchParams('tab=activity&x=one&x=two');
const clone = new URLSearchParams(source);
check('clone preserves ordered duplicate entries', clone.toString() === 'tab=activity&x=one&x=two' && clone !== source);
clone.set('tab', 'settings');
source.append('tail', 'new');
check('clone owns an independent list', '?' + clone === '?tab=settings&x=one&x=two' && source.get('tab') === 'activity' && !clone.has('tail'));
const pairs = [['a', 1], ['a', 2]];
const array = new URLSearchParams(pairs);
pairs[0][1] = 99;
check('array pairs are converted and copied', array.toString() === 'a=1&a=2');
const map = new URLSearchParams(new Map([['a', 1], ['b', 2]]));
check('Map sequence', map.toString() === 'a=1&b=2');
let gets = 0;
const iterable = {get [Symbol.iterator]() {
  gets++;
  return function* () {
    if (typeof gc === 'function') gc();
    yield ['first', 'one'];
    yield new Set(['second', 'two']);
  };
}};
check('custom iterable and single iterator lookup', new URLSearchParams(iterable).toString() === 'first=one&second=two' && gets === 1);
const record = Object.create({inherited: 'excluded'});
Object.defineProperty(record, 'hidden', {value: 'excluded'});
Object.defineProperty(record, 'own', {enumerable: true, get() {
  if (typeof gc === 'function') gc();
  return {toString() {return 'value';}};
}});
check('record own enumerable properties', new URLSearchParams(record).toString() === 'own=value');
const malformed = {'\ud800': '\udfff'};
check('USVString normalization retains record lookup key', new URLSearchParams(malformed).toString() === '%EF%BF%BD=%EF%BF%BD');
check('primitive and default conversion', new URLSearchParams(42).toString() === '42=' && new URLSearchParams(true).toString() === 'true=' && new URLSearchParams(null).toString() === 'null=' && new URLSearchParams().toString() === '');
let errors = 0;
for (const value of [[['one']], [['one', 'two', 'three']], ['ab'], {[Symbol.iterator]: 1}, [[Symbol('key'), 'value']]]) {
  try {new URLSearchParams(value);} catch (error) {if (error instanceof TypeError) errors++;}
}
check('invalid sequence and coercion errors', errors === 5);
const sentinel = new Error('iterator getter');
let caught;
try {new URLSearchParams({get [Symbol.iterator]() {throw sentinel;}});} catch (error) {caught = error;}
check('iterator exception identity', caught === sentinel);
console.log('URL_SEARCH_PARAMS_CONSTRUCTOR_DONE');
