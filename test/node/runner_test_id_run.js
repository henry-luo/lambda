const assert = require('node:assert');
const { describe, it, run } = require('node:test');

if (globalThis.__lambdaRunnerTestIdFixture) {
  function makeSubtest(shouldFail) {
    return async function(t) {
      await t.test('e2e', async () => {
        if (shouldFail) assert.fail('intentional');
      });
    };
  }

  describe('suite', { concurrency: 10000 }, () => {
    it('test-A', makeSubtest(false));
    it('test-B', makeSubtest(false));
    it('test-C', makeSubtest(true));
    it('test-D', makeSubtest(false));
  });
} else {
  globalThis.__lambdaRunnerTestIdFixture = true;

  async function main() {
    const events = [];
    for await (const event of run({
      files: ['test/node/runner_test_id_run.js'],
      isolation: 'none',
    })) {
      events.push(event);
    }

    const failEvent = events.find((event) =>
      event.type === 'test:fail' && event.data.name === 'e2e');
    assert.ok(failEvent);

    const matchingStart = events.find((event) =>
      event.type === 'test:start' && event.data.testId === failEvent.data.testId);
    assert.strictEqual(matchingStart.data.name, 'e2e');

    const e2eStarts = events.filter((event) =>
      event.type === 'test:start' && event.data.name === 'e2e');
    assert.strictEqual(e2eStarts.length, 4);
    assert.strictEqual(new Set(e2eStarts.map((event) => event.data.testId)).size, 4);

    console.log('runner test id run ok');
  }

  main().catch((err) => {
    console.error(err);
    process.exit(1);
  });
}
