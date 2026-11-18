const {pngLibrary} = require('./compare_snapshots.cjs');
const PNG = pngLibrary(require('node:path').resolve(__dirname, '../..'));
const image = new PNG({width: 200, height: 1024});
image.data.fill(255);
const source = 'data:image/png;base64,' + PNG.sync.write(image).toString('base64');

const fixtures = [
    {name: 'ratio_inline_image', width: 112, ratio: '16/9', expected: 95},
    {name: 'ratio_narrow_image', width: 80, ratio: '4/3', expected: 92},
    {name: 'fixed_height_control', width: 112, ratio: '16/9', height: 63, expected: 95},
    {name: 'block_image_control', width: 112, ratio: '16/9', display: 'block', expected: 95},
    {name: 'automatic_height_control', width: 112, expected: 605.44},
    {name: 'ratio_content_growth_control', width: 112, ratio: '16/9', textBlock: true, expected: 432},
];

function fixtureHtml(fixture) {
    const cover = `<div class="cover">${fixture.textBlock
        ? '<span style="display:block;height:400px">content</span>' : `<img src="${source}">`}</div>`;
    return `<!doctype html><style>body{margin:0}
.outer{display:flex;width:558px;overflow:hidden;align-items:flex-start}
.strip{display:inline-flex;flex:1;align-items:center;padding:16px 0;gap:16px}
.cover{flex:none;width:${fixture.width}px;position:relative;
${fixture.ratio ? `aspect-ratio:${fixture.ratio};` : ''}${fixture.height ? `height:${fixture.height}px;` : ''}}
.cover img{width:100%;height:100%;object-fit:contain;vertical-align:middle;display:${fixture.display || 'inline-block'}}
</style><div class="outer"><div class="strip">${cover.repeat(3)}</div></div>
<script>console.log('INTRINSIC_IMAGE_HEIGHT',document.querySelector('.outer').getBoundingClientRect().height)</script>`;
}

module.exports = {fixtures, fixtureHtml};
