const {test} = require('node:test');
const assert = require('node:assert/strict');
const {renderFixtures, rgb, loggedResult, serveFixtureResources} = require('./render_fixture.cjs');
const {scriptPreparationCases, scriptPreparationExpected, scriptPreparationHtml,
    servePreparationScript} = require('./script_preparation_fixtures.cjs');
const render = renderFixtures('script-preparation');
const origin = serveFixtureResources(servePreparationScript);

for (const mode of scriptPreparationCases) {
    test('parser script preparation: ' + mode, async () => {
        const {image, log} = await render(mode, scriptPreparationHtml(mode, origin()));
        assert.deepEqual(loggedResult(log, 'SCRIPT_PREPARATION_RESULT'), scriptPreparationExpected(mode));
        assert.deepEqual(rgb(image, 10, 10), [0, 128, 0]);
    });
}
