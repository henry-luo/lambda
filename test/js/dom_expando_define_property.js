var node = document.getElementById('item');
var cache = { count: 7 };

Object.defineProperty(node, '__jqueryData', {
  value: cache,
  configurable: true
});

console.log(node.__jqueryData === cache);
console.log(Object.getOwnPropertyDescriptor(node, '__jqueryData').configurable);
console.log(delete node.__jqueryData);
console.log(node.__jqueryData === undefined);
