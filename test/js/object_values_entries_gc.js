for (const method of ['values', 'entries']) {
  let source = {get first() {
    delete source.b;
    source.late = 'outside key snapshot';
    return {value: 'getter'};
  }, a: {value: 'kept'}, b: 2};
  Object.defineProperty(source, 'hidden', {value: 'hidden'});
  source[Symbol('ignored')] = 'symbol';
  console.log(method, JSON.stringify(Object[method](source)));
  console.log(method, JSON.stringify(Object[method]('abc')));
  console.log(method, JSON.stringify(Object[method](new String('ab'))));
  console.log(method, Object[method](42).length);
  const proxy = new Proxy({x: {nested: 'live'}, y: 2}, {
    getOwnPropertyDescriptor(target, key) {
      Object.values({scratch: {allocation: key}});
      return Object.getOwnPropertyDescriptor(target, key);
    },
    get(target, key) {
      Object.entries({scratch: {allocation: key}});
      return target[key];
    }
  });
  console.log(method, JSON.stringify(Object[method](proxy)));
}
console.log('OBJECT_VALUES_ENTRIES_GC_DONE');
