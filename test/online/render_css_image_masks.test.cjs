const {test} = require('node:test');
const assert = require('node:assert/strict');
const {renderFixtures, rgb, serveFixtureResources} = require('./render_fixture.cjs');

const render = renderFixtures('css-image-masks');
const webpStencil = Buffer.from(
    'UklGRjIAAABXRUJQVlA4TCUAAAAvJ8AEEBcw//M//wKCoudMD/gIZgImAdSIlFxY74j+BwDtSd8DAA==', 'base64');
const svg = content => `<svg xmlns="http://www.w3.org/2000/svg" width="40" height="20" viewBox="0 0 40 20">${content}</svg>`;
const stylesheet = `
    body { margin: 0; background: lime; }
    .probe { position: absolute; width: 96px; height: 72px; background: blue; }
    .probe > div { position: absolute; inset: 0; background: red; }
    .contain { left: 10px; top: 10px; mask-image: url(../images/stencil.svg);
               mask-size: contain; mask-position: center; mask-repeat: no-repeat; }
    .shorthand { left: 125px; top: 10px; mask: url(../images/stencil.svg) bottom/40px no-repeat;
                 mask-size: 100% 100%; }
    .missing { left: 240px; top: 10px; mask-image: url(../images/missing.svg); }
    .luma { left: 355px; top: 10px; mask: url(../images/luma.svg) center/100% 100% no-repeat luminance; }
    .flipped { left: 10px; top: 105px; mask: url(../images/stencil.svg) center/100% 100% no-repeat;
               transform: rotate(180deg); }
    .repeat { left: 125px; top: 105px; mask: url(../images/stencil.svg) left top/24px 24px repeat; }
    .bitmap { left: 355px; top: 105px; mask: url(../images/stencil.webp) 0 0/100% 100% no-repeat; }
    .reset { left: 10px; top: 200px; }
    .mixed { left: 125px; top: 200px; mask: url(../images/stencil.svg) 0 0/100% 100% no-repeat,
             linear-gradient(white 0 0) right/60% 100% no-repeat; mask-composite: intersect; }
    .two-urls { left: 240px; top: 200px; mask: url(../images/stencil.webp) 0 0/100% 100% no-repeat,
                url(../images/luma.svg) 0 0/100% 100% no-repeat luminance; mask-composite: exclude; }
`;
const probeClasses = ['contain', 'shorthand', 'missing', 'luma', 'flipped', 'repeat', 'bitmap'];
const files = {
    '/page': ['text/html', `<!doctype html><link rel="stylesheet" href="/css/masks.css"><body>
        ${probeClasses.map(name => `<div class="probe ${name}"><div></div></div>`).join('')}
        <div id="reset" class="probe reset" style="mask-image: url('/images/stencil.svg')"><div></div></div>
        <script>
            const reset = document.getElementById('reset');
            if (reset.offsetWidth === 96) reset.style.setProperty('mask', 'none');
        </script></body>`],
    '/layered': ['text/html', `<!doctype html><link rel="stylesheet" href="/css/masks.css"><body>
        <div class="probe mixed"><div></div></div><div class="probe two-urls"><div></div></div></body>`],
    '/css/masks.css': ['text/css', stylesheet],
    '/images/stencil.svg': ['image/svg+xml', svg('<path fill="white" d="M0 0H20V20H0Z"/>' +
        '<path fill="white" opacity=".5" d="M20 0H30V20H20Z"/>')],
    '/images/luma.svg': ['image/svg+xml', svg('<path fill="black" d="M0 0H20V20H0Z"/>' +
        '<path fill="white" d="M20 0H40V20H20Z"/>')],
    '/images/stencil.webp': ['image/webp', webpStencil],
};
const requests = new Set();
const origin = serveFixtureResources((request, response) => {
    requests.add(request.url);
    const file = Object.hasOwn(files, request.url) ? files[request.url] : null;
    if (!file) { response.writeHead(404); response.end(); return; }
    response.writeHead(200, {'Content-Type': file[0]});
    response.end(file[1]);
});

test('CSS URL masks composite the subtree with stylesheet-relative image geometry', async () => {
    const {image} = await render('image-masks', {url: origin() + '/page'});
    const red = [255, 0, 0], clear = [0, 255, 0], half = [128, 127, 0];
    const points = [
        [26, 15, clear], [26, 35, red], [71, 35, half], [100, 35, clear],
        [139, 35, red], [204, 35, clear], [255, 35, clear],
        [365, 35, clear], [440, 35, red], [19, 135, clear], [98, 135, red],
        [130, 115, red], [145, 115, clear], [154, 115, red],
        [370, 135, red], [424, 135, half], [446, 135, clear], [95, 220, red],
    ];
    for (const [x, y, expected] of points) assert.deepEqual(rgb(image, x, y), expected, `pixel ${x},${y}`);
    for (const path of ['/images/stencil.svg', '/images/missing.svg', '/images/luma.svg', '/images/stencil.webp'])
        assert(requests.has(path), path);
});

test('CSS mask compositing uses each selected URL and gradient in a layer list', async () => {
    const {image} = await render('mixed-mask-layers', {url: origin() + '/layered'});
    const red = [255, 0, 0], clear = [0, 255, 0], half = [128, 127, 0];
    assert.deepEqual([145, 168, 186, 209, 260, 300, 322].map(x => rgb(image, x, 235)),
        [clear, red, half, clear, red, [127, 128, 0], red]);
});

test('CSS gradient mask layers combine bottom to top in their own boxes', async () => {
    const operators = ['add', 'subtract', 'intersect', 'exclude'];
    const solid = 'linear-gradient(white 0 0)';
    const {image} = await render('gradient-mask-layers', `<!doctype html><style>
        body{margin:0;background:lime}
        .probe{position:absolute;width:100px;height:60px;box-sizing:border-box;background:red}
        .overlap{top:10px;mask:${solid} left / 60% 100% no-repeat,
                                 ${solid} right / 60% 100% no-repeat}
        .ring{left:10px;top:90px;padding:8px;mask:${solid} content-box,${solid};mask-composite:exclude}
        .fade{left:130px;top:90px;width:101px;mask-image:linear-gradient(to right,transparent,white)}
        .gaps{left:250px;top:90px;mask-image:none,${solid};mask-composite:intersect}
        .period{left:370px;top:90px;mask-image:${solid},none,${solid};
                mask-size:50% 100%;mask-position:left,right;mask-repeat:no-repeat;
                mask-composite:exclude,add}
        .position-owner{mask-position:left,right}
        .inherited{left:10px;top:170px;mask-image:${solid},${solid};mask-position:inherit;
                   mask-size:35% 100%;mask-repeat:no-repeat}
        .conic{left:130px;top:170px;mask-image:conic-gradient(white 0deg 180deg,transparent 180deg 360deg)}
        </style><body>${operators.map((operator, index) =>
        `<div class="probe overlap" style="left:${10 + index * 120}px;mask-composite:${operator},intersect"></div>`).join('')}
        <div class="probe ring"></div><div class="probe fade"></div>
        <div class="probe gaps"></div><div class="probe period"></div>
        <div class="position-owner"><div class="probe inherited"></div></div><div class="probe conic"></div></body>`);
    const red = [255, 0, 0], clear = [0, 255, 0];
    const expected = [[red, red, red], [red, clear, clear], [clear, red, clear], [red, clear, red]];
    for (let index = 0; index < operators.length; index++) {
        const left = 10 + index * 120;
        assert.deepEqual([20, 50, 80].map(x => rgb(image, left + x, 40)), expected[index], operators[index]);
    }
    assert.deepEqual(rgb(image, 13, 120), red);
    assert.deepEqual(rgb(image, 40, 120), clear);
    assert.deepEqual(rgb(image, 180, 120), [128, 127, 0]);
    assert.deepEqual(rgb(image, 265, 120), clear);
    assert.deepEqual(rgb(image, 390, 120), clear);
    assert.deepEqual(rgb(image, 450, 120), clear);
    assert.deepEqual([20, 65, 95, 150, 210].map(x => rgb(image, x, 195)), [red, clear, red, clear, red]);
});
