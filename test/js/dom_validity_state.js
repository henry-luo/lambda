var valueDescriptor = Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value');
var validDescriptor = Object.getOwnPropertyDescriptor(ValidityState.prototype, 'valid');
console.log(typeof valueDescriptor.get === 'function' && typeof valueDescriptor.set === 'function');
console.log(typeof validDescriptor.get === 'function' && validDescriptor.set === undefined &&
    !(Symbol.iterator in ValidityState.prototype));
var input = document.getElementById('input');
var state = input.validity;
console.log(state instanceof ValidityState && input.validity === state);
console.log(Object.prototype.toString.call(state) === '[object ValidityState]');
console.log(state.valid && validDescriptor.get.call(state));
valueDescriptor.set.call(input, 'not a URL');
console.log(state.typeMismatch && !state.valid && !validDescriptor.get.call(state));
valueDescriptor.set.call(input, 'https://example.test/path');
console.log(!state.typeMismatch && state.valid);
input.setCustomValidity('blocked');
console.log(state.customError && !state.valid);
input.setCustomValidity('');
input.required = true;
valueDescriptor.set.call(input, '');
console.log(state.valueMissing && !state.valid);
valueDescriptor.set.call(input, 'https://example.test/');
console.log(!state.valueMissing && state.valid);
for (var receiver of [{}, input, Object.create(ValidityState.prototype)]) {
    try { validDescriptor.get.call(receiver); console.log(false); }
    catch (error) { console.log(error instanceof TypeError); }
}
try { valueDescriptor.set.call(document.createElement('div'), 'wrong'); console.log(false); }
catch (error) { console.log(error instanceof TypeError); }
try { new ValidityState(); console.log(false); }
catch (error) { console.log(error instanceof TypeError); }
Object.setPrototypeOf(state, null);
console.log(validDescriptor.get.call(state));
input.remove();
input = null;
if (typeof gc === 'function') gc();
console.log(validDescriptor.get.call(state));
console.log('VALIDITY_STATE_DONE');
