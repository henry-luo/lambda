const {test} = require('node:test');
const assert = require('node:assert/strict');
const {renderFixtures, rgb} = require('./render_fixture.cjs');

const render = renderFixtures('event-interrupt');

test('host timeout unwinds the remaining event listeners', async () => {
    const {log} = await render('timeout', `<!doctype html><h1>Timeout ownership</h1><script>
        window.addEventListener('load',()=>{while(true){}});
        for(let i=0;i<500;i++) window.addEventListener('load',()=>{});
        </script>`, {...process.env, LAMBDA_JS_EXEC_TIMEOUT_SECONDS: '1'});
    assert.match(log, /JS execution timed out after 1s/);
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
