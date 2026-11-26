const ListFormat = Intl.ListFormat;

function show(value) {
    console.log(JSON.stringify(value));
}

function errorName(run) {
    try { run(); }
    catch (error) { return error.name; }
    return 'none';
}

const names = ['Alpha', 'Beta', 'Gamma'];
const english = new ListFormat('en-US');
show([english.format(), english.format([]), english.format(['Alpha']),
    english.format(['Alpha', 'Beta']), english.format(names),
    english.format(['Alpha', 'Beta', 'Gamma', 'Delta'])]);
show([new ListFormat('en-GB').format(names), new ListFormat('de-DE').format(names),
    new ListFormat('fr-FR').format(names), new ListFormat('ja').format(names),
    new ListFormat('ar-001').format(names), new ListFormat('ti-ER').format(names)]);
show([new ListFormat('en-US', {type: 'disjunction'}).format(names),
    new ListFormat('en-US', {style: 'short'}).format(names),
    new ListFormat('en-US', {type: 'unit', style: 'narrow'}).format(names)]);
show(english.formatToParts(names));
show(new ListFormat('mi', {type: 'disjunction'}).formatToParts(['A', 'B', 'C']));
show(english.formatToParts(['', 'B']));
show(english.format('A😀B'));
show(english.format(['A\0B', 'C']));

const options = {type: 'disjunction', style: 'narrow', localeMatcher: 'lookup'};
show(new ListFormat(['zz-ZZ', 'en-gb-u-ca-gregory', 'fr'], options).resolvedOptions());
show(ListFormat.supportedLocalesOf(['zz-ZZ', 'DE-de', 'en-GB', 'de-DE',
    'en-GB-u-nu-latn'], 1));
show(new ListFormat(new Intl.Locale('fr-FR')).resolvedOptions());

const order = [];
new ListFormat('en-US', {
    get localeMatcher() { order.push('localeMatcher'); return 'lookup'; },
    get type() {
        order.push('type');
        return {toString() { order.push('coerce'); return 'unit'; }};
    },
    get style() { order.push('style'); return 'narrow'; },
});
show(order);

let closed = 0;
const invalid = {
    [Symbol.iterator]() {
        return {
            next() { return {value: 42, done: false}; },
            return() { closed++; throw new SyntaxError('close'); },
        };
    },
};
show([errorName(() => english.format(invalid)), closed]);
const failing = {
    [Symbol.iterator]() {
        return {
            next() { throw new RangeError('next'); },
            return() { closed++; return {}; },
        };
    },
};
show([errorName(() => english.formatToParts(failing)), closed]);
show(english.format(new Set(['X', 'Y', 'Z'])));

show([errorName(() => ListFormat('en')), errorName(() => new ListFormat('en', 1)),
    errorName(() => new ListFormat('en', null)), errorName(() => new ListFormat('en', {type: 'other'})),
    errorName(() => new ListFormat('en', {style: Symbol('style')})),
    errorName(() => new ListFormat('en', {localeMatcher: 'fallback'})),
    errorName(() => ListFormat.supportedLocalesOf('en', null)),
    errorName(() => english.formatToParts(null)), errorName(() => english.format([Object('value')])),
    errorName(() => english.format.call(Object.create(ListFormat.prototype), names))]);
class Derived extends ListFormat {}
const derived = new Derived('en-US');
show([derived instanceof Derived, derived.format(['X', 'Y']),
    Object.prototype.toString.call(derived), ListFormat.length, derived.format.length,
    Object.keys(Intl).includes('ListFormat'), Object.keys(ListFormat.prototype)]);

const spanish = new ListFormat('es-ES');
const spanishOr = new ListFormat('es-ES', {type: 'disjunction'});
show([spanish.format(['cobre', 'hierro']), spanish.format(['miel', 'higos']),
    spanish.format(['té', 'café', 'Infusión']), spanishOr.format(['7', '8 cajas']),
    spanishOr.format(['10', '11']), spanishOr.format(['perro', 'hormiga']),
    spanishOr.format(['10', '110'])]);
const hebrew = new ListFormat('he');
show([hebrew.format(['א', 'ב']), hebrew.format(['ירושלים', 'London']),
    hebrew.formatToParts(['א', 'A'])]);
