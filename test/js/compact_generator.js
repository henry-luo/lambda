function* one() { yield 42; }
function* chain() { yield* one(); }
function* fallback() { yield 1; yield 2; }
function sample(label, iterator, calls) {
  let values = [];
  for (let call of calls) {
    try { values.push(iterator[call[0]](call[1])); }
    catch (e) { values.push('throw:' + String(e)); }
  }
  console.log(label + ':' + JSON.stringify(values));
}
sample('one', one(), [['next', 9], ['next', 10], ['next', 11]]);
sample('one-return', one(), [['next'], ['return', 7], ['next']]);
sample('one-throw', one(), [['next'], ['throw', 'bad'], ['next']]);
sample('chain', chain(), [['next'], ['next'], ['next']]);
sample('chain-return', chain(), [['next'], ['return', 5], ['next']]);
sample('fallback', fallback(), [['next'], ['next'], ['next']]);
let custom = { [Symbol.iterator]() { let count = 0; return {
  next() { return count++ ? {value: 99, done: true} : {value: 8, done: false, extra: 'ok'}; },
  return(value) { return {value: value + 1, done: true}; },
  throw(value) { return {value: value + '!', done: true}; }
}; } };
function* delegated() { yield* custom; }
sample('custom', delegated(), [['next'], ['next']]);
sample('custom-return', delegated(), [['next'], ['return', 4]]);
sample('custom-throw', delegated(), [['next'], ['throw', 'x']]);
let starts = 0;
function captured(value) { return function* () { yield value; }; }
function* tail(value) {
  starts++;
  const local = value + 1;
  yield* captured(local)();
}
const delayed = tail(6);
console.log('lazy:' + starts);
sample('tail', delayed, [['next'], ['next']]);
console.log('starts:' + starts);
sample('before-start-return', tail(1), [['return', 12], ['next']]);
console.log('starts:' + starts);
function* fallbackNested() { yield 1; yield* one(); }
sample('fallback-nested', fallbackNested(), [['next'], ['next'], ['next']]);
let reentry = 0;
const withGetter = { [Symbol.iterator]() { return {
  next() { return {get done() { reentry++; return false; }, get value() { reentry++; return 3; }}; },
  return(value) { return {value, done: true}; }
}; } };
function* getterDelegate() { yield* withGetter; }
sample('getter-return', getterDelegate(), [['next'], ['return', 4]]);
console.log('getters:' + reentry);
function* gcTail() { yield* [5e-324]; }
const gcIterator = gcTail();
gc();
const gcFirst = gcIterator.next();
gc();
console.log('gc:' + Object.is(gcFirst.value, 5e-324) + ':' + gcIterator.next().done);
function* deepLeaf() { gc(); yield 5e-324; }
function* deepChain(n, source) { yield* (n ? deepChain(n - 1, source) : source); }
const deepReturn = deepChain(2048, deepLeaf());
console.log('deep-value:' + Object.is(deepReturn.next().value, 5e-324));
gc();
sample('deep-return', deepReturn, [['return', 17], ['next']]);
const deepThrow = deepChain(2048, deepLeaf());
deepThrow.next();
sample('deep-throw', deepThrow, [['throw', 'stop'], ['next']]);
let opaqueStarts = 0;
function* opaqueChain(n) {
  opaqueStarts++;
  const child = n ? opaqueChain(n - 1) : one();
  yield* {
    [Symbol.iterator]() { return this; },
    next(value) { return child.next(value); },
    return(value) { return child.return(value); }
  };
}
const opaque = opaqueChain(700);
console.log('opaque:' + opaque.next().value + ':' + opaqueStarts + ':' + opaque.return(19).value + ':' + opaqueStarts + ':' + opaque.next().done);
