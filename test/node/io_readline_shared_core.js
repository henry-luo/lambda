const EventEmitter = require('events').EventEmitter;
const readline = require('readline');
function check(actual, expected, label) {
  if (JSON.stringify(actual) !== JSON.stringify(expected)) {
    throw new Error(label + ': ' + JSON.stringify(actual));
  }
}

const input = new EventEmitter();
const writes = [];
const output = { write(bytes) { writes.push(bytes); return true; } };
const lines = [];
let closes = 0;
const rl = readline.createInterface({ input, output, terminal: false, prompt: 'λ> ' });
check(rl instanceof readline.Interface, true, 'interface prototype');
rl.on('line', line => lines.push(line));
rl.on('close', () => closes++);
check(rl.line, '', 'initial line');
check(rl.cursor, 0, 'initial cursor');
rl.prompt();
check(writes, ['λ> '], 'prompt');
input.emit('data', 'abc\r');
input.emit('data', '\nx\ny\n');
check(lines, ['abc', 'x', 'y'], 'submitted lines');
check(rl.line, '', 'line after submit');
let answer = null;
rl.question('Q? ', value => { answer = value; });
check(writes, ['λ> ', 'Q? '], 'question prompt');
input.emit('data', 'ok\n');
check(answer, 'ok', 'question answer');
check(lines, ['abc', 'x', 'y'], 'question does not emit line');
rl.write('😊');
check(rl.cursor, 2, 'utf16 cursor');
check(rl.getCursorPos(), { cols: 5, rows: 0 }, 'script cursor layout');
rl.write('\x7f');
check(rl.line, '', 'unicode backspace');
rl.write('z');
check(rl.line, 'z', 'pending line');
check(rl.cursor, 1, 'pending cursor');
input.emit('end');
check(lines, ['abc', 'x', 'y', 'z'], 'last line');
check(closes, 1, 'end closes interface');
input.emit('data', 'ignored\n');
check(lines, ['abc', 'x', 'y', 'z'], 'input detached after close');
if (rl.close() !== rl) throw new Error('close returns interface');
check(closes, 1, 'close is idempotent');

const nested = [];
const reentrant = readline.createInterface({ input: new EventEmitter(),
  terminal: false });
reentrant.on('line', line => {
  nested.push(line);
  if (line === 'first') reentrant.write('second\n');
});
reentrant.write('first\n');
reentrant.write(Buffer.from('buffer\n'));
check(nested, ['first', 'second', 'buffer'], 'reentrant and Buffer input');
reentrant.close();

const keyed = readline.createInterface({ input: new EventEmitter(),
  terminal: false });
keyed.write('abc');
keyed.write(null, { name: 'left' });
keyed.write('X');
check(keyed.line, 'abXc', 'key-object cursor edit');
keyed.write(null, { name: 'a', ctrl: true });
check(keyed.cursor, 0, 'key-object control home');
keyed.write(null, { name: 'delete' });
check(keyed.line, 'bXc', 'key-object delete');
keyed.write(null, { sequence: '\x1f' });
check(keyed.line, 'abXc', 'key-object undo sequence');
keyed.write(null, { name: 'return' });
check(keyed.line, '', 'key-object submit');
keyed.close();

const ttyWrites = [];
const virtualTty = readline.createInterface({ input: new EventEmitter(),
  output: { isTTY: true, write(bytes) { ttyWrites.push(bytes); return true; } } });
check(virtualTty.terminal, true, 'terminal defaults from output');
virtualTty.prompt();
if (ttyWrites.length !== 1 || ttyWrites[0].length === 0) {
  throw new Error('terminal prompt bytes');
}
virtualTty.close();

const narrow = readline.createInterface({ input: new EventEmitter(),
  output: { columns: 4, isTTY: true, write() { return true; } },
  terminal: true, prompt: '> ' });
narrow.write('abc');
check(narrow.getCursorPos(), { cols: 1, rows: 1 }, 'output width reaches Lambda layout');
narrow.close();

const positionalInput = new EventEmitter();
const positionalLines = [];
const positional = readline.createInterface(positionalInput);
positional.on('line', line => positionalLines.push(line));
positionalInput.emit('data', 'positional\n');
check(positionalLines, ['positional'], 'positional createInterface');
positional.close();

const history = readline.createInterface({ input: new EventEmitter(),
  terminal: true, historySize: 2, removeHistoryDuplicates: true });
history.write('a\nb\na\n');
history.write('\x1b[A');
check(history.line, 'a', 'latest history');
history.write('\x1b[A');
check(history.line, 'b', 'bounded unique history');
history.close();

const dotHistory = readline.createInterface({ input: new EventEmitter(),
  terminal: true });
dotHistory.write('.cmd\n');
dotHistory.write('\x1b[A');
check(dotHistory.line, '.cmd', 'Node history accepts dot commands');
dotHistory.close();

const flowInput = new EventEmitter();
const flowEvents = [];
flowInput.pause = () => flowEvents.push('input-pause');
flowInput.resume = () => flowEvents.push('input-resume');
const flow = readline.createInterface({ input: flowInput, terminal: false });
flow.on('pause', () => flowEvents.push('pause'));
flow.on('resume', () => flowEvents.push('resume'));
flow.pause();
flow.pause();
check(flow.paused, true, 'paused state');
flow.prompt();
check(flow.paused, false, 'prompt resumes input');
check(flowEvents, ['input-pause', 'pause', 'input-resume', 'resume'],
  'pause and resume ordering');
flow.close();

const brokenOutput = readline.createInterface({ input: new EventEmitter(),
  output: { write() { throw new Error('write failed'); } }, terminal: false });
let writeFailed = false;
try { brokenOutput.prompt(); } catch (err) { writeFailed = err.message === 'write failed'; }
check(writeFailed, true, 'output write failure propagates');
brokenOutput.close();
console.log('io_readline_shared_core: passed');
