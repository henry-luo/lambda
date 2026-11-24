const {test} = require('node:test');
const assert = require('node:assert/strict');
const {renderFixtures, loggedResult, rgb} = require('./render_fixture.cjs');
const {intersectionEntryCases, intersectionEntryHtml} = require('./intersection_entry_fixtures.cjs');
const render = renderFixtures('intersection-entry');

for (const fixture of intersectionEntryCases) {
    test('intersection entry: ' + fixture.name, async () => {
        const {image, log} = await render(fixture.name, intersectionEntryHtml(fixture));
        assert.deepEqual(loggedResult(log, 'INTERSECTION_ENTRY_RESULT'), fixture.expected);
        assert.deepEqual(rgb(image, 40, 40), [0, 255, 0]);
    });
}
