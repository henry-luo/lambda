const {test} = require('node:test');
const assert = require('node:assert/strict');
const {renderFixtures, rgb} = require('./render_fixture.cjs');

const render = renderFixtures('html-controls');

async function paint(label, html) {
    const {image} = await render(label, `<!doctype html>
        <style>body {background:white} textarea {width:180px;height:65px;font:16px monospace}</style>
        ${html}`);
    return image.data;
}

test('textarea paints its initial text and honors the live value', async () => {
    const empty = await paint('empty', '<textarea></textarea>');
    const initial = await paint('initial', '<textarea>First &amp; second\nAnother line</textarea>');
    const live = await paint('live', `<textarea id="field">Other text</textarea>
        <script>document.getElementById('field').value = 'First & second\\nAnother line';</script>`);
    const cleared = await paint('cleared', `<textarea id="field">Other text</textarea>
        <script>document.getElementById('field').value = '';</script>`);
    assert.equal(initial.equals(empty), false, 'initial text must be visible');
    assert.equal(initial.equals(live), true, 'initial and matching live values must paint identically');
    assert.equal(cleared.equals(empty), true, 'a live empty value must stay empty');
});

test('HTML list type hints match CSS and yield to authored styles', async () => {
    const typed = await paint('list-types', '<ol type="A" start="3" reversed><li>First<li type="i" value="8">Second</ol>');
    const css = await paint('list-types-css', '<ol style="list-style-type:upper-alpha" start="3" reversed><li>First<li style="list-style-type:lower-roman" value="8">Second</ol>');
    const override = await paint('list-type-override', '<style>ol{list-style:lower-roman}</style><ol type="A"><li>First</ol>');
    const roman = await paint('list-type-roman', '<ol style="list-style-type:lower-roman"><li>First</ol>');
    const reverted = await paint('list-type-revert', '<ol type="A" style="list-style-type:revert"><li>First</ol>');
    const decimal = await paint('list-type-decimal', '<ol><li>First</ol>');
    assert.equal(typed.equals(css), true, 'list and item hints must use the specified numbering styles');
    assert.equal(override.equals(roman), true, 'author shorthand must override the hint');
    assert.equal(reverted.equals(decimal), true, 'revert must discard the author-origin hint');
});

test('changing list type after layout updates inherited markers', async () => {
    const changed = await paint('list-type-changed', `<ol id="list" type="a"><li>First</ol>
        <script>const list = document.getElementById('list');
        list.getBoundingClientRect(); list.setAttribute('type', 'I');</script>`);
    const roman = await paint('list-type-upper-roman', '<ol style="list-style-type:upper-roman"><li>First</ol>');
    assert.equal(changed.equals(roman), true, 'retained markers must pick up the new hint');
});

test('non-modal dialog defaults match the centered absolute box', async () => {
    const style = `<style>
        .parent {position:relative;width:200px;height:110px;background:#eee}
        .before {height:20px}
        .sample {width:40px;height:20px;background:red}
        dialog {font-size:20px;color:#123}
    </style>`;
    const markup = open => `<div class="parent"><div class="before"></div>
        <dialog id="panel" ${open ? 'open' : ''}><div class="sample"></div></dialog></div>`;
    const reference = await paint('dialog-css-reference', style + `<style>dialog {
        position:absolute;left:0;right:0;top:auto;bottom:auto;margin:auto;
        width:40px;height:20px;padding:1em;border:3px solid;background:white;
    }</style>` + markup(true));
    const authored = await paint('dialog-open', style + markup(true));
    const live = await paint('dialog-live', style + markup(false) + `<script>
        const panel = document.getElementById('panel');
        panel.getBoundingClientRect();
        panel.setAttribute('open', '');
    </script>`);
    assert.equal(authored.equals(reference), true, 'the UA box must center and use the computed font and color');
    assert.equal(live.equals(reference), true, 'opening after layout must apply the same defaults');
});

test('dialog show and open reflection preserve modal state', async () => {
    const markup = '<div id="result" style="width:30px;height:30px;background:red"></div><dialog id="panel">Contents</dialog>';
    const expected = await paint('dialog-api-reference', markup.replace('background:red', 'background:green'));
    const {image, log} = await render('dialog-api', '<!doctype html>' + markup + `<script>
        const panel = document.getElementById('panel');
        const result = document.getElementById('result');
        let okay = true;
        function check(condition, description) {
            if (condition) return;
            okay = false;
            console.error('Dialog check failed: ' + description);
        }
        function throwsNamed(call, name) {
            try { call(); } catch (error) { return error.name === name; }
            return false;
        }
        check(panel instanceof HTMLDialogElement && !panel.open, 'closed dialog brand');
        check(throwsNamed(() => HTMLDialogElement.prototype.show.call(result), 'TypeError'), 'receiver');
        const detached = document.createElement('dialog');
        check(throwsNamed(() => detached.showModal(), 'InvalidStateError'), 'detached modal');
        detached.show();
        check(detached.open, 'detached non-modal');
        panel.show();
        panel.show();
        check(panel.open && panel.hasAttribute('open') && !panel.matches(':modal'), 'non-modal show');
        check(throwsNamed(() => panel.showModal(), 'InvalidStateError'), 'modal mode switch');
        panel.open = false;
        check(!panel.hasAttribute('open'), 'open reflection');
        panel.showModal();
        check(panel.open && panel.matches(':modal'), 'modal show');
        check(throwsNamed(() => panel.show(), 'InvalidStateError'), 'non-modal mode switch');
        panel.close('result');
        check(!panel.open && !panel.matches(':modal') && panel.returnValue === 'result', 'close state');
        panel.open = true;
        check(panel.open && panel.hasAttribute('open') && !panel.matches(':modal'), 'open reflection sets non-modal visibility');
        panel.open = false;
        if (okay) result.style.background = 'green';
    </script>`);
    assert.equal(image.data.equals(expected), true, log.split('\n')
        .filter(line => /Dialog check failed|exception/.test(line)).join('\n'));
});

test('dialog requestClose cancels synchronously and queues successful close events', async () => {
    const {image, log} = await render('dialog-request-close', `<!doctype html>
        <body style="margin:0"><div id="result" style="width:20px;height:20px;background:red"></div>
        <dialog id="panel" closedby="none" open>Contents</dialog>
        <script>
            const panel = document.getElementById('panel');
            const events = [];
            let valid = true;
            function check(value, message) {
                if (!value) { valid = false; console.error('Close request check failed: ' + message); }
            }
            let veto = true;
            panel.addEventListener('cancel', event => {
                events.push('cancel');
                check(event.isTrusted && event.cancelable && !event.bubbles && !event.composed &&
                    event.target === panel, 'cancel is a trusted dialog event');
                if (veto) {
                    panel.requestClose();
                    event.preventDefault();
                }
            });
            panel.addEventListener('close', event => {
                events.push('close');
                check(event.isTrusted && !event.cancelable && !event.bubbles, 'close event flags');
            });
            panel.returnValue = 'previous';
            panel.requestClose('vetoed');
            check(panel.open && panel.returnValue === 'previous' && events.join(',') === 'cancel',
                'cancellation preserves the open dialog and avoids recursive cancel');
            let converted = false;
            veto = false;
            panel.requestClose({toString() { converted = true; return 'accepted'; }});
            check(converted && !panel.open && panel.returnValue === 'accepted' &&
                events.join(',') === 'cancel,cancel', 'close changes state before the queued close event');
            panel.requestClose('closed');
            const disconnected = document.createElement('dialog');
            disconnected.show();
            disconnected.requestClose();
            check(disconnected.open && panel.returnValue === 'accepted', 'detached and closed dialogs are ignored');

            panel.showModal();
            panel.addEventListener('cancel', () => {
                panel.close('from listener');
                panel.show();
            }, {once:true});
            panel.requestClose('old request');
            check(panel.open && !panel.matches(':modal') && panel.returnValue === 'from listener',
                'the active request does not close a dialog reopened by the cancel handler');
            panel.requestClose(null);
            check(!panel.open && panel.returnValue === 'null', 'an explicit null converts to a DOM string');
            setTimeout(() => {
                check(events.join(',') === 'cancel,cancel,cancel,cancel,close,close,close',
                    'all accepted closes deliver asynchronous events');
                if (valid) document.getElementById('result').style.background = 'green';
            }, 0);
        </script>`);
    assert.deepEqual(rgb(image, 10, 10), [0, 128, 0], log.split('\n')
        .filter(line => /Close request check failed|exception/.test(line)).join('\n'));
});

test('connected shadow content reflows and paints after insertion', async () => {
    const {image, log} = await render('shadow-content', `<!doctype html>
        <style>
            body {margin:0;background:white}
            #markup, #appended, #empty {display:block}
            .probe {display:none}
        </style>
        <div id="markup"></div><custom-host id="appended"></custom-host>
        <div id="empty"><div style="height:30px;background:red"></div></div>
        <script>
            const markup = document.getElementById('markup');
            const appended = document.getElementById('appended');
            const empty = document.getElementById('empty');
            empty.getBoundingClientRect();
            empty.attachShadow({mode:'open'});
            const open = markup.attachShadow({mode:'open'});
            open.innerHTML = '<div class="probe" style="height:20px;background:green"></div>';
            const inner = open.firstChild;
            const closed = appended.attachShadow({mode:'closed'});
            const box = document.createElement('div');
            box.style.cssText = 'height:20px;background:blue;position:relative;z-index:1';
            closed.appendChild(box);
            const detached = document.createElement('div');
            const detachedRoot = detached.attachShadow({mode:'open'});
            if (inner.isConnected && inner.getBoundingClientRect().height === 20 &&
                    inner.getRootNode() === open &&
                    !markup.contains(inner) && appended.shadowRoot === null &&
                    box.isConnected && !detachedRoot.isConnected) {
                document.body.style.background = 'yellow';
            }
        </script>`);
    assert.deepEqual(rgb(image, 10, 10), [0, 128, 0], 'markup in the open root must paint');
    assert.deepEqual(rgb(image, 10, 30), [0, 0, 255], 'appended content in the closed root must paint');
    assert.deepEqual(rgb(image, 10, 50), [255, 255, 0], log.split('\n')
        .filter(line => /exception/.test(line)).join('\n'));
});

test('shadow roots and named slot assignments follow source tree changes', async () => {
    const {image, log} = await render('shadow-slots', `<!doctype html>
        <div id="result" style="width:20px;height:20px;background:red"></div>
        <div id="host"><b slot="name">Named</b>Text<!--ignored--></div>
        <div id="secret"><b>Private</b></div>
        <script>
            const host = document.getElementById('host');
            const result = document.getElementById('result');
            let valid = true;
            function check(value, message) {
                if (!value) { valid = false; console.error('Shadow check failed: ' + message); }
            }
            function throwsNamed(call, name) {
                try { call(); } catch (error) { return error.name === name; }
                return false;
            }
            check(host.shadowRoot === null, 'unattached root');
            check(throwsNamed(() => host.attachShadow({mode:'Open'}), 'TypeError'), 'invalid mode');
            const root = host.attachShadow({mode:'open', delegatesFocus:true});
            check(host.shadowRoot === root && root.host === host && root.mode === 'open' &&
                root.delegatesFocus === true, 'root metadata');
            check(throwsNamed(() => host.attachShadow({mode:'closed'}), 'NotSupportedError') &&
                host.shadowRoot === root, 'an attached root cannot be replaced');
            root.innerHTML = '<slot name="name"></slot><slot name="name"><i></i></slot>' +
                '<slot></slot><slot name="outer"><slot name="inner"><em></em></slot></slot><div></div>';
            const named = root.firstElementChild;
            const duplicate = named.nextElementSibling;
            const unnamed = duplicate.nextElementSibling;
            const outer = unnamed.nextElementSibling;
            const nestedHost = outer.nextElementSibling;
            const light = host.firstElementChild;
            const text = light.nextSibling;
            check(named instanceof HTMLSlotElement && light.assignedSlot === named &&
                text.assignedSlot === unnamed, 'named and default assignment');
            const snapshot = named.assignedNodes();
            check(Array.isArray(snapshot) && snapshot.length === 1 && snapshot[0] === light &&
                named.assignedElements()[0] === light && duplicate.assignedNodes().length === 0 &&
                duplicate.assignedElements({flatten:true})[0] === duplicate.firstElementChild,
                'assigned sequence and first matching slot');
            check(unnamed.assignedNodes().length === 1 && unnamed.assignedNodes()[0] === text &&
                unnamed.assignedElements().length === 0, 'text and comment filtering');
            check(outer.assignedElements({flatten:true})[0] === outer.firstElementChild.firstElementChild,
                'nested fallback flattening');
            root.insertBefore(duplicate, named);
            check(light.assignedSlot === duplicate && named.assignedNodes().length === 0 &&
                snapshot[0] === light, 'moving a slot changes current assignment only');
            light.slot = 'inner';
            check(outer.assignedElements({flatten:true})[0] === light, 'live slottable name change');
            const secret = document.getElementById('secret');
            const closed = secret.attachShadow({mode:'closed'});
            closed.innerHTML = '<slot></slot>';
            check(secret.shadowRoot === null && secret.__shadowRootInternal === undefined &&
                closed.mode === 'closed' && closed.host === secret &&
                secret.firstElementChild.assignedSlot === null &&
                closed.firstElementChild.assignedElements()[0] === secret.firstElementChild,
                'closed root does not leak through the host or slottable');
            const nested = nestedHost.attachShadow({mode:'closed'});
            const nestedText = document.createTextNode('nested');
            nested.appendChild(nestedText);
            check(nestedText.getRootNode() === nested && nestedText.getRootNode({composed:true}) === document &&
                nested.getRootNode({composed:true}) === document && document.getRootNode() === document &&
                light.getRootNode() === document && throwsNamed(() => nestedText.getRootNode(1), 'TypeError'),
                'composed ancestry crosses both roots');
            host.remove();
            check(nestedText.getRootNode({composed:true}) === host && !nestedText.isConnected,
                'detached host is the composed root');
            const orphan = document.createElement('slot');
            orphan.appendChild(document.createTextNode('fallback'));
            check(orphan.assignedNodes({flatten:true}).length === 0 &&
                throwsNamed(() => HTMLSlotElement.prototype.assignedNodes.call(result), 'TypeError'),
                'ordinary slots and brand checks');
            if (valid) result.style.background = 'green';
        </script>`);
    assert.deepEqual(rgb(image, 10, 10), [0, 128, 0], log.split('\n')
        .filter(line => /Shadow check failed|exception/.test(line)).join('\n'));
});

test('assigned blocks and text paint at the slot and fallbacks follow reassignment', async () => {
    const start = `<!doctype html><body style="margin:0;background:white">
        <style>
            .host {padding:5px}
            .placed {width:27px;height:20px;background:green}
            .unassigned {width:60px;height:20px;background:red}
        </style>`;
    const wrapper = '<div style="margin:7px;padding:5px;background:yellow">';
    const nodes = '<div class="placed" slot="named">Text</div><div class="placed" slot="named">Two</div>';
    const reference = await render('slot-paint-reference', start +
        '<div class="host">' + wrapper + nodes + '<div style="color:blue">Fallback text</div></div></div>');
    const {image, log} = await render('slot-paint-assigned', start +
        '<div class="host" id="host">' + nodes + '<div class="unassigned" slot="unused"></div></div>' +
        `<script>
            const host = document.getElementById('host');
            const root = host.attachShadow({mode:'open'});
            root.innerHTML = '<div style="margin:7px;padding:5px;background:yellow">' +
                '<slot name="named"><div class="unassigned"></div></slot>' +
                '<slot name="empty"><div style="color:blue">Fallback text</div></slot></div>';
            const second = host.children[1];
            second.slot = 'unused';
            second.getBoundingClientRect();
            second.slot = 'named';
            const first = host.firstElementChild;
            const box = first.getBoundingClientRect();
            if (box.x === 17 && box.y === 17 && document.elementFromPoint(18, 18) === first)
                console.log('SLOT_GEOMETRY_READY');
        </script>`);
    assert.equal(image.data.equals(reference.image.data), true, 'projected and fallback content must match equivalent paint');
    assert.match(log, /SLOT_GEOMETRY_READY/);
});

test('inline shadow stylesheets stay in their tree through CSSOM and source changes', async () => {
    const {image, log} = await render('shadow-stylesheets', `<!doctype html>
        <body style="margin:0;background:white">
        <div id="result" style="width:20px;height:20px;background:red"></div>
        <div id="left"></div><div id="right"></div>
        <div class="probe" id="outside" style="width:20px;height:20px"></div>
        <script>
            const left = document.getElementById('left').attachShadow({mode:'open'});
            const right = document.getElementById('right').attachShadow({mode:'open'});
            left.innerHTML = '<style>.probe {background:green} * > .probe {background:red}</style>' +
                '<div class="probe" style="width:20px;height:20px"></div>';
            right.innerHTML = '<style>.probe {background:blue}</style>' +
                '<div class="probe" style="width:20px;height:20px"></div>';
            const sheet = left.firstElementChild;
            const leftBox = sheet.nextElementSibling;
            const rightBox = right.firstElementChild.nextElementSibling;
            const outside = document.getElementById('outside');
            const color = element => getComputedStyle(element).backgroundColor;
            let valid = true;
            function check(value, message) {
                if (!value) { valid = false; console.error('Shadow styles check failed: ' + message); }
            }
            check(document.styleSheets.length === 0 && sheet.sheet.ownerNode === sheet &&
                color(leftBox) === 'rgb(0, 128, 0)' && color(rightBox) === 'rgb(0, 0, 255)',
                'independent sheet lists and the shadow boundary');
            const css = sheet.sheet;
            const leftSheets = left.styleSheets;
            const rightSheets = right.styleSheets;
            check(leftSheets instanceof StyleSheetList && leftSheets === left.styleSheets &&
                leftSheets !== rightSheets && leftSheets.length === 1 && leftSheets[0] === css &&
                leftSheets.item(1) === null && leftSheets[1] === undefined,
                'same-object live StyleSheetList with indexed access');
            css.insertRule('.probe {background:cyan}', css.cssRules.length);
            check(sheet.sheet === css && color(leftBox) === 'rgb(0, 255, 255)' &&
                color(rightBox) === 'rgb(0, 0, 255)', 'CSSOM mutation stays local');
            sheet.textContent = '.probe {background:purple}';
            check(color(leftBox) === 'rgb(128, 0, 128)', 'replacement text is immediately visible');
            sheet.media = 'not all';
            check(leftSheets.length === 1 && color(leftBox) === 'rgba(0, 0, 0, 0)',
                'inactive media does not remove a sheet from its list');
            sheet.media = '';
            right.appendChild(sheet);
            check(color(leftBox) === 'rgba(0, 0, 0, 0)' && color(rightBox) === 'rgb(128, 0, 128)' &&
                document.styleSheets.length === 0 && leftSheets.length === 0 && rightSheets.length === 2 &&
                rightSheets[0] === right.firstElementChild.sheet && rightSheets.item(1) === sheet.sheet,
                'moving a sheet changes its source tree and list order');
            document.head.appendChild(sheet);
            check(document.styleSheets.length === 1 && color(outside) === 'rgb(128, 0, 128)' &&
                rightSheets.length === 1 && color(rightBox) === 'rgb(0, 0, 255)',
                'document ownership and shadow isolation');
            const detachedHost = document.createElement('div');
            const detached = detachedHost.attachShadow({mode:'closed'});
            detached.innerHTML = '<style>.probe {background:orange}</style>' +
                '<div class="probe" style="width:20px;height:20px"></div>';
            const detachedSheets = detached.styleSheets;
            check(detachedSheets.length === 0 && detached.firstElementChild.sheet === null,
                'detached shadow style has no associated sheet');
            document.body.appendChild(detachedHost);
            check(detachedSheets === detached.styleSheets && detachedSheets.length === 1 &&
                detachedSheets[0] === detached.firstElementChild.sheet &&
                color(detached.lastElementChild) === 'rgb(255, 165, 0)',
                'connecting a host registers existing styles');
            if (valid) document.getElementById('result').style.background = 'green';
        </script>`);
    assert.deepEqual(rgb(image, 10, 10), [0, 128, 0], log.split('\n')
        .filter(line => /Shadow styles check failed|exception/.test(line)).join('\n'));
    assert.deepEqual(rgb(image, 10, 30), [255, 255, 255]);
    assert.deepEqual(rgb(image, 10, 50), [0, 0, 255]);
    assert.deepEqual(rgb(image, 10, 70), [128, 0, 128]);
    assert.deepEqual(rgb(image, 10, 90), [255, 165, 0]);
});

test('select sizing and its DOM collection use the same wrapped options', async () => {
    const style = '<style>body{margin:0}select{appearance:base-select}</style>';
    const reference = await paint('select-direct-options', style +
        '<select><option>First option<option selected>Selected option</select>');
    const {image, log} = await render('select-wrapped-options', '<!doctype html>' + style +
        `<select id="choices"><div><option>First option<option><span>Selected</span> <strong>option</strong></div></select>
        <script>
            const select = document.getElementById('choices');
            const wrapper = select.firstElementChild;
            const excluded = document.createElement('datalist');
            excluded.appendChild(document.createElement('option'));
            wrapper.appendChild(excluded);
            select.selectedIndex = 1;
            if (select.value !== 'Selected option' ||
                    select.length !== 2 || select.options[1] !== wrapper.children[1] ||
                    select.selectedOptions[0] !== wrapper.children[1]) {
                throw new Error('wrapped options must share the native selection order');
            }
            console.log('WRAPPED_OPTIONS_READY');
        </script>`);
    assert.match(log, /WRAPPED_OPTIONS_READY/);
    assert.equal(image.data.equals(reference), true, 'wrapper markup must preserve native select sizing and paint');
});

test('selectedcontent snapshots option markup and follows script selection', async () => {
    const {log} = await render('select-mirrored-content', `<!doctype html>
        <form id="form"><select id="choices">
          <button><selectedcontent id="mirror"></selectedcontent></button>
          <option value="one" selected><span data-name="one">One</span><script type="application/json">{"hidden":true}</script></option>
          <option value="two"><strong>Two</strong></option>
        </select></form>
        <script>
            const select = document.getElementById('choices');
            const mirror = document.getElementById('mirror');
            const first = select.options[0].firstElementChild;
            function check(condition, label) {
                if (condition) return;
                console.error('Selected content check failed: ' + label);
                throw new Error(label);
            }
            check(mirror instanceof HTMLSelectedContentElement, 'interface');
            check(mirror.children.length === 1 && mirror.firstElementChild !== first &&
                mirror.firstElementChild.getAttribute('data-name') === 'one' &&
                mirror.textContent === 'One', 'initial clone excludes script content');
            first.textContent = 'Changed';
            check(mirror.textContent === 'One', 'independent snapshot');
            select.value = 'one';
            check(mirror.textContent === 'Changed', 'value setter refreshes the snapshot');
            select.selectedIndex = 1;
            check(mirror.firstElementChild.localName === 'strong' && mirror.textContent === 'Two', 'index setter');
            document.getElementById('form').reset();
            check(mirror.textContent === 'Changed', 'form reset');
            select.selectedIndex = -1;
            check(!mirror.hasChildNodes(), 'empty selection clears the mirror');
            console.log('SELECTED_CONTENT_READY');
        </script>`);
    assert.equal(log.includes('SELECTED_CONTENT_READY'), true, log.split('\n')
        .filter(line => /Selected content check failed|exception/.test(line)).join('\n'));
});

test('connected select subtree changes refresh selectedcontent', async () => {
    const {log} = await render('select-content-mutations', `<!doctype html><body><script>
        async function checkChanges() {
            const container = document.createElement('div');
            container.innerHTML = '<select><button><selectedcontent></selectedcontent></button>' +
                '<div><option><span>One</span></option></div>' +
                '<div><option><strong>Two</strong></option></div></select>';
            const select = container.firstElementChild;
            const mirror = select.querySelector('selectedcontent');
            document.body.appendChild(container);
            await Promise.resolve();
            if (mirror.textContent !== 'One') throw new Error('initial connection');
            select.options[0].parentElement.remove();
            await Promise.resolve();
            if (mirror.textContent !== 'Two') throw new Error('wrapped selected option removal');
            const button = mirror.parentElement;
            mirror.remove();
            select.remove(0);
            if (mirror.textContent !== 'Two') throw new Error('detached mirror keeps its snapshot');
            button.appendChild(mirror);
            await Promise.resolve();
            if (mirror.hasChildNodes()) throw new Error('reconnection clears the old snapshot');
            console.log('SELECTED_SUBTREE_READY');
        }
        checkChanges().catch(error => console.error('Selected subtree check failed: ' + error.message));
    </script>`);
    assert.equal(log.includes('SELECTED_SUBTREE_READY'), true, log.split('\n')
        .filter(line => /Selected subtree check failed|exception/.test(line)).join('\n'));
});

test('progress and meter share live numeric values with native paint', async () => {
    const {image, log} = await render('html-gauges', `<!doctype html>
        <style>
          body {margin:0;background:white}
          progress, meter {position:absolute;left:0;width:120px;height:20px}
          #progress {top:0;direction:rtl;accent-color:red}
          #meter {top:24px}
          #waiting {top:48px}
          #vertical {top:72px;width:20px;height:120px;writing-mode:vertical-rl;
            direction:rtl;accent-color:green}
          #plain {top:72px;left:24px;appearance:none;background:purple}
        </style>
        <progress id="progress" max="10"></progress>
        <meter id="meter" min="-10" max="10" low="-2" high="2" optimum="-8"></meter>
        <progress id="waiting"></progress>
        <progress id="vertical" value="0.5"></progress>
        <progress id="plain" value="0.5"></progress>
        <script>
            const progress = document.getElementById('progress');
            const meter = document.getElementById('meter');
            let valid = progress instanceof HTMLProgressElement && meter instanceof HTMLMeterElement &&
                progress.position === -1 && progress.max === 10 && progress.value === 0 &&
                meter.min === -10 && meter.max === 10 && meter.low === -2 &&
                meter.high === 2 && meter.optimum === -8;
            progress.getBoundingClientRect();
            progress.value = 5;
            progress.max = 0;
            meter.value = 7;
            let rejected = false;
            try { meter.value = Infinity; } catch (error) { rejected = error instanceof TypeError; }
            valid = valid && rejected && progress.value === 5 && progress.position === 0.5 &&
                progress.getAttribute('max') === '10' && meter.value === 7 &&
                meter.getAttribute('value') === '7';
            console.log('HTML_GAUGES_READY', valid);
        </script>`);
    assert.match(log, /HTML_GAUGES_READY true/);
    assert.deepEqual(rgb(image, 90, 10), [255, 0, 0], 'right-to-left progress fills from the right');
    assert.deepEqual(rgb(image, 30, 10), [226, 228, 231]);
    assert.deepEqual(rgb(image, 90, 34), [196, 43, 28], 'meter paints the least desirable region');
    assert.deepEqual(rgb(image, 110, 34), [226, 228, 231]);
    assert.deepEqual(rgb(image, 60, 58), [0, 103, 192], 'indeterminate progress has a distinct segment');
    assert.deepEqual(rgb(image, 10, 100), [226, 228, 231]);
    assert.deepEqual(rgb(image, 10, 160), [0, 128, 0], 'vertical right-to-left progress fills from the bottom');
    assert.deepEqual(rgb(image, 40, 82), [128, 0, 128], 'appearance none suppresses native paint');
});

test('form labels resolve the first eligible control and keep a live NodeList', async () => {
    const {image, log} = await render('form-labels', `<!doctype html>
        <div id="result" style="width:20px;height:20px;background:red"></div>
        <label id="external" for="field">External</label><div id="field"></div>
        <template><div id="field"></div></template>
        <form id="form"><label id="wrapper"><input id="field"><button id="other" type="button">Other</button></label>
            <label id="gauge-label" for="gauge">Gauge</label><progress id="gauge" value="1"></progress>
        </form>
        <label id="external-form" for="moved">Outside</label><textarea id="moved" form="form"></textarea>
        <div id="host"></div>
        <script>
            let valid = true;
            function check(value, message) {
                if (!value) { valid = false; console.error('Label check failed: ' + message); }
            }
            const external = document.getElementById('external');
            const blocker = document.getElementById('field');
            const wrapper = document.getElementById('wrapper');
            const field = wrapper.firstElementChild;
            const other = document.getElementById('other');
            const form = document.getElementById('form');
            const labels = field.labels;
            check(external instanceof HTMLLabelElement && external.control === null && external.form === null &&
                labels instanceof NodeList && labels === field.labels && labels.length === 1 &&
                labels.item(0) === wrapper && wrapper.control === field && wrapper.form === form &&
                other.labels.length === 0, 'first matching ID and implicit control');
            blocker.remove();
            check(external.control === field && external.form === form && labels.length === 2 &&
                labels[0] === external && labels[1] === wrapper, 'live order after duplicate ID removal');
            field.type = 'hidden';
            check(field.labels === null && labels.length === 0 && wrapper.control === other &&
                other.labels[0] === wrapper && external.control === null, 'hidden input is not labelable');
            field.type = 'checkbox';
            check(field.labels === labels && labels.length === 2, 'restored labels identity');
            wrapper.htmlFor = '';
            check(wrapper.control === null && labels.length === 1, 'empty explicit for has no implicit fallback');
            wrapper.removeAttribute('for');
            const gauge = document.getElementById('gauge');
            const gaugeLabel = document.getElementById('gauge-label');
            check(gauge.labels[0] === gaugeLabel && gaugeLabel.control === gauge && gaugeLabel.form === null,
                'a gauge is labelable without a form owner');
            const moved = document.getElementById('moved');
            const outside = document.getElementById('external-form');
            check(moved.labels[0] === outside && outside.form === form, 'explicit form owner');
            const detached = document.createElement('div');
            detached.appendChild(wrapper);
            check(labels.length === 1 && labels[0] === wrapper && wrapper.form === null && external.control === null,
                'live list follows detached control');
            const shadow = document.getElementById('host').attachShadow({mode:'open'});
            shadow.innerHTML = '<form id="form"><label for="moved">Inside</label><textarea id="moved"></textarea></form>';
            const shadowForm = shadow.firstElementChild;
            const inside = shadowForm.firstElementChild;
            const local = inside.nextElementSibling;
            check(inside.control === local && local.labels[0] === inside && inside.form === shadowForm &&
                outside.control === moved && moved.labels.length === 1, 'tree-local label and form lookup');
            const read = Object.getOwnPropertyDescriptor(HTMLLabelElement.prototype, 'control').get;
            let brand = false;
            try { read.call(field); } catch (error) { brand = error.name === 'TypeError'; }
            check(brand, 'prototype accessor validates its receiver');
            if (valid) document.getElementById('result').style.background = 'green';
        </script>`);
    assert.deepEqual(rgb(image, 10, 10), [0, 128, 0], log.split('\n')
        .filter(line => /Label check failed|exception/.test(line)).join('\n'));
});

test('label activation forwards an uncanceled click and preserves device metadata', async () => {
    const {image, log} = await render('form-label-click', `<!doctype html>
        <div id="result" style="width:20px;height:20px;background:red"></div>
        <label id="label" for="control"><span id="caption">Caption</span>
            <button type="button"><span id="interactive">Button</span></button>
        </label>
        <input id="control" type="checkbox">
        <label id="implicit"><span>Implicit</span><input id="nested" type="checkbox"></label>
        <script>
            const label = document.getElementById('label');
            const control = document.getElementById('control');
            const caption = document.getElementById('caption');
            const implicit = document.getElementById('implicit');
            const nested = document.getElementById('nested');
            let events = [];
            let changes = 0;
            let valid = true;
            function check(value, message) {
                if (!value) { valid = false; console.error('Label click check failed: ' + message); }
            }
            control.addEventListener('click', event => events.push(event));
            control.addEventListener('change', () => changes++);
            caption.click();
            check(control.checked && changes === 1 && events.length === 1 &&
                events[0].target === control && !events[0].isTrusted, 'forwarded synthetic activation');
            document.getElementById('interactive').click();
            check(control.checked && events.length === 1, 'interactive descendants have their own behavior');
            label.addEventListener('click', event => event.preventDefault(), {once:true});
            label.click();
            check(control.checked && events.length === 1, 'label cancellation suppresses forwarding');
            const source = new PointerEvent('click', {bubbles:true, cancelable:true, detail:3,
                pointerId:12, pointerType:'pen', isPrimary:true, ctrlKey:true, clientX:18, clientY:9});
            caption.dispatchEvent(source);
            const forwarded = events[1];
            check(forwarded && forwarded !== source && forwarded instanceof PointerEvent &&
                forwarded.pointerId === 12 && forwarded.pointerType === 'pen' && forwarded.isPrimary &&
                forwarded.detail === 3 && forwarded.ctrlKey && forwarded.clientX === 18 &&
                !forwarded.isTrusted && source.target === caption, 'pointer data and dispatch target');
            implicit.click();
            check(nested.checked, 'implicit association forwards once');
            nested.click();
            check(!nested.checked, 'a click on the control does not forward again');
            control.disabled = true;
            label.click();
            check(events.length === 2, 'disabled control receives no forwarded click');
            control.disabled = false;
            const fieldset = document.createElement('fieldset');
            fieldset.disabled = true;
            document.body.appendChild(fieldset);
            fieldset.appendChild(control);
            label.click();
            check(events.length === 2, 'disabled fieldset is observed before reflow');
            if (valid) document.getElementById('result').style.background = 'green';
        </script>`);
    assert.deepEqual(rgb(image, 10, 10), [0, 128, 0], log.split('\n')
        .filter(line => /Label click check failed|exception/.test(line)).join('\n'));
});

test('label focus delegates when the label has no focusable area', async () => {
    const {image, log} = await render('label-focus', `<!doctype html>
        <div id="result" style="width:20px;height:20px;background:red"></div>
        <label id="implicit"><input id="first"></label>
        <label id="focusable" for="second" tabindex="-1">Focusable label</label>
        <label id="empty-tabindex" for="second" tabindex="">Empty tabindex</label>
        <label id="hidden" for="second" tabindex="0" style="display:none">Hidden label</label>
        <input id="second"><label id="disabled" for="third">Disabled control</label><input id="third" disabled>
        <script>
            const first = document.getElementById('first');
            const second = document.getElementById('second');
            const focusable = document.getElementById('focusable');
            let valid = true;
            function check(value, message) {
                if (!value) { valid = false; console.error('Label focus check failed: ' + message); }
            }
            document.getElementById('implicit').focus();
            check(document.activeElement === first, 'implicit label focuses its control');
            focusable.focus();
            check(document.activeElement === focusable, 'valid negative tabindex keeps focus on the label');
            document.getElementById('empty-tabindex').focus();
            check(document.activeElement === second, 'empty tabindex delegates');
            first.focus();
            document.getElementById('hidden').focus();
            check(document.activeElement === second, 'hidden label delegates');
            document.getElementById('disabled').focus();
            check(document.activeElement === second, 'disabled target receives no focus');
            if (valid) document.getElementById('result').style.background = 'green';
        </script>`);
    assert.deepEqual(rgb(image, 10, 10), [0, 128, 0], log.split('\n')
        .filter(line => /Label focus check failed|exception/.test(line)).join('\n'));
});

test('datalist options stay live and input list resolution stays in its tree', async () => {
    const {log} = await render('html-datalist', `<!doctype html>
        <div id="suggestions"></div>
        <template><div id="suggestions"></div></template>
        <datalist id="suggestions"><option value="first"></datalist>
        <input id="field" list="suggestions"><div id="host"></div>
        <script>
            const field = document.getElementById('field');
            const blocker = document.getElementById('suggestions');
            const list = document.querySelector('datalist');
            let valid = field.list === null && list instanceof HTMLDataListElement;
            blocker.removeAttribute('id');
            const options = list.options;
            valid = valid && field.list === list && options instanceof HTMLCollection && options.length === 1;
            const extra = document.createElement('option');
            extra.value = 'second';
            list.appendChild(extra);
            list.appendChild(document.createElementNS('http://www.w3.org/2000/svg', 'option'));
            valid = valid && options === list.options && options.length === 2 && options[1] === extra;
            field.type = 'checkbox';
            valid = valid && field.list === null;
            field.type = 'range';
            valid = valid && field.list === list;
            const shadow = document.getElementById('host').attachShadow({mode:'open'});
            shadow.innerHTML = '<input list="suggestions">';
            const shadowInput = shadow.firstElementChild;
            valid = valid && shadowInput.list === null;
            const local = document.createElement('datalist');
            local.id = 'suggestions';
            shadow.appendChild(local);
            valid = valid && shadowInput.list === local && field.list === list;
            console.log('HTML_DATALIST_READY', valid);
        </script>`);
    assert.match(log, /HTML_DATALIST_READY true/);
});
