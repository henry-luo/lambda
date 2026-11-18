const {test} = require('node:test');
const assert = require('node:assert/strict');
const {renderFixtures} = require('./render_fixture.cjs');

const render = renderFixtures('balance-atomic');
const atom = '<span class="atom"><div></div></span>';
const sentence = 'One two three four five six seven eight ';
const fixtures = [
    {name: 'balanced paragraph measures a block-containing inline atom',
        content: sentence + atom + ' nine ten eleven twelve thirteen fourteen.'},
    {name: 'balanced paragraph measures repeated inline atoms',
        content: sentence + atom + ' nine ten ' + atom + ' eleven twelve thirteen fourteen.'},
    {name: 'nested balanced paragraphs keep independent measurement owners',
        content: sentence + '<span class="atom"><div class="balance nested">Small words ' + atom +
            ' more words that wrap inside.</div></span> nine ten eleven twelve thirteen fourteen.'},
    {name: 'ordinary balanced text still wraps without atomic descendants',
        content: sentence + ' nine ten eleven twelve thirteen fourteen.', plain: true},
];

for (const fixture of fixtures) {
    test(fixture.name, async () => {
        const source = `<!doctype html><style>body{margin:0;background:white}
            .balance{width:400px;font:20px/30px Arial;text-wrap:balance}
            .atom{display:inline-block;background:lime}.atom>div:not(.nested){width:40px;height:20px}
            .nested{width:120px;font-size:16px}
            </style><div class="balance">${fixture.content}</div>`;
        const {image} = await render(fixture.name.replaceAll(' ', '-'), source);
        let ink = 0, atomPixels = 0;
        for (let y = 0; y < 300; y++) for (let x = 0; x < 450; x++) {
            const offset = (y * image.width + x) * 4;
            const [r, g, b] = image.data.subarray(offset, offset + 3);
            if (r < 100 && g < 100 && b < 100) ink++;
            if (r === 0 && g === 255 && b === 0) atomPixels++;
        }
        assert(ink > 400, 'paragraph text must survive the measurement');
        if (!fixture.plain) assert(atomPixels >= 800, 'the inline atom must remain visible');
    });
}
