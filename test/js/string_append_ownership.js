function builder() {
    let text = '';
    return {
        append(piece) { text += piece; },
        appendFrom(run) { text += run(); },
        *pause() { text += yield 0; },
        read() { return text; },
        evalRead() { return eval('text'); },
        expression(piece) { return text += piece; }
    };
}
function seeded() {
    const b = builder();
    b.append('a');
    b.append('b');
    return b;
}
function check(value, label) {
    if (!value) throw new Error(label);
    console.log(true);
}

const large = builder();
const piece = 'é😀x'.repeat(8);
for (let i = 0; i < 256; i++) large.append(piece);
const earlier = large.read();
for (let i = 0; i < 256; i++) large.append(piece);
check(earlier.length === 8192 && large.read().length === 16384 &&
    earlier.slice(-4) === 'é😀x', 'Unicode alias');

const b = seeded(), saved = [b.read()];
b.append('c');
const returned = b.expression('d');
b.append('e');
check(saved[0] === 'ab' && returned === 'abcd' && b.read() === 'abcde', 'published aliases');

const nested = seeded();
nested.appendFrom(() => { nested.append('!'); return 'X'; });
check(nested.read() === 'abX', 'RHS reentrant append');

const retained = seeded();
let nestedValue;
retained.appendFrom(() => { retained.append('!'); nestedValue = retained.read(); return 'X'; });
check(nestedValue === 'ab!' && retained.read() === 'abX', 'nested alias');

const readDuringRhs = seeded();
let rhsAlias;
readDuringRhs.appendFrom(() => { rhsAlias = readDuringRhs.read(); return 'X'; });
check(rhsAlias === 'ab' && readDuringRhs.read() === 'abX', 'RHS alias');

const throwing = seeded();
try { throwing.appendFrom(() => { throwing.append('!'); throw new Error('stop'); }); }
catch (error) { if (error.message !== 'stop') throw error; }
throwing.append('?');
check(throwing.read() === 'ab!?', 'borrow cleanup after throw');

const coercing = seeded();
coercing.append({ toString() { coercing.append('!'); return 'X'; } });
check(coercing.read() === 'abX', 'coercion reentrant append');

const parked = seeded(), first = parked.pause(), second = parked.pause();
first.next();
second.next();
first.next('X');
const firstResult = parked.read();
second.next('Y');
check(firstResult === 'abX' && parked.read() === 'abY', 'out-of-order parked appends');

const saturated = seeded(), pending = [];
for (let i = 0; i < 40; i++) {
    const pause = saturated.pause();
    pause.next();
    pending.push(pause);
}
let saturationCorrect = true;
for (let i = 0; i < 40; i++) {
    pending[i].next(String(i));
    if (saturated.read() !== 'ab' + i) saturationCorrect = false;
}
check(saturationCorrect, 'borrow saturation');

const evaluated = seeded(), evalAlias = evaluated.evalRead();
evaluated.append('c');
check(evalAlias === 'ab' && evaluated.read() === 'abc', 'direct eval alias');

function completion() {
    let text = '';
    text += 'a';
    const result = eval('text += "b"');
    text += 'c';
    return result === 'ab' && text === 'abc';
}
check(completion(), 'eval completion');

function mapped(text) {
    const previous = arguments[0];
    text += 'b';
    return previous === 'a' && arguments[0] === 'ab';
}
check(mapped('a'), 'mapped parameter');

let effect = 0, refused = false;
function constant() {
    const text = 'a';
    try { text += (++effect, 'b'); } catch (error) { refused = error instanceof TypeError; }
    return text;
}
check(constant() === 'a' && effect === 1 && refused, 'const refusal');

const holder = { text: 'a' }, propertyAlias = holder.text;
holder.text += 'b';
check(propertyAlias === 'a' && holder.text === 'ab', 'property assignment');

function continuation() {
    let text = '', previous = '';
    for (let i = 0; i < 12000; i++) {
        if (i === 100) previous = text;
        text += 'x';
    }
    return previous.length === 100 && text.length === 12000;
}
check(continuation(), 'loop continuation alias');

console.log('STRING_APPEND_DONE');
