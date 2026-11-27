function currency(code, options, locale = 'en-US') {
  return new Intl.NumberFormat(locale, {style: 'currency', currency: code, ...options});
}

function rejects(action, type) {
  try { action(); return false; }
  catch (error) { return error instanceof type; }
}

const usd = currency('usd');
console.log(usd.format(1234.5) === '$1,234.50' && usd.format(-0) === '-$0.00');
const zero = currency('usd', {maximumFractionDigits: 0});
const resolved = zero.resolvedOptions();
console.log(zero.format(999.5) === '$1,000' && resolved.style === 'currency' && resolved.currency === 'USD' &&
  resolved.minimumFractionDigits === 0 && resolved.maximumFractionDigits === 0 &&
  resolved.currencySign === 'standard' && resolved.currencyDisplay === 'symbol');
console.log(currency('JPY').format(1234.5) === '¥1,235' &&
  currency('BHD').format(1.23) === 'BHD\xa01.230');
console.log(currency('JPY', {minimumFractionDigits: 2}).format(2) === '¥2.00' &&
  currency('USD', {minimumFractionDigits: 4}).format(2) === '$2.0000');

console.log(currency('CAD').format(5) === 'CA$5.00' &&
  currency('CAD', {currencyDisplay: 'narrowSymbol'}).format(5) === '$5.00' &&
  currency('CAD', {}, 'en-CA').format(5) === '$5.00');
console.log(currency('INR', {}, 'en-IN').format(12345678) === '₹1,23,45,678.00' &&
  currency('EUR', {}, 'en-AT').format(1234.5) === '€\xa01.234,50');
const code = currency('ZZZ', {currencyDisplay: 'code'});
console.log(code.format(1) === 'ZZZ\xa01.00' && code.format(Infinity) === 'ZZZ∞');
const accounting = currency('USD', {currencySign: 'accounting'});
const parts = accounting.formatToParts(-1234.5);
console.log(parts.map(part => part.type + ':' + part.value).join('|') ===
  'literal:(|currency:$|integer:1|group:,|integer:234|decimal:.|fraction:50|literal:)' &&
  parts.map(part => part.value).join('') === accounting.format(-1234.5));
console.log(currency('USD', {currencyDisplay: 'name'}).format(1) === '1.00 US dollars' &&
  currency('USD', {currencyDisplay: 'name', maximumFractionDigits: 0}).format(1.1) === '1 US dollar');
console.log(rejects(() => new Intl.NumberFormat('en', {style: 'currency'}), TypeError) &&
  rejects(() => currency('US1'), RangeError) &&
  rejects(() => currency('USD', {currencySign: 'bad'}), RangeError) &&
  rejects(() => currency('USD', {currencyDisplay: 'bad'}), RangeError) &&
  rejects(() => currency('USD', {minimumFractionDigits: 2, maximumFractionDigits: 1}), RangeError));
