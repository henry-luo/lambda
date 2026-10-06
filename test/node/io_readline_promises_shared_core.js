const EventEmitter = require('events').EventEmitter;
const readline = require('readline/promises');
const input = new EventEmitter();
const writes = [];
const output = { write(bytes) { writes.push(bytes); return true; } };
const rl = readline.createInterface({ input, output, terminal: false });

const answer = rl.question('P? ');
if (writes.length !== 1 || writes[0] !== 'P? ') {
  throw new Error('promise question prompt');
}
input.emit('data', 'done\n');
answer.then(value => {
  if (value !== 'done') throw new Error('promise question answer: ' + value);
  const canceled = rl.question('cancel? ');
  rl.close();
  canceled.then(() => { throw new Error('closed question resolved'); }, err => {
    if (!err || err.message !== 'readline closed') {
      throw new Error('closed question rejection');
    }
    console.log('io_readline_promises_shared_core: passed');
  });
});
