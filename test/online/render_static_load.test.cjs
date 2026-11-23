const {test} = require('node:test');
const assert = require('node:assert/strict');
const {renderFixtures, serveFixtureResources, rgb, loggedResult} = require('./render_fixture.cjs');

const render = renderFixtures('static-load');
const origin = serveFixtureResources((request, response) => {
    const url = new URL(request.url, 'http://fixture.invalid');
    const mode = url.searchParams.get('mode');
    response.setHeader('Cache-Control', 'no-store');
    if (url.pathname === '/page') {
        const image = '/image.svg?mode=' + mode;
        const markup = mode.endsWith('image')
            ? `<img ${mode === 'lazy-image' ? 'loading="lazy"' : ''} width="100" height="100" src="${image}">`
            : `<div style="width:100px;height:100px;background:white url('${image}')"></div>`;
        const script = mode === 'scripted'
            ? `<script>window.addEventListener('load',function(){
                console.log('STATIC_LOAD_FRAME',JSON.stringify({now:performance.now()}));
            });</script>`
            : mode === 'data-script' ? '<script type="application/ld+json">{"name":"sample"}</script>'
            : mode === 'document-skipped' ? '<script>document.querySelector("div").style.background="red";</script>'
            : mode === 'source-skipped' ? '<script type="module" src="/entry.js"></script>' : '';
        response.writeHead(200, {'Content-Type': 'text/html'});
        response.end('<!doctype html><style>body{margin:0}</style>' + markup + script);
    } else if (url.pathname === '/image.svg') {
        // exceed optional snapshot grace while staying within the ordinary navigation deadline.
        const timer = setTimeout(() => response.writeHead(200, {'Content-Type': 'image/svg+xml'}).end(
            '<svg xmlns="http://www.w3.org/2000/svg" width="100" height="100"><rect width="100" height="100" fill="blue"/></svg>'), 1600);
        response.on('close', () => clearTimeout(timer));
    } else if (url.pathname === '/entry.js') {
        response.writeHead(200, {'Content-Type': 'text/javascript'});
        response.end('/*' + 'x'.repeat(200000) + '*/throw new Error("unadmitted source executed");');
    } else response.writeHead(404).end();
});

for (const mode of ['static', 'scripted', 'data-script', 'source-skipped', 'document-skipped', 'eager-image', 'lazy-image']) {
    test('initial image load without executable scripts: ' + mode, async () => {
        const {image, log} = await render(mode, {url: origin() + '/page?mode=' + mode},
            {...process.env, RADIANT_JS_TASK_DIAGNOSTICS: '1',
                ...(mode === 'source-skipped' ? {RADIANT_JS_EXTERNAL_SCRIPT_BYTES: '1024'} : {}),
                ...(mode === 'document-skipped' ? {RADIANT_JS_TOTAL_SCRIPT_BYTES: '1'} : {})});
        assert.deepEqual(rgb(image, 50, 50), mode === 'lazy-image' ? [255, 255, 255] : [0, 0, 255]);
        if (mode === 'scripted') assert.equal(loggedResult(log, 'STATIC_LOAD_FRAME').now, 0);
        if (mode === 'source-skipped') assert.match(log, /status=skipped-large-defer/);
    });
}
