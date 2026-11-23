const {pngLibrary} = require('./compare_snapshots.cjs');
const PNG = pngLibrary(require('node:path').resolve(__dirname, '../..'));
const pixels = new PNG({width: 4, height: 3});
for (let i = 0; i < pixels.data.length; i += 4) {
    pixels.data.set([255, 0, 0, 255], i);
}
const raster = 'data:image/png;base64,' + PNG.sync.write(pixels).toString('base64');
const vector = 'data:image/svg+xml,' + encodeURIComponent(
    '<svg xmlns="http://www.w3.org/2000/svg" width="4" height="3"><rect width="4" height="3" fill="red"/></svg>');

const fixtures = [
    {name: 'explicit-raster'},
    {name: 'explicit-vector', source: vector},
    {name: 'percentage-height-ratio', ratio: true},
    {name: 'clipped-percentage-height-ratio', ratio: true, clip: true},
    {name: 'nested-block-control', nested: true},
    {name: 'flex-control', flex: true},
];

function fixtureHtml(fixture) {
    const image = `<img class="image" src="${fixture.source || raster}" width="80" height="60">`;
    return `<!doctype html><style>body{margin:0;background:magenta}
.stage{display:${fixture.flex ? 'flex' : 'grid'};width:160px;height:60px;
grid-template-columns:80px 80px;${fixture.clip ? 'overflow:clip' : ''}}
.sibling{width:80px;height:60px;background:lime}
.image{object-fit:contain;${fixture.ratio ? 'aspect-ratio:4/3;height:100%' : 'width:80px;height:60px'}}
</style><div class="stage"><div class="sibling"></div>${fixture.nested ? `<div>${image}</div>` : image}</div>`;
}

module.exports = {fixtures, fixtureHtml};
