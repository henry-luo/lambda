// internal reactions must not call a user species getter a second time.
var intrinsicPromise = Promise;
var previousSpecies = Object.getOwnPropertyDescriptor(Promise, Symbol.species);
var speciesReads = 0;
function Species(executor) {
  if (typeof gc === 'function') gc();
  return new intrinsicPromise(executor);
}
Object.defineProperty(Promise, Symbol.species, {configurable: true, get: function () {
  speciesReads++;
  if (speciesReads > 1) throw new Error('internal species recursion');
  return Species;
}});
var source = new intrinsicPromise(function (resolve) {resolve('ready');});
var chained = source.then(function(value) {
  if (typeof gc === 'function') gc();
  console.log('handler:' + value);
  return 'done';
});
console.log('species:' + speciesReads);
Object.defineProperty(Promise, Symbol.species, previousSpecies);
chained.then(function(value) {console.log('result:' + value);});
