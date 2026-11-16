const {test, before, after} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const http = require('node:http');
const {parseOptions, runLambdaRender} = require('./capture_snapshots.cjs');
const {pngLibrary} = require('./compare_snapshots.cjs');
const {rgb} = require('./render_fixture.cjs');
const root = path.resolve(__dirname, '../..');
const scratch = fs.mkdtempSync(path.join(root, 'temp/css-resource-discovery-'));
const requests = [];
let server, origin;

before(async () => {
    server = http.createServer((request, response) => {
        const url = new URL(request.url, 'http://fixture.invalid');
        const run = url.searchParams.get('run');
        requests.push({run, path: url.pathname});
        response.setHeader('Cache-Control', 'no-store');
        if (url.pathname === '/event.css') {
            response.writeHead(run === 'error' ? 404 : 200, {'Content-Type': 'text/css'});
            response.end(run === 'empty' ? '' : run === 'chain-second'
                ? '#probe{background:blue}' : '#probe{background:rgb(0,128,0)}');
            return;
        }
        if (url.pathname === '/event-page') {
            const listener = `link.addEventListener('load',done);link.addEventListener('error',done);`;
            response.writeHead(200, {'Content-Type': 'text/html'});
            response.end(`<!doctype html><style>#probe{width:80px;height:80px;background:red}</style><div id="probe"></div><script>
                var steps=['script'],link=document.createElement('link');
                function done(event){steps.push(event.type);console.log('CSS_LINK_EVENT',JSON.stringify({
                    type:event.type,target:event.target===link,receiver:this===link,
                    sheet:!!link.sheet,steps:steps.join(',')}));
                    if(${run === 'chain'}&&steps.length===3){
                        link=document.createElement('link');link.rel='stylesheet';link.href='/event.css?run=chain-second';
                        link.addEventListener('load',done);link.addEventListener('error',done);document.head.appendChild(link);
                    }
                }
                window.addEventListener('load',()=>console.log('CSS_WINDOW_LOAD',steps.join(',')));
                link.rel='stylesheet';link.href='/event.css?run=${run}';
                ${run === 'after' ? '' : listener}document.head.appendChild(link);
                ${run === 'after' ? listener : ''}steps.push('end');
            </script>`);
            return;
        }
        if (url.pathname.endsWith('.svg')) {
            const fill = url.pathname === '/hero.svg' ? 'blue' : url.pathname === '/pseudo.svg' ? 'magenta' : 'lime';
            response.writeHead(200, {'Content-Type': 'image/svg+xml'});
            response.end(`<svg xmlns="http://www.w3.org/2000/svg" width="80" height="80"><path fill="${fill}" d="M0 0H80V80H0Z"/></svg>`);
            return;
        }
        if (url.pathname === '/styles/sheet.css') {
            let css = 'body{margin:0}#probe{width:80px;height:80px}img{display:block}';
            const count = run === 'capacity' ? 600 : 2;
            for (let index = 0; index < count; index++) {
                css += `.unused-${index}{background-image:url(/unused/${index}.svg?run=${run})}`;
            }
            css += `@media print{#probe{background-image:url(/inactive-media.svg?run=${run})}}` +
                `@supports(display:bogus){#probe{background-image:url(/inactive-supports.svg?run=${run})}}`;
            if (run === 'variable') css += `:root{--art:url(../used.svg?run=${run})}#probe{background-image:var(--art)}`;
            else if (run === 'activation') css += `#probe.active{background-image:url(../used.svg?run=${run})}`;
            else css += `#probe{background-image:url(../used.svg?run=${run})}`;
            if (run === 'pseudo') css += `#probe::before{content:"";display:block;width:40px;height:40px;background-image:url(../pseudo.svg?run=${run})}`;
            response.writeHead(200, {'Content-Type': 'text/css'});
            response.end(css);
            return;
        }
        response.writeHead(200, {'Content-Type': 'text/html'});
        const script = run === 'activation'
            ? '<script>document.getElementById("probe").className="active";</script>'
            : run === 'dynamic-sheet' ? `<script>var link=document.createElement('link');link.rel='stylesheet';link.href='/styles/sheet.css?run=${run}';document.head.appendChild(link);</script>` : '';
        const link = run === 'dynamic-sheet' ? '' : `<link rel="stylesheet" href="/styles/sheet.css?run=${run}">`;
        response.end(`<!doctype html><head>${link}</head><body><div id="probe"></div><img src="/hero.svg?run=${run}" width="80" height="80">${script}</body>`);
    });
    await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
    origin = `http://127.0.0.1:${server.address().port}`;
});
after(async () => {
    if (server) await new Promise(resolve => server.close(resolve));
    fs.rmSync(scratch, {recursive: true, force: true});
});

async function renderHttpFixture(run, route = '/page') {
    const png = path.join(scratch, run + '.png');
    const result = await runLambdaRender({label: run, url: `${origin}${route}?run=${run}`},
        png, parseOptions([], root), scratch);
    assert.equal(result.ok, true, result.error);
    return {image: pngLibrary(root).sync.read(fs.readFileSync(png)),
        log: fs.readFileSync(path.join(scratch, result.outputLog), 'utf8')};
}

for (const run of ['capacity', 'variable', 'pseudo', 'activation', 'dynamic-sheet']) {
    test('CSS resource discovery: ' + run, async () => {
        const {image} = await renderHttpFixture(run);
        assert.deepEqual(rgb(image, 60, 60), [0, 255, 0], 'selected CSS background is ready');
        assert.deepEqual(rgb(image, 20, 100), [0, 0, 255], 'visible image retains resource admission');
        if (run === 'pseudo') assert.deepEqual(rgb(image, 20, 20), [255, 0, 255]);
        const selected = requests.filter(row => row.run === run);
        assert(selected.some(row => row.path === '/used.svg'), 'stylesheet-relative URL is requested');
        assert(!selected.some(row => row.path.startsWith('/unused/')), 'unmatched icon rules acquired resources');
        assert(!selected.some(row => row.path.startsWith('/inactive-')), 'inactive rules acquired resources');
    });
}

for (const run of ['before', 'after', 'empty', 'error', 'chain']) {
    test('CSS link completion: ' + run, async () => {
        const {image, log} = await renderHttpFixture(run, '/event-page');
        const events = [...log.matchAll(/CSS_LINK_EVENT (\{[^\n]+\})/g)].map(match => JSON.parse(match[1]));
        assert.equal(events.length, run === 'chain' ? 2 : 1, 'each connected stylesheet must finish with one event');
        const failed = run === 'error';
        assert.deepEqual(events[0], {type: failed ? 'error' : 'load', target: true, receiver: true,
            sheet: !failed, steps: 'script,end,' + (failed ? 'error' : 'load')});
        if (run === 'chain') assert.deepEqual(events[1], {type:'load', target:true, receiver:true,
            sheet:true, steps:'script,end,load,load'});
        assert(log.lastIndexOf('CSS_LINK_EVENT') < log.indexOf('CSS_WINDOW_LOAD'),
            'stylesheet completion and its dependent stylesheet must precede window load');
        assert.deepEqual(rgb(image, 40, 40), run === 'chain' ? [0, 0, 255] :
            run === 'before' || run === 'after' ? [0, 128, 0] : [255, 0, 0]);
    });
}
