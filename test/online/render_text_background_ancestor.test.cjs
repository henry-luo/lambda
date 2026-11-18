const {test} = require('node:test');
const assert = require('node:assert/strict');
const {renderFixtures, rgb} = require('./render_fixture.cjs');
const {fixtures, fixtureHtml, greenInk, inkByChannel} = require('./text_background_ancestor_fixtures.cjs');
const render = renderFixtures('text-background-ancestor');

for (const [index, fixture] of fixtures.entries()) {
    test('text-clipped ancestor background: ' + fixture.name, async () => {
        const {image} = await render('text-background-' + index, fixtureHtml(fixture));
        const ink = greenInk(image);
        assert.ok(ink > 500, `glyphs must remain visible: ${ink} green pixels`);
        assert.ok(ink < 10000, `the background must stay clipped to glyphs: ${ink} green pixels`);
        if (fixture.noRed) assert.equal(inkByChannel(image, 0), 0, 'child glyphs use the owner gradient coordinates');
        assert.deepEqual(rgb(image, 25, 25), [255, 255, 255], 'padding stays transparent');
    });
}
