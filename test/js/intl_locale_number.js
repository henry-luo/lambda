const locale = new Intl.Locale('SR-cyrl-rs');
console.log(locale instanceof Intl.Locale && locale.toString() === 'sr-Cyrl-RS');
console.log(locale.language === 'sr' && locale.script === 'Cyrl' && locale.region === 'RS');
console.log(locale.baseName === 'sr-Cyrl-RS');
console.log(new Intl.Locale('de-DE-u-nu-latn').baseName === 'de-DE');
console.log(new Intl.Locale('de-DE-u-nu-latn').toString() === 'de-DE-u-nu-latn');
const changed = new Intl.Locale('en-US', {language:'zh', script:'hANT', region:'tw'});
console.log(String(changed) === 'zh-Hant-TW');
console.log(new Intl.Locale('en').script === undefined && new Intl.Locale('en').region === undefined);
function rejects(action, type) { try { action(); return false; } catch (e) { return e instanceof type; } }
console.log(['en_', 'e-US', 'abcd', 'en--US', 'en-US-US', 'en-1901-1901', 'en-u'].every(tag => rejects(() => new Intl.Locale(tag), RangeError)));
console.log(rejects(() => Intl.Locale('en'), TypeError) && rejects(() => new Intl.Locale(), TypeError));
const getter = Object.getOwnPropertyDescriptor(Intl.Locale.prototype, 'language').get;
console.log([{}, Object.create(Intl.Locale.prototype), new Proxy(locale,{})].every(value => rejects(() => getter.call(value), TypeError)));
Object.freeze(locale);
console.log(getter.call(locale) === 'sr');
class ChildLocale extends Intl.Locale {}
console.log(new ChildLocale('en-US') instanceof ChildLocale);
console.log(Object.prototype.toString.call(locale) === '[object Intl.Locale]');
const likelyCases = [
  ['en','en-Latn-US'], ['ar','ar-Arab-EG'], ['he','he-Hebr-IL'],
  ['sr','sr-Cyrl-RS'], ['sr-Latn','sr-Latn-RS'], ['zh-TW','zh-Hant-TW'],
  ['zh-HK','zh-Hant-HK'], ['und-Arab','ar-Arab-EG'], ['und-TW','zh-Hant-TW'],
  ['en-GB','en-Latn-GB'], ['qaa','qaa'], ['xx-Arab','xx-Arab']
];
console.log(likelyCases.every(([input, expected]) => String(new Intl.Locale(input).maximize()) === expected));
console.log([['en-Latn-US','en'],['sr-Cyrl-RS','sr'],['sr-Latn-RS','sr-Latn'],['zh-Hant-TW','zh-TW'],['zh-Hant-HK','zh-HK']]
  .every(([input, expected]) => String(new Intl.Locale(input).minimize()) === expected));
const extended = new Intl.Locale('de-1901-u-nu-latn');
console.log(String(extended.maximize()) === 'de-Latn-DE-1901-u-nu-latn' && String(extended.maximize().minimize()) === 'de-1901-u-nu-latn');
console.log(likelyCases.every(([input]) => {
  const maximal = new Intl.Locale(input).maximize();
  return String(maximal.maximize()) === String(maximal) && String(maximal.minimize().maximize()) === String(maximal);
}));
console.log([undefined, 0, {}, Object.create(Intl.Locale.prototype), new Proxy(locale,{})]
  .every(value => ['maximize','minimize'].every(method => rejects(() => Intl.Locale.prototype[method].call(value), TypeError))));
const hidden = new Intl.Locale('zh-TW');
Object.defineProperty(hidden, 'language', {get() {throw new Error('public language');}});
Object.defineProperty(hidden, 'toString', {value() {throw new Error('public toString');}});
console.log(String(hidden.maximize()) === 'zh-Hant-TW' && String(hidden.minimize()) === 'zh-TW');
console.log(locale.maximize() !== locale && locale.minimize() !== locale && locale.maximize() instanceof Intl.Locale);
const childMaximum = new ChildLocale('en').maximize();
console.log(childMaximum instanceof Intl.Locale && !(childMaximum instanceof ChildLocale));
const LocaleConstructor = Intl.Locale;
Intl.Locale = function() {throw new Error('replaced Locale');};
try { console.log(locale.maximize() instanceof LocaleConstructor && locale.minimize() instanceof LocaleConstructor); }
finally { Intl.Locale = LocaleConstructor; }
console.log(['maximize','minimize'].every(name => {
  const d = Object.getOwnPropertyDescriptor(Intl.Locale.prototype,name);
  return d.writable && d.configurable && !d.enumerable && d.value.name === name && d.value.length === 0;
}));
const format = new Intl.NumberFormat('en-US');
console.log(format.format(12345.6) === '12,345.6');
console.log(format.format === format.format);
const bound = format.format;
console.log(bound.call(null, -12345.6) === '-12,345.6');
console.log(format.formatToParts(-12345.6).map(part => part.type + ':' + part.value).join('|') === 'minusSign:-|integer:12|group:,|integer:345|decimal:.|fraction:6');
console.log(format.formatToParts(-12345.6).map(part => part.value).join('') === format.format(-12345.6));
console.log(new Intl.NumberFormat('en', {useGrouping:false}).format(9876543210) === '9876543210');
console.log(new Intl.NumberFormat('en', {minimumIntegerDigits:4, minimumFractionDigits:2}).format(1.5) === '0,001.50');
console.log(new Intl.NumberFormat('en', {maximumFractionDigits:1}).format(2.55) === '2.6');
console.log(new Intl.NumberFormat('en', {maximumFractionDigits:0}).format(999.9) === '1,000');
console.log(new Intl.NumberFormat('en', {maximumFractionDigits:9}).format(1e-8) === '0.00000001');
console.log(new Intl.NumberFormat('en', {useGrouping:false}).format(1e21) === '1000000000000000000000');
console.log(format.format(-0) === '-0' && format.format(NaN) === 'NaN' && format.format(-Infinity) === '-∞');
const strip = new Intl.NumberFormat('en', {minimumFractionDigits:2, trailingZeroDisplay:'stripIfInteger'});
console.log(strip.format(1) === '1' && strip.format(1.5) === '1.50');
const options = strip.resolvedOptions();
console.log(options.locale === 'en' && options.style === 'decimal' && options.minimumFractionDigits === 2 && options.maximumFractionDigits === 3 && options.trailingZeroDisplay === 'stripIfInteger');
options.minimumFractionDigits = 9;
console.log(strip.format(1.5) === '1.50');
console.log(rejects(() => format.formatToParts.call({}, 1), TypeError) && rejects(() => format.resolvedOptions.call({}, 1), TypeError));
console.log(rejects(() => new Intl.NumberFormat('en', {minimumFractionDigits:5,maximumFractionDigits:1}), RangeError));
const token = {};
try { new Intl.Locale('en', {get region(){throw token;}}); console.log(false); } catch (error) { console.log(error === token); }
class ChildNumber extends Intl.NumberFormat {}
console.log(new ChildNumber('en').format(1234) === '1,234');
