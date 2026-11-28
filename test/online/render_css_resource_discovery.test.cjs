const {test, before, after} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const http = require('node:http');
const {parseOptions, runLambdaRender} = require('./capture_snapshots.cjs');
const {pngLibrary} = require('./compare_snapshots.cjs');
const {rgb, renderFixtures, loggedResult} = require('./render_fixture.cjs');
const renderLocalMedia = renderFixtures('media-resource-local');
const {scriptCompletionSource, scriptCompletionHtml} = require('./script_completion_fixtures.cjs');
const root = path.resolve(__dirname, '../..');
const scratch = fs.mkdtempSync(path.join(root, 'temp/css-resource-discovery-'));
const requests = [];
const imageDiscovery = new Map();
let server, origin;

function fetchBaseHtml(run, local = false, xhr = false) {
    return `<!doctype html><head><template><base href="${origin}/ignored/"></template>
        <base id="active-base" href="${origin}/base/assets/"><base href="${origin}/later/">
        </head><body><script>
        (async () => {
            const rows = [];
            const request = ${xhr ? `reference => new Promise((resolve, reject) => {
                const xhr = new XMLHttpRequest();
                xhr.open('GET', reference);
                xhr.onload = () => resolve(JSON.parse(xhr.responseText));
                xhr.onerror = () => reject(new Error('XHR failed'));
                xhr.send();
            })` : `reference => fetch(reference).then(response => response.json())`};
            const read = async (reference) => rows.push((await request(reference + '?run=${run}')).path);
            await read('value');
            await read('/value');
            document.getElementById('active-base').href = '${origin}/changed/';
            await read('value');
            document.getElementById('active-base').remove();
            await read('value');
            document.querySelectorAll('base').forEach(base => base.remove());
            await read(${local ? JSON.stringify(origin + '/base/value') : "'value'"});
            console.log('FETCH_BASE_RESULT', JSON.stringify({rows}));
        })().catch(error => console.log('FETCH_BASE_RESULT', JSON.stringify({error:String(error)})));
        </script></body>`;
}

function mediaSourceHtml(run, source = `/media.mp4?run=${run}`) {
    const deferred = run === 'media-none';
    const preload = deferred || run === 'media-none-autoplay' ? 'preload="none"' : '';
    const media = run === 'media-child' ? `<source src="${source}" type="video/mp4">` : '';
    return `<!doctype html><style>body{margin:0}video{display:block;width:320px;height:${deferred ? '120px' : 'auto'}}#band{height:10px;background:lime}</style>
        <video ${preload} ${deferred ? '' : 'autoplay muted'} ${media ? '' : `src="${source}"`}>${media}</video><div id="band"></div>`;
}

before(async () => {
    server = http.createServer((request, response) => {
        const url = new URL(request.url, 'http://fixture.invalid');
        const run = url.searchParams.get('run');
        requests.push({run, path: url.pathname, initial: url.searchParams.has('initial')});
        response.setHeader('Cache-Control', 'no-store');
        if (url.pathname === '/image-discovery-page') {
            imageDiscovery.set(run, {released: false, early: [], release: null});
            const picture = run === 'early-picture';
            response.writeHead(200, {'Content-Type': 'text/html'});
            response.end(`<!doctype html><head><base href="/image-discovery/">
                <link rel="stylesheet" href="sheet.css?run=${run}"></head><body>
                <template><img src="inert.svg?run=${run}"></template>
                <noscript><img src="noscript.svg?run=${run}"></noscript>
                <script>var heroLoads=0;document.body.addEventListener('load',function(event){
                    if(event.target.id==='hero')heroLoads++},true);
                window.addEventListener('load',function(){var hero=document.getElementById('hero');
                    console.log('EAGER_IMAGE_RESULT',JSON.stringify({loads:heroLoads,complete:hero.complete,
                        width:hero.naturalWidth,current:hero.currentSrc.indexOf('/selected.svg?')>=0}))});</script>
                ${picture ? `<picture><source media="(max-width:400px)" srcset="mobile.svg?run=${run}">
                    <source media="(min-width:1000px)" type="image/svg+xml"
                        srcset="selected.svg?run=${run} 80w, large.svg?run=${run} 160w" sizes="80px">` : ''}
                <img id="hero" loading="${picture ? 'EaGeR' : 'invalid'}" width="80" height="80"
                    src="${picture ? 'fallback' : 'selected'}.svg?run=${run}">
                ${picture ? '</picture>' : ''}
                <img loading="lazy" width="80" height="80" src="lazy.svg?run=${run}">
                </body>`);
            return;
        }
        if (url.pathname.startsWith('/image-discovery/')) {
            const state = imageDiscovery.get(run);
            if (url.pathname.endsWith('/sheet.css')) {
                const timer = setTimeout(() => state.release(), 400);
                state.release = () => {
                    if (state.released) return;
                    state.released = true;
                    clearTimeout(timer);
                    response.writeHead(200, {'Content-Type': 'text/css'});
                    response.end('body{margin:0}img{display:block}');
                };
                if (state.early.includes('/image-discovery/selected.svg')) state.release();
            } else {
                if (!state.released) state.early.push(url.pathname);
                response.writeHead(200, {'Content-Type': 'image/svg+xml'});
                response.end('<svg xmlns="http://www.w3.org/2000/svg" width="80" height="80"><path fill="lime" d="M0 0H80V80H0Z"/></svg>');
                if (url.pathname.endsWith('/selected.svg') && state.release) state.release();
            }
            return;
        }
        if (url.pathname === '/script-completion-page') {
            response.writeHead(200, {'Content-Type': 'text/html'});
            response.end(scriptCompletionHtml(run));
            return;
        }
        if (url.pathname === '/script-completion.js') {
            response.writeHead(run === 'fetch-error' ? 404 : 200, {'Content-Type': 'application/javascript'});
            response.end(run === 'fetch-error' ? '' : scriptCompletionSource(run));
            return;
        }
        if (url.pathname.endsWith('/value')) {
            response.writeHead(200, {'Content-Type': 'application/json'});
            response.end(JSON.stringify({path: url.pathname}));
            return;
        }
        if (url.pathname === '/base/fetch-page') {
            response.writeHead(200, {'Content-Type': 'text/html'});
            response.end(fetchBaseHtml(run, false, run.includes('xhr')));
            return;
        }
        if (url.pathname === '/media.mp4') {
            response.writeHead(200, {'Content-Type': 'video/mp4'});
            response.end(fs.readFileSync(path.join(root, 'test/media/test_video.mp4')));
            return;
        }
        if (url.pathname === '/media-source-page') {
            response.writeHead(200, {'Content-Type': 'text/html'});
            response.end(mediaSourceHtml(run));
            return;
        }
        if (url.pathname === '/base/page') {
            response.writeHead(200, {'Content-Type': 'text/html'});
            response.end(`<!doctype html><head><template><base href="/ignored/"></template>
                <base href="/base/assets/"><base href="/later/">
                <link rel="stylesheet" href="entry.css?run=${run}"></head><body>
                <div id="probe"></div><img src="image.svg?run=${run}">
                <script src="entry.js?run=${run}" defer></script>
                <script type="module">import {value} from './value.js?run=${run}';
                    document.getElementById('probe').dataset.module=value;</script><script>
                window.addEventListener('load',()=>{const probe=document.getElementById('probe');
                    console.log('BASE_URL_CONTROL',[document.URL===${JSON.stringify(origin + request.url)},
                        location.href===document.URL,document.baseURI===${JSON.stringify(origin + '/base/assets/')},
                        probe.dataset.classic==='loaded',probe.dataset.module==='loaded'].join(','));});
                </script></body>`);
            return;
        }
        const baseAssets = {
            '/base/assets/entry.css': ['text/css', 'body{margin:0}#probe{width:80px;height:80px;background:green}img{display:block}'],
            '/base/assets/entry.js': ['application/javascript', "document.getElementById('probe').dataset.classic='loaded';"],
            '/base/assets/value.js': ['application/javascript', "export const value='loaded';"],
            '/base/assets/image.svg': ['image/svg+xml', '<svg xmlns="http://www.w3.org/2000/svg" width="80" height="80"><path fill="blue" d="M0 0H80V80H0Z"/></svg>'],
        };
        if (baseAssets[url.pathname]) {
            response.writeHead(200, {'Content-Type': baseAssets[url.pathname][0]});
            response.end(baseAssets[url.pathname][1]);
            return;
        }
        if (url.pathname === '/template-page') {
            response.writeHead(200, {'Content-Type': 'text/html'});
            response.end(`<!doctype html><head><template>
                <link rel="stylesheet" href="/inert/sheet.css?run=${run}">
                <style>#probe{background:red!important}</style>
                <script>globalThis.inertInline=true;</script><script src="/inert/script.js?run=${run}"></script>
                </template><style>body{margin:0}#probe{width:80px;height:80px;background:green}</style>
                </head><body><div id="probe"></div><template>
                <img src="/inert/image.svg?run=${run}">
                <div style="background-image:url(/inert/background.svg?run=${run})"></div></template>
                <script>
                    const template = document.createElement('template');
                    const content = template.content;
                    document.body.appendChild(template);
                    template.innerHTML = '<img src="/inert/runtime.svg?run=${run}">' +
                        '<link rel="stylesheet" href="/inert/runtime.css?run=${run}">' +
                        '<script>globalThis.inertInline=true;</scr' + 'ipt>';
                    console.log('TEMPLATE_RESOURCE_CONTROL',globalThis.inertInline===undefined,
                        document.styleSheets.length===1,content.childNodes.length===3);
                </script></body>`);
            return;
        }
        if (url.pathname === '/event.css') {
            response.writeHead(run === 'error' ? 404 : 200, {'Content-Type': 'text/css'});
            response.end(run === 'empty' ? '' : run === 'chain-second'
                ? '#probe{background:blue}' : '#probe{background:rgb(0,128,0)}');
            return;
        }
        if (url.pathname === '/initial-event-page') {
            response.writeHead(200, {'Content-Type': 'text/html'});
            response.end(`<!doctype html><style>#probe{width:80px;height:80px;background:red}</style>
                <link id="sheet" rel="stylesheet" media="${run === 'print' ? 'print' : 'all'}"
                    href="/event.css?run=${run}&initial=1" onload="this.media='all';console.log('INITIAL_CSS_EVENT','load')"
                    onerror="console.log('INITIAL_CSS_EVENT','error')">
                <div id="probe"></div><script>
                    document.getElementById('sheet').addEventListener('${run === 'error' ? 'error' : 'load'}',
                        function(e){console.log('INITIAL_CSS_LISTENER',e.target===this,!!this.sheet)});
                    window.addEventListener('load',()=>console.log('INITIAL_WINDOW_LOAD',document.styleSheets.length));
                </script>`);
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
            else if (run === 'quoted') css += `#probe{background-image:url("../used.svg?run=${run}")}`;
            else if (run === 'quoted-query') css += `#probe{background-image:url("../used+hero.svg?run=${run}&crop=0,11,1440,1602")}`;
            else if (run === 'image-set-string') css += `#probe{background-image:image-set("../used.svg?run=${run}" type("image/svg+xml") 1x)}`;
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
        const inline = run === 'quoted-query-inline'
            ? ` style="background-image:url(&quot;${origin}/used+hero.svg?run=${run}&amp;crop=0,11,1440,1602&quot;)"` : '';
        response.end(`<!doctype html><head>${link}</head><body><div id="probe"${inline}></div><img src="/hero.svg?run=${run}" width="80" height="80">${script}</body>`);
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

for (const run of ['early-image', 'early-picture']) {
    test('eager image discovery overlaps blocking CSS: ' + run, async () => {
        const {image, log} = await renderHttpFixture(run, '/image-discovery-page');
        assert.deepEqual(imageDiscovery.get(run).early, ['/image-discovery/selected.svg'],
            'only the selected eager image starts before the stylesheet completes');
        const selected = requests.filter(row => row.run === run);
        assert.equal(selected.filter(row => row.path === '/image-discovery/selected.svg').length, 1,
            'later discovery reuses the same request');
        assert(!selected.some(row => /\/(inert|noscript|mobile|large|fallback)\.svg$/.test(row.path)),
            'inert nodes and unselected responsive candidates do not acquire resources');
        assert.deepEqual(loggedResult(log, 'EAGER_IMAGE_RESULT'), {
            loads: 1, complete: true, width: 80, current: true,
        }, 'early bytes retain element completion before window.load');
        assert.deepEqual(rgb(image, 40, 40), [0, 255, 0]);
    });
}

for (const run of ['blocking', 'defer', 'async', 'module', 'throw', 'empty', 'removed', 'fetch-error', 'inline', 'inline-module']) {
    test('script completion: ' + run, async () => {
        const {image, log} = await renderHttpFixture(run, '/script-completion-page');
        const state = loggedResult(log, 'SCRIPT_COMPLETION_RESULT');
        const inline = run === 'inline' || run === 'inline-module';
        const fetched = run !== 'fetch-error';
        const executes = fetched && run !== 'empty';
        const loads = !inline && fetched ? 1 : 0;
        assert.equal(state.loads, loads);
        assert.equal(state.errors, fetched ? 0 : 1);
        assert.equal(state.ready, executes);
        assert.equal(state.listeners, executes && loads ? 1 : 0);
        assert.equal(state.shape, true, 'load/error event shape and restored currentScript');
        assert.equal(state.scope, executes && run !== 'module' && run !== 'inline-module' ? 'resource' : null);
        assert.equal(state.handlerMicrotask, !!loads);
        if (loads) {
            if (executes) assert(state.steps.indexOf('execute-microtask') < state.steps.indexOf('load'));
            assert(state.steps.indexOf('load-microtask') < state.steps.indexOf('window'));
            if (run !== 'async') assert.equal(state.headTail, !['blocking', 'throw', 'empty'].includes(run));
        }
        assert.deepEqual(rgb(image, 40, 40), [0, 128, 0]);
        assert.deepEqual(rgb(image, 120, 40), executes ? [0, 128, 0] : [255, 255, 255],
            'script completion must retain stylesheets inserted during evaluation');
        assert.deepEqual(rgb(image, 40, 100), [0, 0, 255],
            'load/error dispatch must retain earlier bootstrap stylesheet writes');
    });
}

test('HTML base resolves initial dependencies without changing document identity', async () => {
    const {image, log} = await renderHttpFixture('html-base', '/base/page');
    assert.match(log, /BASE_URL_CONTROL true,true,true,true,true/);
    assert.deepEqual(rgb(image, 40, 40), [0, 128, 0]);
    assert.deepEqual(rgb(image, 40, 100), [0, 0, 255]);
    const selected = requests.filter(row => row.run === 'html-base');
    for (const name of ['entry.css', 'entry.js', 'value.js', 'image.svg']) {
        assert(selected.some(row => row.path === '/base/assets/' + name), name + ' resolves against the first active base');
    }
    assert(!selected.some(row => row.path.startsWith('/ignored/') || row.path.startsWith('/later/')));
});

for (const xhr of [false, true]) for (const local of [false, true]) {
    test(`${xhr ? 'XHR' : 'fetch'} uses the active HTML base through mutations in ${local ? 'file' : 'HTTP'} documents`, async () => {
        const run = `${xhr ? 'xhr' : 'fetch'}-base-${local ? 'file' : 'http'}`;
        const {log} = local
            ? await renderLocalMedia(run, fetchBaseHtml(run, true, xhr))
            : await renderHttpFixture(run, '/base/fetch-page');
        assert.deepEqual(loggedResult(log, 'FETCH_BASE_RESULT'), {
            rows: ['/base/assets/value', '/value', '/changed/value', '/later/value', '/base/value']
        });
        assert(!requests.some(row => row.run === run && row.path.startsWith('/ignored/')));
    });
}

test('HTML template contents neither activate nor acquire document resources', async () => {
    const {image, log} = await renderHttpFixture('inert-template', '/template-page');
    assert.match(log, /TEMPLATE_RESOURCE_CONTROL true true true/);
    assert.deepEqual(rgb(image, 40, 40), [0, 128, 0]);
    assert(!requests.some(row => row.run === 'inert-template' && row.path.startsWith('/inert/')),
        'inert stylesheet, script, image and inline CSS must not enter resource discovery: ' +
        JSON.stringify(requests.filter(row => row.run === 'inert-template')));
});

for (const run of ['capacity', 'variable', 'pseudo', 'activation', 'dynamic-sheet', 'quoted', 'quoted-query', 'quoted-query-inline', 'image-set-string']) {
    test('CSS resource discovery: ' + run, async () => {
        const {image} = await renderHttpFixture(run);
        assert.deepEqual(rgb(image, 60, 60), [0, 255, 0], 'selected CSS background is ready');
        assert.deepEqual(rgb(image, 20, 100), [0, 0, 255], 'visible image retains resource admission');
        if (run === 'pseudo') assert.deepEqual(rgb(image, 20, 20), [255, 0, 255]);
        const selected = requests.filter(row => row.run === run);
        assert(selected.some(row => row.path === (run.startsWith('quoted-query') ? '/used+hero.svg' : '/used.svg')),
            'selected stylesheet/inline URL is requested');
        assert(!selected.some(row => row.path.startsWith('/unused/')), 'unmatched icon rules acquired resources');
        assert(!selected.some(row => row.path.startsWith('/inactive-')), 'inactive rules acquired resources');
        assert(!requests.some(row => row.path === '/styles/image/svg+xml'), 'image-set type string became a URL');
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

for (const run of ['print', 'active', 'empty', 'error']) {
    test('Initial CSS link completion: ' + run, async () => {
        const {image, log} = await renderHttpFixture(run, '/initial-event-page');
        const failed = run === 'error';
        assert.deepEqual([...log.matchAll(/INITIAL_CSS_EVENT (load|error)/g)].map(m => m[1]),
            [failed ? 'error' : 'load'], 'initial link completes exactly once');
        assert(log.includes('INITIAL_CSS_LISTENER true '), 'listener observes its link as the target');
        if (!failed) assert(log.includes('INITIAL_CSS_LISTENER true true'), 'successful completion exposes its sheet');
        assert(log.indexOf('INITIAL_CSS_EVENT') < log.indexOf('INITIAL_WINDOW_LOAD'), 'link completes before window load');
        if (!failed) assert(log.includes('INITIAL_WINDOW_LOAD 2'), 'prefetched sheet is published only once');
        assert.deepEqual(rgb(image, 40, 40), failed || run === 'empty' ? [255, 0, 0] : [0, 128, 0]);
        assert.equal(requests.filter(row => row.run === run && row.initial && row.path === '/event.css').length, 1,
            'discovery reuses the prefetched transfer');
    });
}

for (const run of ['media-direct', 'media-child', 'media-none-autoplay', 'media-none']) {
    test(`snapshot media discovery: ${run}`, {skip: process.platform !== 'darwin'}, async () => {
        const {image} = await renderHttpFixture(run, '/media-source-page');
        const deferred = run === 'media-none';
        assert.deepEqual(rgb(image, 10, (deferred ? 120 : 180) + 4), [0, 255, 0],
            'ready video metadata supplies its 640:360 ratio before snapshot layout');
        assert.equal(requests.filter(row => row.run === run && row.path === '/media.mp4').length,
            deferred ? 0 : 1, 'preload none stays deferred unless autoplay requests media; layout reuses the transfer');
    });
}

test('snapshot media discovery: local HTML discovers its remote source', {skip: process.platform !== 'darwin'}, async () => {
    const run = 'media-local';
    const {image} = await renderLocalMedia(run, mediaSourceHtml(run, `${origin}/media.mp4?run=${run}`));
    assert.deepEqual(rgb(image, 10, 184), [0, 255, 0], 'remote media initializes its manager before the snapshot wait');
    assert.equal(requests.filter(row => row.run === run && row.path === '/media.mp4').length, 1);
});
