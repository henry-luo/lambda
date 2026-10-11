import {build} from 'esbuild';
import puppeteer from 'puppeteer';
import {createServer} from 'node:http';
import {createHash} from 'node:crypto';
import {readFile, mkdir, writeFile} from 'node:fs/promises';
import {fileURLToPath} from 'node:url';
import path from 'node:path';

const directory = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(directory, '../../../..');
const output = path.join(root, 'temp/ui_dtna/reference');
const font = process.env.DTNA_REFERENCE_FONT || path.join(root, 'test/layout/data/font/LiberationSans-Regular.ttf');
const sha256 = data => createHash('sha256').update(data).digest('hex');
await mkdir(output, {recursive: true});
const fontBytes = await readFile(font);
const faces = [{path:font,bytes:fontBytes,weight:'100 500',style:'normal'}];
// pin bold/italic faces as well; native font matching must not be compared with browser synthesis.
if (!process.env.DTNA_REFERENCE_FONT) {
    for (const [name,weight,style] of [['Bold','600 900','normal'],['Italic','100 500','italic'],['BoldItalic','600 900','italic']]) {
        const filename = path.join(path.dirname(font),`LiberationSans-${name}.ttf`);
        faces.push({path:filename,bytes:await readFile(filename),weight,style});
    }
}
const bundle = await build({
    absWorkingDir: directory, entryPoints: ['fixtures.jsx'], bundle: true,
    write: false, format: 'iife', define: {'process.env.NODE_ENV': '"production"'},
});
const script = bundle.outputFiles[0].contents;
await writeFile(path.join(output, 'reference.js'), script);
const html = `<!doctype html><html lang="en-US"><meta charset="UTF-8">
<style>${faces.map((face,index)=>`@font-face{font-family:'Reference Sans';src:url('/font-${index}.ttf');font-weight:${face.weight};font-style:${face.style}}`).join('')}body{margin:0;padding:24px;font-family:'Reference Sans'}</style>
<div id="root"></div><script src="/reference.js"></script></html>`;
const routes = {'/reference.js': ['text/javascript', script],
    ...Object.fromEntries(faces.map((face,index)=>[`/font-${index}.ttf`,['font/ttf',face.bytes]]))};
const server = createServer((request, response) => {
    const resource = routes[new URL(request.url, 'http://localhost').pathname];
    response.setHeader('Content-Type', resource ? resource[0] : 'text/html');
    response.end(resource ? resource[1] : html);
});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
let browser;
try {
    const executable = process.env.CHROME_HEADLESS_SHELL || puppeteer.executablePath({headless: 'shell'});
    browser = await puppeteer.launch({
        executablePath: executable,
        headless: 'shell', userDataDir: path.join(output, 'browser-profile'),
        args: ['--no-sandbox', '--disable-setuid-sandbox', '--lang=en-US'],
    });
    const page = await browser.newPage();
    await page.setViewport({width: 800, height: 600, deviceScaleFactor: 1});
    await page.emulateTimezone('UTC');
    await page.emulateMediaFeatures([{name: 'prefers-reduced-motion', value: 'reduce'}]);
    const errors = [];
    page.on('pageerror', error => errors.push(error.message));
    const captures = [];
    const geometryOf = async () => page.$$eval('[data-case] .ant-btn, [data-case] button, [data-case] input, [data-case] .ant-select, [role="progressbar"], .ant-timeline-item, .ant-statistic, .ant-badge, .ant-descriptions, .ant-skeleton, .ant-card, .ant-empty, .ant-result, [data-case] .ant-typography, [data-case] .ant-row, [data-case] .ant-col, [data-case="grid-modes"], [data-case="grid-modes"] [id], .ant-segmented, .ant-segmented-item, .ant-avatar, .ant-tag, .ant-rate, .ant-alert, .ant-spin, .ant-spin-dot-holder, .ant-spin-description, .ant-spin-container', nodes =>
        nodes.map(node => {
            const box = node.getBoundingClientRect();
            const style = getComputedStyle(node);
            const segmented = node.matches('.ant-segmented-item') ? node.closest('.ant-segmented') : null;
            const spin = node.matches('.ant-spin-dot-holder,.ant-spin-description,.ant-spin-container') ? node.closest('.ant-spin') : null;
            const spinPart = node.matches('.ant-spin-dot-holder') ? 'indicator' : node.matches('.ant-spin-description') ? 'description' : 'container';
            const identifier = node.id || (segmented ? `${segmented.id}-item-${[...segmented.querySelectorAll('.ant-segmented-item')].indexOf(node)}` : spin ? `${spin.id}-${spinPart}` : '');
            return {id: identifier, text: node.textContent, role: node.getAttribute('role'),
                x: box.x, y: box.y, width: box.width, height: box.height,
                style:Object.fromEntries(['font-family','font-size','font-weight','line-height','margin-top','margin-bottom','padding','color','background-color','border-color','border-radius','opacity','pointer-events']
                    .map(property=>[property,style.getPropertyValue(property)]))};
        }));
    for (const fixture of ['foundation', 'composition', 'progress', 'progress-gradients', 'timeline-statistic', 'display', 'skeleton', 'surfaces', 'typography', 'pagination-modes', 'grid-modes', 'typography-tokens', 'segmented-modes', 'avatar-modes', 'tag-modes', 'rate-modes', 'alert-modes', 'spin-modes', 'button-modes']) {
        const viewport = fixture === 'button-modes' ? {width:900,height:700} : fixture === 'surfaces' ? {width:820,height:1050} :
            fixture === 'grid-modes' ? {width:500,height:700} : fixture === 'typography-tokens' ? {width:600,height:900} :
            fixture === 'segmented-modes' ? {width:600,height:650} :
            fixture === 'avatar-modes' ? {width:500,height:600} :
            fixture === 'tag-modes' ? {width:600,height:700} :
            fixture === 'rate-modes' ? {width:600,height:700} :
            fixture === 'alert-modes' ? {width:600,height:950} :
            fixture === 'spin-modes' ? {width:600,height:800} :
            fixture === 'progress-gradients' ? {width:300,height:250} : fixture === 'typography' ? {width:600,height:600} :
            fixture === 'skeleton' || fixture === 'progress' ? {width:500,height:500} :
            fixture === 'timeline-statistic' ? {width:600,height:500} : {width:800,height:600};
        await page.setViewport({...viewport,deviceScaleFactor:1});
        await page.goto(`http://127.0.0.1:${server.address().port}/?fixture=${fixture}`, {waitUntil: 'networkidle0'});
        await page.evaluate(() => document.fonts.ready);
        if (fixture === 'spin-modes' || fixture === 'button-modes') {
            // constant Spin keyframes ignore the global motion token; freeze their CSS phase explicitly.
            await page.evaluate(()=>document.getAnimations().forEach(animation=>{animation.pause();animation.currentTime=0;}));
        }
        const geometry = await geometryOf();
        const filename = `${fixture}.png`;
        const bytes = await page.screenshot({path: path.join(output, filename)});
        const capture = {fixture, filename, viewport:{...viewport,scale:1}, sha256: sha256(bytes), geometry};
        captures.push(capture);
        if (fixture === 'button-modes') {
            capture.semantic = await page.$eval('#button-parts',node=>({
                root:node.classList.contains('authored-button'),
                icon:!!node.querySelector('.authored-image'),
                contentWeight:getComputedStyle(node.querySelector('.authored-caption')).fontWeight,
            }));
            await page.click('#button-delay');
            await page.click('#button-primary');
            await page.keyboard.press('Space');
            await page.click('#button-default');
            await page.keyboard.press('Enter');
            for (const id of ['disabled','loading','custom','disabled-link']) await page.click(`#button-${id}`);
            await page.click('#button-link');
            await page.click('#button-submit');
            await page.click('#button-form-input');
            await page.keyboard.press('End');
            await page.keyboard.type('edit');
            await page.click('#button-reset');
            const reset = await page.$eval('#button-form-input',node=>node.value);
            await page.waitForFunction(()=>document.getElementById('button-delay').classList.contains('ant-btn-loading'));
            await page.click('#button-delay');
            await page.click('#button-controlled');
            await page.click('#button-toggle');
            await page.click('#button-controlled');
            await page.waitForFunction(()=>document.getElementById('button-controlled').classList.contains('ant-btn-loading'));
            await page.click('#button-controlled');
            await page.click('#button-toggle');
            await page.click('#button-controlled');
            await page.keyboard.press('Enter');
            await page.click('#button-remove');
            capture.interaction = await page.evaluate(()=>({
                count:document.getElementById('button-count').textContent,
                request:document.getElementById('button-request').textContent,
                submits:document.getElementById('button-submits').textContent,
                removed:!document.getElementById('button-removable'),
            }));
            capture.interaction.reset = reset;
            if (JSON.stringify(capture.interaction) !== JSON.stringify({count:'12',request:'button-controlled',submits:'1',removed:true,reset:'seed'}))
                throw new Error(`Button interaction mismatch: ${JSON.stringify(capture.interaction)}`);
        }
        if (fixture === 'spin-modes') {
            await page.click('#idle-content');
            await page.click('#blocked-content');
            await page.click('#toggle-spin');
            await page.waitForFunction(()=>document.getElementById('spin-controlled').getAttribute('aria-busy') === 'true');
            await page.click('#toggle-spin');
            await page.waitForFunction(()=>document.getElementById('spin-controlled').getAttribute('aria-busy') === 'false');
            await page.click('#remove-spin');
            await page.click('#toggle-fullscreen');
            const fullscreen = await page.$eval('#spin-fullscreen',node=>{
                const box=node.getBoundingClientRect();
                return {x:box.x,y:box.y,width:box.width,height:box.height};
            });
            await page.keyboard.press('Space');
            const fullscreenHidden = await page.$eval('#spin-fullscreen',node=>node.getAttribute('aria-busy') === 'false');
            await page.click('#toggle-fullscreen');
            await page.click('#toggle-spin');
            const blocked = await page.$eval('#spin-controlled',node=>node.getAttribute('aria-busy') === 'false');
            await page.waitForFunction(()=>document.getElementById('spin-delay').getAttribute('aria-busy') === 'true');
            capture.interaction = await page.evaluate(()=>({
                count:document.getElementById('spin-clicks').textContent,
                removed:!document.getElementById('spin-removable'),
            }));
            capture.interaction = {...capture.interaction,fullscreenHidden,blocked,fullscreen};
            capture.animationPolicy = 'CSS Web Animations paused at 0ms; automatic progress is a live observation and this PNG has no pixel-parity gate';
            if (JSON.stringify(capture.interaction) !== JSON.stringify({count:'1',removed:true,fullscreenHidden:true,blocked:true,fullscreen:{x:0,y:0,width:600,height:800}}))
                throw new Error(`Spin interaction mismatch: ${JSON.stringify(capture.interaction)}`);
        }
        if (fixture === 'alert-modes') {
            await page.click('#alert-retry');
            await page.click('#alert-error .ant-alert-close-icon');
            const close = await page.$('#alert-parts .ant-alert-close-icon');
            await close.focus();
            await page.keyboard.press('Space');
            await page.click('#alert-retain .ant-alert-close-icon');
            await page.click('.alert-inner .ant-alert-close-icon');
            const nestedSurvived = await page.$eval('.alert-outer',node=>!node.querySelector('.alert-inner'));
            if (!nestedSurvived) throw new Error('nested Alert close did not retain its parent');
            await page.click('.alert-outer > .ant-alert-close-icon');
            capture.interaction = await page.evaluate(()=>({
                count:document.getElementById('alert-count').textContent,
                request:document.getElementById('alert-request').textContent,
                closed:['alert-error','alert-parts','alert-retain'].every(id=>!document.getElementById(id)) && !document.querySelector('.alert-outer'),
            }));
            capture.interaction.nestedSurvived = nestedSurvived;
            if (JSON.stringify(capture.interaction) !== JSON.stringify({count:'6',request:'null:close',closed:true,nestedSurvived:true}))
                throw new Error(`Alert interaction mismatch: ${JSON.stringify(capture.interaction)}`);
        }
        if (fixture === 'rate-modes') {
            const choose = async (id,index,offset) => {
                const stars = await page.$$(`#${id} .ant-rate-star`);
                const box = await stars[index].boundingBox();
                await page.mouse.click(box.x + (offset ?? box.width/2),box.y + box.height/2);
            };
            await choose('rate-half',1,2);
            await choose('rate-half',1,2);
            await choose('rate-default',3);
            for (const key of ['ArrowRight','ArrowRight','ArrowLeft','Enter']) await page.keyboard.press(key);
            await choose('rate-fixed',2);
            await choose('rate-disabled',2);
            await choose('rate-readonly',2);
            await choose('rate-no-clear',1);
            await choose('rate-no-keyboard',2);
            await page.keyboard.press('ArrowRight');
            await choose('rate-rtl',1,18);
            await page.keyboard.press('ArrowLeft');
            await page.keyboard.press('ArrowRight');
            await choose('rate-custom',2);
            await page.waitForFunction(()=>document.getElementById('rate-count').textContent === '13');
            capture.interaction = await page.evaluate(()=>({
                count:document.getElementById('rate-count').textContent,
                request:document.getElementById('rate-request').textContent,
                values:Object.fromEntries([...document.querySelectorAll('.ant-rate[id]')].map(node=>[node.id,
                    node.querySelectorAll('.ant-rate-star-full').length + node.querySelectorAll('.ant-rate-star-half').length/2])),
            }));
            const expected = {'rate-default':0,'rate-half':0,'rate-fixed':2,'rate-disabled':3,'rate-readonly':3,
                'rate-no-clear':2,'rate-no-keyboard':3,'rate-small':1,'rate-large':4,'rate-custom':3,'rate-rtl':1.5};
            if (capture.interaction.request !== 'rate-custom:3' || JSON.stringify(capture.interaction.values) !== JSON.stringify(expected))
                throw new Error(`Rate interaction mismatch: ${JSON.stringify(capture.interaction)}`);
        }
        if (fixture === 'tag-modes') {
            for (const selector of ['#tag-disabled .ant-tag-close-icon','#tag-check-disabled','#tag-inherited','#tag-fixed','#tag-check'])
                await page.click(selector);
            await page.keyboard.press('Space');
            await page.click('#tag-retain .ant-tag-close-icon');
            await page.click('#tag-close .ant-tag-close-icon');
            capture.interaction = await page.evaluate(()=>({count:document.getElementById('tag-count').textContent,
                request:document.getElementById('tag-request').textContent,
                checked:document.getElementById('tag-check').getAttribute('aria-checked'),
                fixed:document.getElementById('tag-fixed').getAttribute('aria-checked'),
                retained:getComputedStyle(document.getElementById('tag-retain')).display !== 'none',
                closed:getComputedStyle(document.getElementById('tag-close')).display === 'none'}));
            if(JSON.stringify(capture.interaction)!==JSON.stringify({count:'5',request:'tag-close:close',checked:'false',fixed:'false',retained:true,closed:true}))
                throw new Error(`Tag interaction mismatch: ${JSON.stringify(capture.interaction)}`);
        }
        if (fixture === 'avatar-modes') {
            capture.resizes = [];
            for (const [width,size] of [[600,32],[800,48],[1100,64],[1300,32],[500,24]]) {
                await page.setViewport({width,height:600,deviceScaleFactor:1});
                await page.waitForFunction(size=>document.getElementById('avatar-responsive').getBoundingClientRect().width === size,{},size);
                capture.resizes.push({viewport:{width,height:600,scale:1},geometry:await geometryOf()});
            }
        }
        if (fixture === 'grid-modes') {
            capture.resizes = [];
            for (const width of [800,1100,500]) {
                await page.setViewport({width,height:700,deviceScaleFactor:1});
                // responsive observers commit asynchronously after the viewport notification.
                await page.waitForFunction((centered) => getComputedStyle(document.getElementById('align-row')).alignItems === centered,
                    {}, width >= 992 ? 'flex-end' : width >= 768 ? 'center' : 'flex-start');
                const filename = `grid-modes-${width}.png`;
                const bytes = await page.screenshot({path:path.join(output,filename)});
                capture.resizes.push({filename,viewport:{width,height:700,scale:1},sha256:sha256(bytes),geometry:await geometryOf()});
            }
        }
        if (fixture === 'typography-tokens') {
            capture.edits = [];
            const editingState = async () => ({geometry:await geometryOf(),
                ...await page.$eval('#font-draft',node=>({value:node.value,focus:document.activeElement.id,caret:node.selectionStart}))});
            await page.type('#font-draft','ab');
            capture.edits.push(await editingState());
            await page.click('#font-draft',{clickCount:3});
            await page.keyboard.press('Backspace');
            capture.edits.push(await editingState());
        }
        if (fixture === 'segmented-modes') {
            const select = async (id,index) => {
                const items = await page.$$(`#${id} .ant-segmented-item`);
                await items[index].click();
            };
            await select('seg-block',0);
            if (await page.$eval('#seg-count',node=>node.textContent) !== '0') throw new Error('same Segmented selection emitted a change');
            await select('seg-block',2);
            await page.keyboard.press('ArrowUp');
            await select('seg-fixed',2);
            await select('seg-disabled',2);
            await select('seg-named',2);
            await page.click('#seg-submit');
            capture.interaction = await page.evaluate(()=>({count:document.getElementById('seg-count').textContent,
                request:document.getElementById('seg-request').textContent,entries:document.getElementById('seg-entries').textContent,
                controlledLabel:document.querySelector('#seg-fixed input:checked').closest('label').textContent,
                controlledNativeValue:document.querySelector('#seg-fixed input:checked').value}));
            // the pinned upstream radios omit value; record that native-form difference explicitly.
            if (JSON.stringify(capture.interaction) !== JSON.stringify({count:'4',request:'seg-named:3',entries:'choice:on',controlledLabel:'One',controlledNativeValue:'on'}))
                throw new Error(`Segmented interaction mismatch: ${JSON.stringify(capture.interaction)}`);
        }
        if (fixture === 'composition') {
            await page.type('#draft', ' updated');
            await page.click('#parent-update');
            const state = await page.$eval('#draft', node => node.value);
            if (state !== 'Seed updated') throw new Error(`reference draft lost: ${state}`);
            await page.click('.ant-select');
            await page.waitForSelector('.ant-select-dropdown');
            await page.keyboard.press('Escape');
            await page.waitForSelector('.ant-select-dropdown-hidden');
        }
    }
    if (errors.length) throw new Error(errors.join('\n'));
    const inputs = {};
    for (const name of ['package.json', 'package-lock.json', 'fixtures.jsx', 'capture.mjs'])
        inputs[name] = sha256(await readFile(path.join(directory, name)));
    const provenance = {schema: 1, upstream: {version: '6.6.5', commit: '4a39f54842eade4e565ab336ef6097cd7e723cdd'},
        inputs, browser: {version: await browser.version(), path: executable, sha256: sha256(await readFile(executable))},
        viewport: {width: 800, height: 600, scale: 1},
        font: {path: font, sha256: sha256(fontBytes)},
        faces:faces.map(face=>({path:face.path,weight:face.weight,style:face.style,sha256:sha256(face.bytes)})),
        locale: 'en-US', timezone: 'UTC', motion: false,
        captures, interaction: 'draft retained after parent update; select opened and dismissed with Escape',
        claim: 'AntD reference captures; native correspondence must be measured separately'};
    await writeFile(path.join(output, 'provenance.json'), JSON.stringify(provenance, null, 2) + '\n');
    console.log(`dtna reference: ${captures.length} captures and composition interactions passed`);
} finally {
    if (browser) await browser.close();
    await new Promise(resolve => server.close(resolve));
}
