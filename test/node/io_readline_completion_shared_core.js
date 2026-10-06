const EventEmitter = require('events').EventEmitter;
const readline = require('readline');
let rl;
const completer = line => {
  if (line === 'he') return [['hello', 'help'], 'he'];
  if (line === 'help') return [['helper'], 'help'];
  if (line === 'stale') {
    rl.write('x');
    return [['stale-complete'], 'stale'];
  }
  return [[], line];
};
rl = readline.createInterface({ input: new EventEmitter(), terminal: true,
  completer });
rl.write('he\t');
if (rl.line !== 'hel' || rl.cursor !== 3) {
  throw new Error('common prefix completion: ' + rl.line);
}
rl.write('p\t');
if (rl.line !== 'helper' || rl.cursor !== 6) {
  throw new Error('single match completion: ' + rl.line);
}
rl.write('\n');
rl.write('heX\x1b[D\t');
if (rl.line !== 'helX' || rl.cursor !== 3) {
  throw new Error('completion before suffix: ' + rl.line);
}
rl.write('\n');
rl.write('stale\t');
if (rl.line !== 'stalex') {
  throw new Error('stale reply changed newer edit: ' + rl.line);
}
rl.close();

let completeLater;
const callbackRl = readline.createInterface({ input: new EventEmitter(),
  terminal: true, completer: (line, done) => { completeLater = done; } });
callbackRl.write('fo\t');
if (callbackRl.line !== 'fo') throw new Error('callback completed early');
completeLater(null, [['food'], 'fo']);
if (callbackRl.line !== 'food') {
  throw new Error('callback completion: ' + callbackRl.line);
}
callbackRl.write('\n');
callbackRl.write('st\t');
callbackRl.write('x');
completeLater(null, [['stale'], 'st']);
if (callbackRl.line !== 'stx') {
  throw new Error('late callback changed newer edit: ' + callbackRl.line);
}
callbackRl.close();

const promiseRl = readline.createInterface({ input: new EventEmitter(),
  terminal: true, completer: line => Promise.resolve([['bar'], line]) });
promiseRl.write('ba\t');
Promise.resolve().then(() => {
  if (promiseRl.line !== 'bar') {
    throw new Error('promise completion: ' + promiseRl.line);
  }
  promiseRl.close();
  console.log('io_readline_completion_shared_core: passed');
});
