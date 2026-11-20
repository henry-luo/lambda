const {test} = require('node:test');
const assert = require('node:assert/strict');
const {renderFixtures, rgb} = require('./render_fixture.cjs');

const render = renderFixtures('event-interrupt');

test('host timeout unwinds the remaining event listeners', async () => {
    const {log} = await render('timeout', `<!doctype html><h1>Timeout ownership</h1><script>
        window.addEventListener('load',()=>{while(true){}});
        for(let i=0;i<500;i++) window.addEventListener('load',()=>{});
        </script>`, {...process.env, LAMBDA_JS_EXEC_TIMEOUT_SECONDS: '1'});
    assert.match(log, /JS execution timed out after 1000ms/);
    assert.doesNotMatch(log, /for 'load' threw: Script execution timed out; continuing dispatch/);
});

test('ordinary listener exceptions still allow subsequent listeners', async () => {
    const {image, log} = await render('ordinary-exception', `<!doctype html>
        <style>body{margin:0}#box{width:100px;height:100px;background:red}</style><div id=box></div>
        <script>window.addEventListener('load',()=>{throw new Error('ordinary listener failure')});
        window.addEventListener('load',()=>{document.getElementById('box').style.background='lime'});</script>`);
    assert.match(log, /ordinary listener failure/);
    assert.deepEqual(rgb(image, 50, 50), [0, 255, 0]);
});

test('load propagation stops at Document while direct window dispatch remains observable', async () => {
    for (const bubbles of [false, true]) {
        const {image} = await render('load-path-' + bubbles, `<!doctype html>
            <style>body{margin:0}#box{width:100px;height:100px;background:red}</style><div id=box></div>
            <script>
            const box=document.getElementById('box');
            let windowLoads=0, documentLoads=0, ordinary=0;
            const observeWindow=()=>{windowLoads++};
            const observeDocument=()=>{documentLoads++};
            window.addEventListener('load',observeWindow,true);
            window.addEventListener('load',observeWindow,false);
            document.addEventListener('load',observeDocument,true);
            document.addEventListener('load',observeDocument,false);
            box.dispatchEvent(new Event('load',{bubbles:${bubbles}}));
            let ok=windowLoads===0 && documentLoads===${bubbles ? 2 : 1};
            documentLoads=0;
            document.dispatchEvent(new Event('load',{bubbles:${bubbles}}));
            ok=ok && windowLoads===0 && documentLoads===2;
            documentLoads=0;
            window.dispatchEvent(new Event('load',{bubbles:${bubbles}}));
            ok=ok && windowLoads===2 && documentLoads===0;
            window.addEventListener('path-control',()=>{ordinary++},true);
            window.addEventListener('path-control',()=>{ordinary++},false);
            box.dispatchEvent(new Event('path-control',{bubbles:true}));
            ok=ok && ordinary===2;
            box.style.background=ok?'lime':'red';
            </script>`);
        assert.deepEqual(rgb(image, 50, 50), [0, 255, 0], 'bubbles=' + bubbles);
    }
});
