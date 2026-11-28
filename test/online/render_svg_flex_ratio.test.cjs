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

test('SVG flex wrapper measures auto height from the definite viewport width', async () => {
    const source = `<!doctype html><style>
    * {box-sizing:border-box}
    body {margin:0}
    .grid {display:grid; grid-template:auto auto / 1fr 1.5fr; gap:20px; width:1120px}
    .col {display:flex; flex-flow:column; width:100%; height:100%; justify-content:space-between}
    .artwork {display:flex; flex:1; flex-flow:column; width:100%; height:100%; align-items:center;
        justify-content:center; overflow:hidden; padding:42px; background:lightblue}
    .viewport {width:100%; max-height:560px; background:red}
    svg {display:block; width:100%; height:100%}
    </style>
    <div class="grid">
      <div class="col"><div style="height:300px"></div></div>
      <div class="col"><div class="artwork"><div class="viewport">
        <svg viewBox="0 0 777 358" width="777" height="358"><rect width="777" height="358" fill="green"/></svg>
      </div></div></div>
    </div>
    <script>
    const view = document.querySelector('.viewport').getBoundingClientRect();
    const svg = document.querySelector('svg').getBoundingClientRect();
    console.log('SVG_FLEX_WRAPPER '+JSON.stringify({viewWidth:view.width,viewHeight:view.height,
        svgWidth:svg.width,svgHeight:svg.height}));
    </script>`;
    const {image,log} = await render('definite-inline-size', source);
    const sizes = loggedResult(log, 'SVG_FLEX_WRAPPER');
    assert.equal(sizes.viewWidth, 576);
    assert.equal(sizes.svgWidth, sizes.viewWidth);
    assert.ok(Math.abs(sizes.svgHeight - 576 * 358 / 777) < 0.02, JSON.stringify(sizes));
    assert.ok(Math.abs(sizes.viewHeight - sizes.svgHeight) < 0.02, JSON.stringify(sizes));
    assert.deepEqual(rgb(image, 600, 350), [255, 255, 255]);
});
