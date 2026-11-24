const {test} = require('node:test');
const assert = require('node:assert/strict');
const {renderFixtures, rgb} = require('./render_fixture.cjs');
const {stackingOverflowCases, stackingOverflowHtml} = require('./stacking_overflow_fixtures.cjs');
const render = renderFixtures('stacking-overflow');

for (const fixture of stackingOverflowCases) {
    test('positioned stacking overflow: ' + fixture.name, async () => {
        const {image} = await render(fixture.name, stackingOverflowHtml(fixture));
        const [x,y]=fixture.point||[40,720];
        assert.deepEqual(rgb(image,x,y),fixture.clipped?[255,255,255]:[0,255,0]);
    });
}
