function check(name, condition) {
  if (!condition) throw new Error('fractional radix: ' + name);
  console.log(name + ': pass');
}
check('fractional binary and hexadecimal', (0.5).toString(2) === '0.1' && (10.25).toString(16) === 'a.4');
check('fractional base36 and sign', (0.5).toString(36) === '0.i' && (-0.25).toString(36) === '-0.9');
const format = Number.prototype.toString;
check('captured method and radix truncation', format.call(0.5, 36.9) === '0.i' && format.call(0.5, '16') === '0.8');
check('Redux random-id formatting shape', /^0\.[0-9a-z]+$/.test((0.123456789).toString(36)));
check('integer and special values', (255).toString(16) === 'ff' && (-0).toString(36) === '0' && NaN.toString(36) === 'NaN' && Infinity.toString(36) === 'Infinity');
let errors = 0;
for (const radix of [1, 37, NaN]) {
  try {format.call(0.5, radix);} catch (error) {if (error instanceof RangeError) errors++;}
}
check('invalid radix errors', errors === 3);
console.log('NUMBER_FRACTIONAL_RADIX_DONE');
