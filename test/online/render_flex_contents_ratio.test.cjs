const {test} = require('node:test');
const assert = require('node:assert/strict');
const {renderFixtures, rgb} = require('./render_fixture.cjs');
const render = renderFixtures('flex-contents-ratio');

for (const depth of [0, 1, 2]) {
    test('a ratio flex item retains its assigned height through ' + depth + ' contents wrappers', async () => {
        let item = '<div class="ratio"><div class="inside"></div></div>';
        for (let index = 0; index < depth; index++)
            item = '<div style="display:contents">' + item + '</div>';
        const html = '<!doctype html><style>body{margin:0}.row{display:flex;align-items:center;width:400px;height:min-content}' +
            '.ratio{aspect-ratio:2;flex:none;width:100%;height:auto;display:flex;position:relative}' +
            '.inside{position:absolute;inset:0;background:red}.following{width:400px;height:10px;background:cyan}' +
            '</style><div class="row">' + item + '</div><div class="following"></div>';
        const {image} = await render('ratio-contents-' + depth, html);
        assert.deepEqual(rgb(image, 20, 20), [255, 0, 0], 'the positioned content paints');
        assert.deepEqual(rgb(image, 20, 190), [255, 0, 0], 'the ratio keeps the full 200px height');
        assert.deepEqual(rgb(image, 20, 205), [0, 255, 255], 'following flow retains its position');
    });
}
