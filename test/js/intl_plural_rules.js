function check(locale, options, values) {
    const rules = new Intl.PluralRules(locale, options);
    const state = rules.resolvedOptions();
    console.log([state.locale, state.type, state.pluralCategories.join('/'),
        values.map(value => rules.select(value)).join('/')].join(' '));
}

check('en-US', {}, [1, 0, 2, -1, 1.0001, 1.2, NaN, Infinity]);
check('en', {type: 'ordinal'}, [1, 2, 3, 4, 11, 12, 13, 21, 102]);
check(new Intl.Locale('ru'), {}, [1, 2, 5, 11, 21, 22, 1.2, 1e100]);
check('ar', {}, [0, 1, 2, 3, 11, 100, 2.5]);
check('pt-PT', {}, [0, 1, 2, 1000000]);
check('sk', {}, [1, 2, 5, 1.2]);
check('en', {minimumFractionDigits: 1}, [1, 1.1]);
check('en', {minimumFractionDigits: 1, trailingZeroDisplay: 'stripIfInteger'}, [1, 1.1]);
check('en', {maximumSignificantDigits: 2}, [9.99, .999, .994]);
check('en', {maximumFractionDigits: 0, roundingMode: 'ceil'}, [.9, -1.1, -0.9, 1.1]);
check('en', {minimumFractionDigits: 0, maximumFractionDigits: 0, roundingIncrement: 2}, [1, 2, -2]);

const localized = Intl.PluralRules.supportedLocalesOf({
    length: 6, 1: 'EN-US-x-ui', 2: 'xx-ZZ', 3: 'ru', 4: 'en-us-x-ui', 5: 'pt-PT'
}, {localeMatcher: 'lookup'});
console.log(localized.join('/'));
console.log(new Intl.PluralRules(['xx-ZZ', 'pl']).resolvedOptions().locale);
const range = new Intl.PluralRules('en');
console.log([range.selectRange(1, 1), range.selectRange(1, 2),
    new Intl.PluralRules('ru').selectRange(1, 3)].join('/'));

for (const action of [
    () => Intl.PluralRules('en'),
    () => new Intl.PluralRules('en', null),
    () => new Intl.PluralRules(['en', 'und-invalid!']),
    () => Intl.PluralRules.prototype.select.call({}, 1),
    () => range.select(1n),
    () => range.selectRange(undefined, 1),
    () => range.selectRange(NaN, 1),
    () => new Intl.PluralRules('en', {minimumFractionDigits: 3, maximumFractionDigits: 2}),
]) {
    try { action(); console.log('unexpected'); } catch (error) { console.log(error.name); }
}
console.log(Object.prototype.toString.call(range));
console.log(Intl.PluralRules.length, Intl.PluralRules.supportedLocalesOf.length,
    Intl.PluralRules.prototype.select.length, Intl.PluralRules.prototype.selectRange.length);
