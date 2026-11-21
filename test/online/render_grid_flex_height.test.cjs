const {test} = require('node:test');
const assert = require('node:assert/strict');
const {renderFixtures, loggedResult} = require('./render_fixture.cjs');
const {fixtureHtml} = require('./grid_flex_height_fixtures.cjs');
const render = renderFixtures('grid-flex-height');
for (const align of ['start', 'center', 'end']) test('grid preserves final flex content height: ' + align, async () => {
    const {log} = await render(align, fixtureHtml(align));
    const result = loggedResult(log, 'GRID_FLEX_HEIGHT');
    assert.ok(Math.abs(result.height - result.contentBottom) < 0.1, JSON.stringify(result));
});
