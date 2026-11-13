const {test} = require('node:test');
const assert = require('node:assert/strict');
const {renderFixtures, rgb} = require('./render_fixture.cjs');
const {fixtures, fixtureHtml} = require('./opacity_positioned_fixtures.cjs');
const render = renderFixtures('opacity-positioned');
for (const [index, fixture] of fixtures.entries()) {
    test(fixture.name, async () => {
        const {image} = await render('opacity-positioned-' + index, fixtureHtml(fixture));
        const pixel = rgb(image, 30, fixture.sampleY || 30);
        for (let channel = 0; channel < 3; channel++)
            assert.ok(Math.abs(pixel[channel] - fixture.expected[channel]) <= 1,
                'group/clip pixel ' + pixel + ' expected ' + fixture.expected);
        assert.deepEqual(rgb(image, 5, 5), [255,255,255], 'uncovered backdrop stays white');
    });
}
