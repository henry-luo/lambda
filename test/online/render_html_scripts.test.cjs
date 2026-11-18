const {test} = require('node:test');
const assert = require('node:assert/strict');
const {renderFixtures, rgb} = require('./render_fixture.cjs');

const render = renderFixtures('html-scripts');

test('inserted classic scripts run synchronously with their own currentScript', async () => {
    const {image, log} = await render('dynamic-inline', `<!doctype html>
        <div id="result" style="width:20px;height:20px;background:red"></div>
        <script id="driver">
            window.scriptOrder = [];
            window.scriptCount = 0;
            const outer = document.currentScript;
            const script = document.createElement('script');
            script.id = 'inserted';
            script.text = "window.scriptCount++; window.scriptOrder.push(document.currentScript.id);" +
                "queueMicrotask(() => window.scriptOrder.push('microtask'));";
            window.scriptOrder.push('before');
            document.head.appendChild(script);
            const restored = document.currentScript === outer && outer.id === 'driver';
            window.scriptOrder.push('after');
            const synchronous = window.scriptOrder.join(',') === 'before,inserted,after';
            document.body.appendChild(script);
            document.body.appendChild(script.cloneNode(true));
            Promise.resolve().then(() => {
                const valid = restored && synchronous && window.scriptCount === 1 &&
                    window.scriptOrder.join(',') === 'before,inserted,after,microtask';
                if (valid) document.getElementById('result').style.background = 'green';
                else console.error('Dynamic inline failed', restored, synchronous, window.scriptCount, window.scriptOrder.join(','));
            });
        </script>`);
    assert.deepEqual(rgb(image, 10, 10), [0, 128, 0], log.split('\n')
        .filter(line => /Dynamic inline failed|exception/.test(line)).join('\n'));
});

test('script preparation respects atomic insertion, source changes and inert markup', async () => {
    const {image, log} = await render('script-preparation', `<!doctype html>
        <div id="result" style="width:20px;height:20px;background:red"></div><div id="host"></div>
        <script id="parser-empty"></script>
        <script>
            window.scriptOrder = [];
            function make(id, text) {
                const script = document.createElement('script');
                script.id = id;
                script.textContent = text;
                return script;
            }
            const first = make('first', "const pending = document.getElementById('later');" +
                "window.scriptOrder.push(pending.isConnected ? 'connected' : 'detached'); pending.remove();");
            const later = make('later', "window.scriptOrder.push('later');");
            document.body.append(first, later);
            const removedBeforePreparation = window.scriptOrder.join(',') === 'connected' && !later.isConnected;
            document.body.appendChild(later);
            const empty = make('empty', '');
            document.head.appendChild(empty);
            empty.appendChild(document.createTextNode("window.scriptOrder.push('filled');"));
            document.getElementById('parser-empty').textContent = "window.scriptOrder.push('parser-filled');";
            const inert = document.createElement('div');
            inert.innerHTML = '<script>window.scriptOrder.push("inert");<' + '/script>';
            document.head.appendChild(inert.firstElementChild);
            const unsupported = make('plain', "window.scriptOrder.push('plain');");
            unsupported.type = 'text/plain';
            document.head.appendChild(unsupported);
            const detached = make('detached', "window.scriptOrder.push('clone');");
            document.body.appendChild(detached.cloneNode(true));
            const shadow = document.getElementById('host').attachShadow({mode:'open'});
            shadow.appendChild(make('shadow-script', "window.scriptOrder.push(document.currentScript === null ? 'shadow' : 'leaked');"));
            const valid = removedBeforePreparation && window.scriptOrder.join(',') ===
                'connected,later,filled,parser-filled,clone,shadow';
            if (valid) document.getElementById('result').style.background = 'green';
            else console.error('Script preparation failed', removedBeforePreparation, window.scriptOrder.join(','));
        </script>`);
    assert.deepEqual(rgb(image, 10, 10), [0, 128, 0], log.split('\n')
        .filter(line => /Script preparation failed|exception/.test(line)).join('\n'));
});
