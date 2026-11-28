const {test} = require('node:test');
const assert = require('node:assert/strict');
const {loggedResult, renderFixtures} = require('./render_fixture.cjs');
const {fixtures, fixtureHtml} = require('./intrinsic_image_basis_fixtures.cjs');
const render = renderFixtures('intrinsic-image-basis');

for (const fixture of fixtures) {
    test('intrinsic inline flow retains its own sizing basis: ' + fixture.name, async () => {
        const {log} = await render(fixture.name, fixtureHtml(fixture));
        const match = /INTRINSIC_IMAGE_HEIGHT\s+([\d.]+)/.exec(log);
        assert.ok(match, log);
        assert.ok(Math.abs(Number(match[1]) - fixture.expected) < 0.1,
            `${fixture.name}: got ${match[1]}, expected ${fixture.expected}`);
    });
}

for (const viewportRatio of ['auto', 'inherit']) {
    test('percentage SVG inside a ratio-sized flex item with viewport ratio ' + viewportRatio, async () => {
        const {log} = await render('ratio-flex-svg-' + viewportRatio, `<!doctype html>
            <style>
                .row { display: flex; align-items: center; height: min-content; }
                .icon { flex: none; width: 24px; aspect-ratio: 1; }
                .viewport { width: 100%; height: 100%; aspect-ratio: ${viewportRatio}; }
                svg { display: block; width: 100%; height: 100%; }
            </style>
            <div class="row"><div class="icon"><div class="viewport">
                <svg><circle cx="8" cy="8" r="7"/></svg>
            </div></div></div>
            <script>
                const sizes = {};
                for (const selector of ['.row', '.icon', '.viewport', 'svg']) {
                    sizes[selector] = document.querySelector(selector).getBoundingClientRect().height;
                }
                console.log('RATIO SVG ' + JSON.stringify(sizes));
            </script>`);
        assert.deepEqual(loggedResult(log, 'RATIO SVG'), {'.row': 24, '.icon': 24, '.viewport': 24, svg: 24});
    });
}
