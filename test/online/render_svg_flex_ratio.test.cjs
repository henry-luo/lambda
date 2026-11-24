const {test} = require('node:test');
const assert = require('node:assert/strict');
const {renderFixtures, rgb, loggedResult} = require('./render_fixture.cjs');
const {svgFlexRatioCases, svgFlexRatioHtml} = require('./svg_flex_ratio_fixtures.cjs');
const render = renderFixtures('svg-flex-ratio');

for (const fixture of svgFlexRatioCases) test('SVG flex ratio: ' + fixture.name, async () => {
    const {image, log} = await render(fixture.name, svgFlexRatioHtml(fixture));
    const result = loggedResult(log, 'SVG_FLEX_RATIO_RESULT');
    assert.ok(Math.abs(result.width-fixture.width)<0.02, JSON.stringify(result));
    assert.ok(Math.abs(result.height-fixture.usedHeight)<0.02, JSON.stringify(result));
    assert.deepEqual(rgb(image, fixture.column?10:Math.ceil(fixture.width+12), fixture.column?28:10), [0,0,255]);
});
