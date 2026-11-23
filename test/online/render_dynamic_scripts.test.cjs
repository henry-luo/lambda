const {test} = require('node:test');
const assert = require('node:assert/strict');
const {renderFixtures, rgb, loggedResult, serveFixtureResources} = require('./render_fixture.cjs');
const {dynamicScriptCases, dynamicScriptBudgetCases, dynamicModuleCases, dynamicScriptExpected, dynamicScriptHtml,
    serveDynamicScript} = require('./dynamic_script_fixtures.cjs');
const render = renderFixtures('dynamic-scripts');
const requests = [];
const origin = serveFixtureResources((request, response) => {
    requests.push(new URL(request.url, 'http://fixture.invalid'));
    serveDynamicScript(request, response);
});

for (const [module, modes] of [[false, [...dynamicScriptCases, ...dynamicScriptBudgetCases]], [true, dynamicModuleCases]]) {
  for (const mode of modes) {
    test('dynamic ' + (module ? 'module' : 'classic') + ' script: ' + mode, async () => {
        const {image, log} = await render((module ? 'module-' : '') + mode, dynamicScriptHtml(mode, origin(), module), {
            ...process.env, LAMBDA_POST_LOAD_SETTLE_MS:mode === 'zero-window' ? '0' : '1000',
        });
        const expected = dynamicScriptExpected(mode, module);
        assert.deepEqual(loggedResult(log, 'DYNAMIC_SCRIPT_RESULT'), expected);
        assert.deepEqual(rgb(image, 40, 40), [0, 128, 0]);
        assert.deepEqual(rgb(image, 140, 40), expected.runs.length ? [0, 0, 255] : [255, 255, 255]);
        const selected = requests.filter(url => url.searchParams.get('case') === mode && url.searchParams.has('module') === module);
        if (mode !== 'cached') assert.equal(selected.length, ['inert', 'unsupported', 'empty-src', 'async-idl'].includes(mode) ? 0
            : mode === 'late-fetch-chain' ? 3
                : ['ordered', 'async', 'chain', 'late-chain', 'stylesheet', 'late-fetch', 'late-fetch-immediate'].includes(mode) ? 2 : 1,
        'preparation owns each request');
    });
  }
}
