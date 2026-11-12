const {test} = require('node:test');
const assert = require('node:assert/strict');
const {renderFixtures, rgb} = require('./render_fixture.cjs');
const {fixtures, fixtureHtml} = require('./rem_fixtures.cjs');

const render = renderFixtures('rem');
for (const [index, fixture] of fixtures.entries()) {
    test(fixture.name, async () => {
        const {image, log} = await render('rem-' + index, fixtureHtml(fixture));
        const scale = fixture.zoom || 1;
        const root = fixture.font * scale;
        const width = root * 10;
        assert(log.includes('REM_GEOMETRY ' + width + ' ' +
            ((fixture.font * 2 + 10) * scale) + ' ' + root * 4), log.slice(-2000));
        assert.deepEqual(rgb(image, root + width - 2, root + 2), [255, 0, 0]);
        assert.deepEqual(rgb(image, root + width + 2, root + 2), [255, 255, 255]);
        assert.deepEqual(rgb(image, root + 2, root * 3 + 2), [0, 0, 255]);
    });
}
