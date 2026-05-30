// Chalk 5.3.0 Library Support Tests — Js52 P5
// Tests prototype-chain getters, ANSI escape generation, tagged templates.
// Uses native ESM `import chalk from './chalk_src.js'` after Js52 P1 fixed
// aliased export specifiers (`export { J as default }` now resolves).

import chalk from './chalk_src.js';

// Force a color level so ANSI codes are emitted regardless of TTY detection.
chalk.level = 3;

// === Test 1: chalk loaded as function/object ===
console.log(typeof chalk);
console.log(typeof chalk.red);
console.log(typeof chalk.bold);

// === Test 2: Basic single-style ANSI escapes ===
const red = chalk.red('hello');
console.log(red.startsWith('\x1b[31m'));
console.log(red.endsWith('\x1b[39m'));
console.log(red.indexOf('hello') > 0);

// === Test 3: Chained style (prototype-chain getters) ===
const boldRed = chalk.red.bold('boom');
console.log(typeof boldRed);
console.log(boldRed.indexOf('boom') >= 0);
console.log(boldRed.indexOf('\x1b[1m') >= 0);
console.log(boldRed.indexOf('\x1b[31m') >= 0);

// === Test 4: Triple chain ===
const triple = chalk.bold.underline.green('triple');
console.log(triple.indexOf('triple') >= 0);
console.log(triple.indexOf('\x1b[4m') >= 0);
console.log(triple.indexOf('\x1b[32m') >= 0);

// === Test 5: Background colors ===
const bg = chalk.bgBlue.white('bg');
console.log(bg.indexOf('\x1b[44m') >= 0);
console.log(bg.indexOf('\x1b[37m') >= 0);

// === Test 6: Variadic args joined with spaces ===
const multi = chalk.red('a', 'b', 'c');
console.log(multi.indexOf('a b c') >= 0);

// === Test 7: chalk.level ===
console.log(typeof chalk.level === 'number');
console.log(chalk.level >= 0 && chalk.level <= 3);

// === Test 8: visible/reset/dim modifiers ===
console.log(typeof chalk.visible);
console.log(typeof chalk.reset);
console.log(typeof chalk.dim);
console.log(typeof chalk.italic);
console.log(typeof chalk.strikethrough);

// === Test 9: All basic colors exist ===
const colors = ['black','red','green','yellow','blue','magenta','cyan','white'];
let allOk = true;
for (const c of colors) {
    if (typeof chalk[c] !== 'function') { allOk = false; break; }
}
console.log(allOk);

// === Test 10: Number coerces to string ===
const n = chalk.green(42);
console.log(n.indexOf('42') >= 0);

// === Test 11: Empty string wrapped with codes ===
const empty = chalk.red('');
console.log(empty.startsWith('\x1b[31m'));

console.log("CHALK_DONE");
