// descriptors on Error use ordinary property storage without changing the carrier.
function checkErrorField(name) {
    var error = new TypeError('initial');
    var stored = 'first';
    var setterReceiver = false;
    function getter() { return this === error ? stored : 'wrong receiver'; }
    function setter(value) { setterReceiver = this === error; stored = value; }
    Object.defineProperty(error, name, { get: getter, set: setter, configurable: true });
    Object.defineProperty(error, 'tag', { value: true });
    console.log(name, error[name], error.tag, error instanceof TypeError);
    error[name] = 'changed';
    var descriptor = Object.getOwnPropertyDescriptor(error, name);
    console.log('accessor', error[name], setterReceiver, descriptor.get === getter,
        descriptor.set === setter, descriptor.enumerable, descriptor.configurable);
    var names = Object.getOwnPropertyNames(error);
    console.log('own', Object.prototype.hasOwnProperty.call(error, name),
        names.filter(function(key) { return key === name; }).length);
    try { throw error; } catch (caught) {
        console.log('caught', caught === error, caught[name], caught.tag);
    }
    Object.defineProperty(error, name, { value: 'final', writable: true });
    descriptor = Object.getOwnPropertyDescriptor(error, name);
    console.log('data', error[name], descriptor.writable, descriptor.enumerable,
        descriptor.configurable);
    var prototype = Object.getPrototypeOf(error);
    var original = Object.getOwnPropertyDescriptor(prototype, name);
    Object.defineProperty(prototype, name, {
        value: 'prototype ' + name, writable: true, configurable: true
    });
    console.log('delete', delete error[name],
        Object.prototype.hasOwnProperty.call(error, name), error.tag);
    console.log('inherited', typeof error[name], error[name] === TypeError.prototype[name]);
    error[name] = 'again';
    console.log('restored', error[name], Object.prototype.hasOwnProperty.call(error, name), error.tag);
    if (original) Object.defineProperty(prototype, name, original);
    else delete prototype[name];
}

checkErrorField('message');
checkErrorField('stack');
checkErrorField('name');
checkErrorField('cause');
