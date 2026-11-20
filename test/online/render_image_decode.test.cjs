const {test} = require('node:test');
const assert = require('node:assert/strict');
const {renderFixtures, rgb} = require('./render_fixture.cjs');
const {pngLibrary} = require('./compare_snapshots.cjs');
const PNG = pngLibrary(require('node:path').resolve(__dirname, '../..'));
const pixels = new PNG({width: 2, height: 2});
pixels.data.fill(255);
const source = 'data:image/png;base64,' + PNG.sync.write(pixels).toString('base64');
const render = renderFixtures('image-decode');
const cases = [
    ['loading reflection and receiver brands', `
        const i=new Image, d=Object.getOwnPropertyDescriptor(HTMLImageElement.prototype,'loading');
        let branded=false;try{d.get.call(document.body)}catch(e){branded=e instanceof TypeError}
        i.loading='LaZy';
        const lazy=i.loading==='lazy'&&i.getAttribute('loading')==='LaZy';
        i.loading='unknown';
        done(branded&&lazy&&i.loading==='eager'&&i.getAttribute('loading')==='unknown');
    `],
    ['detached decode resolves after synchronous work', `
        const i=new Image; i.src=source;let synchronous=true;
        i.decode().then(value=>done(!synchronous&&value===undefined&&i.complete&&
            i.naturalWidth===2&&i.currentSrc===source),failed);
        synchronous=false;
    `],
    ['decode observes same-task source updates', `
        const i=new Image;const promise=i.decode();i.src=source;
        promise.then(()=>done(i.complete&&i.naturalHeight===2),failed);
    `],
    ['empty source rejects with EncodingError', `
        const i=new Image;
        i.decode().then(failed,e=>done(e instanceof DOMException&&e.name==='EncodingError'&&
            i.complete&&i.currentSrc===''));
    `],
    ['broken image rejects without fabricated readiness', `
        const i=new Image;i.src='data:image/png;base64,AA==';
        i.decode().then(failed,e=>done(e instanceof DOMException&&e.name==='EncodingError'&&
            i.complete&&i.naturalWidth===0));
    `],
    ['decode rejects an illegal receiver through its promise', `
        const p=HTMLImageElement.prototype.decode.call(document.body);
        const branded=p instanceof Promise;
        p.then(failed,e=>done(branded&&e instanceof TypeError));
    `],
];
for (const [name, script] of cases) test(name, async () => {
    const {image} = await render(name.replaceAll(' ', '-'), '<!doctype html><style>body{margin:0;background:red}</style><script>' +
        'const source=' + JSON.stringify(source) + ';function done(ok){document.body.style.background=ok?"lime":"red"}' +
        'function failed(){done(false)};document.addEventListener("DOMContentLoaded",()=>{' + script + '});</script>');
    assert.deepEqual(rgb(image, 10, 10), [0, 255, 0]);
});

test('SSR load handler can reveal an actually decoded image', async () => {
    const script = 'let completed=0;const supported="loading" in HTMLImageElement.prototype;' +
        'if(supported)document.body.addEventListener("load",function(e){const t=e.target;' +
        'if(t.dataset.mainImage===undefined)return;completed++;const copy=new Image;copy.src=t.currentSrc;' +
        'copy.decode().then(()=>{t.style.opacity=1;document.body.style.background=completed===1?"lime":"red"})},true);';
    const {image} = await render('ssr-image', '<!doctype html><style>body{margin:0;background:red}</style>' +
        '<body><script>' + script + '</script><img data-main-image src="' + source + '" style="opacity:0"></body>');
    assert.deepEqual(rgb(image, 10, 10), [0, 255, 0]);
});

test('currentSrc reports the selected picture source', async () => {
    const selectedPixels = new PNG({width: 3, height: 2});
    selectedPixels.data.fill(255);
    const selected = 'data:image/png;base64,' + PNG.sync.write(selectedPixels).toString('base64');
    const {image} = await render('picture-source', '<!doctype html><style>body{margin:0;background:red}</style>' +
        '<picture><source srcset="' + selected + '"><img id="image" src="' + source + '"></picture>' +
        '<script>const i=document.getElementById("image");i.decode().then(()=>{' +
        'if(i.currentSrc===' + JSON.stringify(selected) + '&&i.naturalWidth===3&&' +
        'Object.getOwnPropertyDescriptor(HTMLImageElement.prototype,"currentSrc").set===undefined)' +
        'document.body.style.background="lime"});</script>');
    assert.deepEqual(rgb(image, 10, 10), [0, 255, 0]);
});
