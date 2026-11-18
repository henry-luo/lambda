const {test} = require('node:test');
const assert = require('node:assert/strict');
const {renderFixtures, rgb} = require('./render_fixture.cjs');
const render = renderFixtures('svg-comment');

const svg = '<svg xmlns="http://www.w3.org/2000/svg" width="100" height="100"><rect width="100" height="100" fill="#0055ee"/></svg>';
const fixtures = [
    ['ordinary', svg, [0, 85, 238]],
    ['xml-declaration', '<?xml version="1.0"?>' + svg, [0, 85, 238]],
    ['leading-comment', '<!-- Copyright © 2025 Example -->\n' + svg, [0, 85, 238]],
    ['bom-and-comments', '\ufeff \n<!-- <svg> is comment text -->\n<!-- second -->\t' + svg, [0, 85, 238]],
    ['unclosed-comment', '<!-- ' + svg, [255, 255, 255]],
    ['comment-only', '<!-- ' + svg + ' -->', [255, 255, 255]],
];

for (const [name, source, expected] of fixtures) {
    test('SVG image content detection: ' + name, async () => {
        const url = 'data:image/svg+xml;base64,' + Buffer.from(source).toString('base64');
        const {image} = await render(name, `<!doctype html><style>body{margin:0;background:white}img{width:100px;height:100px}</style><img src="${url}">`);
        assert.deepEqual(rgb(image, 50, 50), expected);
    });
}
