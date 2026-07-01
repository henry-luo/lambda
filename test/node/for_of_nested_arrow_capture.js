const assert = require('assert');

const starts = [{ id: 1 }, { id: 2 }, { id: 3 }];
const completes = [{ id: 1 }, { id: 2 }, { id: 3 }];
const seen = [];

for (const complete of completes) {
  const match = starts.find((start) => start.id === complete.id);
  seen.push(match.id);
}

assert.deepStrictEqual(seen, [1, 2, 3]);

async function runAsync() {
  await Promise.resolve();
  const asyncSeen = [];
  for (const complete of completes) {
    const match = starts.find((start) => start.id === complete.id);
    asyncSeen.push(match.id);
  }
  assert.deepStrictEqual(asyncSeen, [1, 2, 3]);
}

async function main() {
  await runAsync();
  console.log('for-of nested arrow capture ok');
}

main().catch((err) => {
  console.error(err);
  process.exit(1);
});
