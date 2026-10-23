function outer(value) {
  const aliased = arguments;
  return function inner(other) {
    console.log(arguments[0] === other);
    aliased[0] = 17;
    console.log(value === 17);
    value = 23;
    console.log(aliased[0] === 23);
    console.log((() => { aliased[0] = 29; return value === 29; })());
    console.log(eval('value = 31; value') === 31 && aliased[0] === 31);
    delete aliased[0];
    value = 37;
    console.log(value === 37 && aliased[0] === undefined);
  };
}
outer(3)(7);

function duplicate(value, value) {
  const aliased = arguments;
  return function inner(other) {
    console.log(arguments.length === 1 && other === 5);
    aliased[0] = 11;
    console.log(value === 3);
    aliased[1] = 13;
    console.log(value === 13);
    value = 19;
    console.log(aliased[0] === 11 && aliased[1] === 19);
  };
}
duplicate(2, 3)(5);

function strictOuter(value) {
  'use strict';
  const aliased = arguments;
  return function inner(other) {
    console.log(arguments[0] === other);
    aliased[0] = 17;
    console.log(value === 3);
    value = 23;
    console.log(aliased[0] === 17);
  };
}
strictOuter(3)(7);

function defaults(value = 3) {
  const aliased = arguments;
  return function inner(other) {
    console.log(arguments[0] === other);
    aliased[0] = 17;
    console.log(value === 3);
    value = 23;
    console.log(aliased[0] === 17);
  };
}
defaults(3)(7);
console.log('MAPPED_OWNER_DONE');
