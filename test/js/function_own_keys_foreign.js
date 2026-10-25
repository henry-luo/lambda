function check(name, condition) {
  if (!condition) throw new Error('function own keys: ' + name);
  console.log(name + ': pass');
}
function Component(first, second) {}
const marker = Symbol('component');
Component.visible = 'value';
Component[marker] = 'symbol';
Object.defineProperty(Component, 'hidden', {value: 'retained', configurable: true});
const names = Object.getOwnPropertyNames;
const symbols = Object.getOwnPropertySymbols;
const keys = names(Component);
check('callable string keys cross native reflection', keys.includes('name') && keys.includes('length') && keys.includes('prototype') && keys.includes('visible') && keys.includes('hidden') && keys.every(key => typeof key === 'string'));
check('callable Symbol keys cross native reflection', symbols(Component).length === 1 && symbols(Component)[0] === marker && Reflect.ownKeys(Component).includes(marker));
const target = () => {};
for (const key of names(Component).filter(key => key === 'visible' || key === 'hidden')) {
  Object.defineProperty(target, key, Object.getOwnPropertyDescriptor(Component, key));
}
check('component static hoisting', target.visible === 'value' && target.hidden === 'retained' && !Object.getOwnPropertyDescriptor(target, 'hidden').enumerable && !names(target).includes('prototype'));
if (typeof gc === 'function') gc();
check('reflection retained across collection', names(Component).includes('hidden') && symbols(Component)[0] === marker);
const proxy = new Proxy(Component, {ownKeys: object => Reflect.ownKeys(object)});
check('proxy reflection filters key kinds', names(proxy).every(key => typeof key === 'string') && symbols(proxy).length === 1 && symbols(proxy)[0] === marker);
let typedKeys = true;
for (const Constructor of [Uint8Array, BigInt64Array]) {
  const array = new Constructor(2);
  array.label = 'value';
  Object.defineProperty(array, 'hidden', {value: 1});
  Object.defineProperty(array, marker, {value: 'symbol'});
  const own = Reflect.ownKeys(array);
  typedKeys = typedKeys && names(array).join(',') === '0,1,label,hidden' && symbols(array).length === 1 && symbols(array)[0] === marker && own.length === 5 && own[4] === marker;
}
check('typed-array reflection retains Symbol keys', typedKeys);
console.log('FUNCTION_OWN_KEYS_FOREIGN_DONE');
