const {test} = require('node:test');
const assert = require('node:assert/strict');
const {renderFixtures, rgb} = require('./render_fixture.cjs');

const render = renderFixtures('html-controls');

test('declarative shadow roots attach during navigation and opt-in parsing', async () => {
    const {image, log} = await render('declarative-shadow-roots', `<!doctype html>
        <style>body { margin:0 } .sample { background:red }</style>
        <div id="host"><template shadowrootmode="OPEN" shadowrootserializable
            shadowrootdelegatesfocus shadowrootclonable>
            <style>.sample { background:lime; width:30px; height:30px }</style>
            <div class="sample"><slot name="content"></slot></div>
            <section id="nested" hidden><template shadowrootmode="closed"><em>Hidden</em></template></section>
            <script>window.declarativeScript = 'root'; /* <&> */</script>
        </template><span slot="content" id="light"></span></div>
        <div id="light-sample" class="sample" style="width:30px;height:30px"></div>
        <script>
            function check(value, reason) {
                if (!value) {
                    console.error('Declarative root: ' + reason);
                    throw new Error(reason);
                }
            }
            const host = document.getElementById('host');
            const root = host.shadowRoot;
            check(root instanceof ShadowRoot, 'initial root');
            check(root.mode === 'open' && root.clonable && root.serializable && root.delegatesFocus, 'root options');
            check(host.children.length === 1 && !host.querySelector('template'), 'declaration consumed');
            const slot = root.querySelector('slot');
            check(slot.assignedNodes()[0] === document.getElementById('light'), 'slot projection');
            check(root.querySelector('#nested').shadowRoot === null &&
                !root.querySelector('#nested').querySelector('template'), 'closed nested root');
            check(window.declarativeScript === 'root', 'parser script order');
            check(root.styleSheets.length === 1 && document.styleSheets.length === 1, 'scoped initial sheet');
            const serialized = host.getHTML({serializableShadowRoots:true});
            check(host.getHTML() === host.innerHTML && host.getHTML({shadowRoots:[root]}) === serialized &&
                serialized.startsWith('<template shadowrootmode="open"') && serialized.includes('/* <&> */'),
                'opt-in serialization includes the detached root before light children');
            const clone = host.cloneNode(false);
            check(clone.children.length === 0 && clone.shadowRoot.querySelector('.sample'), 'shallow host clone');
            let rejected = false;
            try { clone.attachShadow({mode:'closed'}); } catch (error) {
                rejected = error.name === 'NotSupportedError';
            }
            check(rejected, 'hydration mode mismatch');
            const original = clone.shadowRoot;
            check(clone.attachShadow({mode:'open'}) === original &&
                original.childNodes.length === 0 && original.delegatesFocus, 'hydration preserves root identity');
            const declaration = '<article><template shadowrootmode="open"><b>value</b></template>' +
                '<template shadowrootmode="closed"><i>duplicate</i></template></article>';
            const detached = document.createElement('div');
            detached.innerHTML = declaration;
            check(detached.firstElementChild.shadowRoot === null, 'innerHTML opt-out');
            detached.setHTMLUnsafe(declaration);
            const article = detached.firstElementChild;
            check(article.shadowRoot.querySelector('b') && article.querySelector('template').content.querySelector('i'),
                'fragment opt-in and duplicate fallback');
            article.shadowRoot.setHTMLUnsafe('<div><template shadowrootmode="open"><u>nested</u></template></div>');
            check(article.shadowRoot.firstElementChild.shadowRoot.querySelector('u'), 'shadow fragment opt-in');
            const parsed = Document.parseHTMLUnsafe(declaration);
            check(parsed.querySelector('article').shadowRoot.querySelector('b'), 'document opt-in');
            check(new DOMParser().parseFromString(declaration, 'text/html').querySelector('article').shadowRoot === null,
                'DOMParser opt-out');
            console.log('DSD_CHECKS_READY');
        </script>`);
    assert.match(log, /DSD_CHECKS_READY/, log.split('\n').filter(line => /Declarative root|exception/i.test(line)).join('\n'));
    assert.deepEqual(rgb(image, 10, 10), [0, 255, 0]);
    assert.deepEqual(rgb(image, 10, 40), [255, 0, 0]);
});

test('modal dialog top layer bypasses ancestor paint and blocks outside interaction', async () => {
    const {image, log} = await render('modal-top-layer', `<!doctype html>
        <style>
            body { margin:0; background:white }
            #outside { position:fixed; inset:0; background:red; z-index:2147483647; border:0 }
            #clip { width:10px; height:10px; overflow:hidden; transform:translate(110px,70px); opacity:.25 }
            dialog { position:absolute; inset:30px auto auto 40px; margin:0; padding:0; border:0;
                     width:60px; height:50px; background:lime; z-index:-10 }
            dialog::backdrop { display:contents; background:blue }
            #note { position:fixed; inset:45px auto auto 55px; margin:0; width:25px;
                    height:25px; border:none; padding:0; background:yellow }
        </style>
        <button id="outside">Outside</button>
        <div id="clip" inert>
            <dialog id="pane"><button id="inside" autofocus>Inside</button><div id="note" popover="manual"></div></dialog>
        </div>
        <dialog id="second"><button id="inside-second" autofocus>Next</button></dialog>
        <script>
            const byId = id => document.getElementById(id);
            const outside = byId('outside'), pane = byId('pane'), inside = byId('inside');
            const second = byId('second'), note = byId('note');
            let valid = true;
            function check(value, reason) {
                if (!value) { valid = false; console.error('Modal top layer: ' + reason); }
            }
            outside.focus();
            pane.showModal();
            check(document.activeElement === inside, 'modal escapes inert ancestor');
            outside.focus();
            check(document.activeElement === inside, 'outside focus is blocked');
            second.showModal();
            check(document.activeElement === byId('inside-second'), 'second modal owns focus');
            inside.focus();
            check(document.activeElement === byId('inside-second'), 'older modal focus is blocked');
            second.close();
            check(document.activeElement === inside, 'closing topmost restores focus to previous modal');
            pane.close();
            check(document.activeElement === outside, 'closing final modal unblocks document');
            pane.showModal();
            note.showPopover();
            requestAnimationFrame(() => {
                check(getComputedStyle(pane, '::backdrop').display === 'block', 'backdrop display is blockified');
                check(document.elementFromPoint(65, 60) === note, 'last top layer entry owns the hit');
                check(document.elementFromPoint(95, 75) === pane, 'dialog owns its principal box');
                check(document.elementFromPoint(10, 90) === pane, 'dialog owns backdrop hit');
                if (valid) console.log('MODAL_TOP_LAYER_READY');
            });
        </script>`);
    assert.match(log, /MODAL_TOP_LAYER_READY/, log.split('\n').filter(line => /Modal top layer|exception/i.test(line)).join('\n'));
    assert.deepEqual(rgb(image, 10, 90), [0, 0, 255]);
    assert.deepEqual(rgb(image, 95, 75), [0, 255, 0]);
    assert.deepEqual(rgb(image, 65, 60), [255, 255, 0]);
});

test('anchor insets, sizing, position areas and popup invokers share layout geometry', async () => {
    const {image, log} = await render('anchor-positioning', `<!doctype html>
        <style>
          body { margin:0 }
          #stage { position:relative; margin:20px; width:300px; height:150px; border:2px solid black }
          #pin { position:absolute; left:25px; top:15px; width:50px; height:30px; anchor-name:--pin }
          #tip { position:absolute; position-anchor:--pin; left:calc(anchor(right) + 5px);
                 top:anchor(bottom); width:anchor-size(); height:14px; background:lime }
          #area { position:absolute; position-anchor:--pin; position-area:bottom;
                  width:30px; height:12px; background:blue }
          #fallback { position:absolute; left:anchor(--missing left, 7px); top:100px;
                      width:anchor-size(--missing width, 13px); height:10px }
          #button { position:absolute; left:150px; top:15px; width:60px; height:30px; box-sizing:border-box }
          #popup { position-anchor:auto; top:anchor(bottom); left:anchor(left);
                   width:anchor-size(width); height:12px; padding:0; margin:0; border:none; background:purple }
        </style>
        <div id="stage">
          <div id="pin"></div><div id="tip"></div><div id="area"></div><div id="fallback"></div>
          <button id="button" command="show-popover" commandfor="popup">Open</button>
          <div id="popup" popover="manual"></div>
        </div>
        <script>
          const byId = id => document.getElementById(id);
          let valid = true;
          function box(id, x, y, width) {
              const rect = byId(id).getBoundingClientRect();
              if (Math.abs(rect.left - x) < .01 && Math.abs(rect.top - y) < .01 &&
                  Math.abs(rect.width - width) < .01) return;
              valid = false;
              console.error('Anchor layout: ' + id + ' got ' + [rect.left, rect.top, rect.width].join(','));
          }
          byId('button').click();
          requestAnimationFrame(() => {
              box('tip', 102, 67, 50);
              box('area', 57, 67, 30);
              box('fallback', 29, 122, 13);
              box('popup', 172, 67, 60);
              byId('pin').style.left = '45px';
              requestAnimationFrame(() => {
                  box('tip', 122, 67, 50);
                  box('area', 77, 67, 30);
                  if (valid) console.log('ANCHOR_LAYOUT_READY');
              });
          });
        </script>`);
    assert.match(log, /ANCHOR_LAYOUT_READY/, log.split('\n').filter(line => /Anchor layout|anchor-positioning|exception/i.test(line)).join('\n'));
    assert.deepEqual(rgb(image, 125, 72), [0, 255, 0]);
    assert.deepEqual(rgb(image, 80, 72), [0, 0, 255]);
    assert.deepEqual(rgb(image, 177, 72), [128, 0, 128]);
});

test('a fixed anchored box follows its default anchor through retained scrolling', async () => {
    const {image, log} = await render('anchor-scroll', `<!doctype html>
        <style>
            body { margin:0 }
            #scroll { position:absolute; inset:10px auto auto 10px; overflow:auto;
                      width:100px; height:100px }
            #content { height:220px; padding-top:60px }
            #pin { width:20px; height:20px; anchor-name:--pin }
            #tip { position:fixed; position-anchor:--pin; top:anchor(bottom); left:anchor(right);
                   width:30px; height:15px; background:lime }
        </style>
        <div id="scroll"><div id="content"><div id="pin"></div></div></div><div id="tip"></div>
        <script>
            const tip = document.getElementById('tip'), scroll = document.getElementById('scroll');
            const before = tip.getBoundingClientRect();
            scroll.scrollTop = 35;
            requestAnimationFrame(() => {
                const after = tip.getBoundingClientRect();
                if (before.top !== 90 || after.top !== 55 || after.left !== 30 ||
                    document.elementFromPoint(40, 60) !== tip) {
                    console.error('Anchor scroll: ' + [before.top, after.top, after.left, scroll.scrollTop]);
                    return;
                }
                tip.style.top = '55px';
                tip.style.left = '30px';
                tip.getBoundingClientRect();
                scroll.scrollTop = 60;
                requestAnimationFrame(() => {
                    if (tip.getBoundingClientRect().top === 55) console.log('ANCHOR_SCROLL_READY');
                    else console.error('Anchor scroll continued after the queries were removed');
                });
            });
        </script>`);
    assert.match(log, /ANCHOR_SCROLL_READY/, log.split('\n').filter(line => /Anchor scroll|exception/i.test(line)).join('\n'));
    assert.deepEqual(rgb(image, 40, 60), [0, 255, 0]);
    assert.notDeepEqual(rgb(image, 40, 95), [0, 255, 0]);
});

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

test('dialog form submission shares cancellation, value reflection and the queued close event', async () => {
    const {log} = await render('dialog-form-submission', `<!doctype html>
        <dialog id="dialog"><form method="dialog" id="form">
            <input id="required" required><button id="button">Confirm</button>
            <div id="host"></div>
        </form></dialog>
        <button id="external" form="form" formmethod="DiAlOg" value="outside">Outside</button>
        <script>
            const dialog = document.getElementById('dialog');
            const form = document.getElementById('form');
            const required = document.getElementById('required');
            const button = document.getElementById('button');
            const external = document.getElementById('external');
            const submits = [];
            let closes = 0, cancel = false, trusted = true;
            form.addEventListener('submit', event => {
                submits.push(event.submitter && event.submitter.id);
                if (cancel) event.preventDefault();
            });
            dialog.addEventListener('close', event => {
                closes++;
                trusted = trusted && event.isTrusted && !event.bubbles && !event.cancelable;
            });
            dialog.returnValue = 'keep';
            dialog.show();
            form.requestSubmit(button);
            const checks = {invalid:dialog.open && submits.length === 0};
            button.formNoValidate = true;
            cancel = true;
            form.requestSubmit(button);
            checks.canceled = dialog.open && dialog.returnValue === 'keep' && submits.length === 1;
            cancel = false;
            button.value = 'chosen';
            button.click();
            checks.clicked = !dialog.open && dialog.returnValue === 'chosen' &&
                button.getAttribute('value') === 'chosen';
            dialog.show();
            form.submit();
            checks.direct = !dialog.open && dialog.returnValue === 'chosen' && submits.length === 2;
            required.value = 'valid';
            form.method = 'post';
            dialog.show();
            form.requestSubmit(external);
            checks.override = !dialog.open && dialog.returnValue === 'outside';
            dialog.show();
            const shadow = document.getElementById('host').attachShadow({mode:'open'});
            shadow.innerHTML = '<form method="dialog"><button>Shadow submit</button></form>';
            shadow.firstElementChild.requestSubmit();
            checks.scope = dialog.open && dialog.returnValue === 'outside';
            dialog.close();
            checks.unqueued = closes === 0 && submits.join(',') === 'button,button,external';
            setTimeout(() => {
                checks.closeEvents = closes === 4 && trusted;
                console.log('DIALOG_FORM', JSON.stringify(checks));
            }, 0);
        </script>`);
    const match = log.match(/DIALOG_FORM (\{[^\n]+\})/);
    assert(match, log.slice(-3000));
    assert.deepEqual(JSON.parse(match[1]), {invalid:true,canceled:true,clicked:true,direct:true,
        override:true,scope:true,unqueued:true,closeEvents:true});
});

test('submit and formdata events share submission state and snapshot listener edits', async () => {
    const {log} = await render('formdata-events', `<!doctype html>
        <button id="outside" form="form" name="earlier" value="outside"></button>
        <dialog id="dialog" open><form id="form" method="get">
            <fieldset disabled><legend><input name="first" value="a"></legend>
                <input name="ignored" value="b"></fieldset>
            <button id="button" name="middle" value="clicked"></button>
            <input name="last" value="c"><input id="required">
        </form></dialog>
        <script>
            const form = document.getElementById('form');
            const dialog = document.getElementById('dialog');
            const button = document.getElementById('button');
            const required = document.getElementById('required');
            const outside = document.getElementById('outside');
            const checks = {}, order = [];
            let eventData, recursionError, submitter, brands = true;
            let cancel = false, invalidCount = 0;
            required.oninvalid = () => {
                if (++invalidCount === 1) outside.click();
            };
            form.addEventListener('submit', event => {
                order.push('submit');
                submitter = event.submitter;
                brands = brands && event instanceof SubmitEvent && event instanceof Event &&
                    event.isTrusted && event.bubbles && event.cancelable && !event.composed;
                if (order.length === 1) form.requestSubmit(button);
                if (cancel) event.preventDefault();
            });
            document.addEventListener('formdata', event => {
                order.push('formdata');
                brands = brands && event instanceof FormDataEvent && event instanceof Event &&
                    event.isTrusted && event.bubbles && !event.cancelable && !event.composed;
                if (order.length === 2) {
                    form.submit();
                    try { new FormData(form); } catch (error) { recursionError = error.name; }
                }
                form.method = 'dialog';
                eventData = event.formData;
                eventData.set('first', 'edited');
                eventData.append('event', 'last');
            });
            required.required = true;
            button.click();
            checks.invalid = invalidCount === 1 && dialog.open && order.length === 0;
            required.required = false;
            button.click();
            checks.activation = !dialog.open && dialog.returnValue === 'clicked' && submitter === button &&
                order.join() === 'submit,formdata' && recursionError === 'InvalidStateError';
            checks.entries = JSON.stringify([...eventData]) ===
                '[["first","edited"],["middle","clicked"],["last","c"],["event","last"]]';
            const data = new FormData(form, outside);
            eventData.set('first', 'later');
            checks.snapshot = data !== eventData && JSON.stringify([...data]) ===
                '[["earlier","outside"],["first","edited"],["last","c"],["event","last"]]';

            dialog.show();
            cancel = true;
            form.requestSubmit(null);
            checks.cancel = dialog.open && submitter === null &&
                order.join() === 'submit,formdata,formdata,submit';
            cancel = false;
            form.submit();
            checks.direct = !dialog.open && order.join() === 'submit,formdata,formdata,submit,formdata';
            checks.brand = brands;
            const custom = new SubmitEvent('check', {submitter:button, composed:true});
            const payload = new FormDataEvent('check', {formData:data});
            const getter = Object.getOwnPropertyDescriptor(SubmitEvent.prototype, 'submitter').get;
            let badReceiver = false;
            try { getter.call(new Event('check')); } catch (error) { badReceiver = error.name === 'TypeError'; }
            checks.constructors = custom.submitter === button && custom.composed && !custom.isTrusted &&
                new SubmitEvent('check').submitter === null && payload.formData === data && badReceiver;
            console.log('FORM_SUBMISSION_EVENTS', JSON.stringify(checks));
        </script>`);
    const match = log.match(/FORM_SUBMISSION_EVENTS (\{[^\n]+\})/);
    assert(match, log.slice(-3000));
    assert.deepEqual(JSON.parse(match[1]), {invalid:true,activation:true,entries:true,snapshot:true,
        cancel:true,direct:true,brand:true,constructors:true});
});

test('GET submission replaces the action query and preserves its fragment', async () => {
    const {log} = await render('form-get-url', `<!doctype html>
        <base href="forms-scope/" target="base-frame">
        <iframe name="base-frame" id="base-frame" srcdoc=""></iframe>
        <iframe name="button-frame" id="button-frame" srcdoc=""></iframe>
        <form id="form" action="fallback.html?stale=yes#fallback">
            <input name="field" value="a b">
            <button id="button" name="send" value="yes"
                formaction="landing.html?stale=yes#fragment" formtarget="button-frame">Send</button>
        </form>
        <form id="empty" action="empty.html?stale=yes#last"></form>
        <script>
            const form = document.getElementById('form');
            const button = document.getElementById('button');
            const buttonFrame = document.getElementById('button-frame');
            const baseFrame = document.getElementById('base-frame');
            const empty = document.getElementById('empty');
            const checks = {};
            const suffix = '/forms-scope/landing.html?field=a+b&send=yes&line%0D%0Abreak=a%0D%0Ab%0D%0Ac&attachment=a+b.txt#fragment';
            form.addEventListener('formdata', event => {
                event.formData.append('line\\nbreak', 'a\\rb\\nc');
                event.formData.append('attachment', new File(['contents'], 'a b.txt'));
            });
            button.click();
            checks.activation = buttonFrame.contentWindow.location.href.endsWith(suffix);
            button.setAttribute('formaction', 'other.html?replace=1#next');
            form.requestSubmit(button);
            checks.request = buttonFrame.contentWindow.location.href.endsWith(
                suffix.replace('landing.html', 'other.html').replace('#fragment', '#next'));
            form.submit();
            checks.direct = baseFrame.contentWindow.location.href.endsWith(
                '/forms-scope/fallback.html?field=a+b&line%0D%0Abreak=a%0D%0Ab%0D%0Ac&attachment=a+b.txt#fallback');
            empty.submit();
            checks.empty = baseFrame.contentWindow.location.href.endsWith('/forms-scope/empty.html?#last') &&
                baseFrame.contentWindow.location.search === '';
            button.setAttribute('formaction', '');
            form.requestSubmit(button);
            checks.emptyAction = !buttonFrame.contentWindow.location.pathname.includes('/forms-scope/') &&
                buttonFrame.contentWindow.location.search.startsWith('?field=a+b&send=yes');
            console.log('FORM_GET_URL', JSON.stringify(checks));
        </script>`);
    const match = log.match(/FORM_GET_URL (\{[^\n]+\})/);
    assert(match, log.slice(-3000));
    assert.deepEqual(JSON.parse(match[1]), {activation:true,request:true,direct:true,empty:true,emptyAction:true});
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

test('dialog and popover toggles cancel, coalesce and retarget their source', async () => {
    const {log} = await render('html-toggle-events', `<!doctype html>
        <dialog id="first">First</dialog><dialog id="second">Second</dialog>
        <button type="button" id="invoker" popovertarget="popover">Menu</button>
        <div id="popover" popover>Popover</div><div id="host"></div>
        <div id="ancestor" popover><dialog id="inside">Inside</dialog>
            <div id="unrelated" popover>Unrelated</div></div>
        <div id="manual" popover="manual">Manual</div>
        <script>
            const first = document.getElementById('first');
            const second = document.getElementById('second');
            const popover = document.getElementById('popover');
            const invoker = document.getElementById('invoker');
            const host = document.getElementById('host');
            const checks = {}, pending = [];
            let data;
            checks.closedGeometry = popover.offsetWidth === 0 && popover.offsetHeight === 0 &&
                popover.getClientRects().length === 0;
            const emptyBox = document.createElement('div');
            emptyBox.style.cssText = 'width:0;height:0';
            document.body.append(emptyBox);
            checks.zeroBox = emptyBox.getClientRects().length === 1;
            emptyBox.remove();
            checks.detachedBox = emptyBox.getClientRects().length === 0;
            first.addEventListener('beforetoggle', event => {
                checks.cancelable = !first.open && event instanceof ToggleEvent &&
                    event.isTrusted && event.cancelable && event.oldState === 'closed' &&
                    event.newState === 'open' && event.source === null;
                event.preventDefault();
            }, {once:true});
            first.showModal();
            checks.canceled = !first.open;
            const listen = node => node.addEventListener('toggle', event => {
                pending.push(node.id + ':' + event.oldState + '>' + event.newState);
            });
            listen(first);
            listen(second);
            first.show();
            second.show();
            first.addEventListener('beforetoggle', event => {
                event.preventDefault();
                checks.closing = first.open && !event.cancelable && event.oldState === 'open' &&
                    event.newState === 'closed';
            }, {once:true});
            first.close('result');
            checks.synchronous = !first.open && first.returnValue === 'result' && pending.length === 0;
            popover.addEventListener('beforetoggle', event => {
                checks.popover = event.source === invoker && event.cancelable &&
                    !popover.matches(':popover-open');
            }, {once:true});
            popover.addEventListener('toggle', event => data = event, {once:true});
            invoker.click();
            const shadow = host.attachShadow({mode:'open'});
            shadow.innerHTML = '<button>Source</button>';
            const source = shadow.firstElementChild;
            const probe = new ToggleEvent('probe', {source, bubbles:true, composed:true});
            source.addEventListener('probe', event => checks.internal = event.source === source);
            host.addEventListener('probe', event => checks.external = event.source === host);
            source.dispatchEvent(probe);
            checks.after = probe.source === host;
            setTimeout(() => {
                checks.order = pending.join(',') === 'second:closed>open,first:closed>closed';
                checks.payload = data instanceof ToggleEvent && data.source === invoker &&
                    data.newState === 'open' && !data.bubbles && !data.cancelable;
                const ancestor = document.getElementById('ancestor');
                const inside = document.getElementById('inside');
                const unrelated = document.getElementById('unrelated');
                const manual = document.getElementById('manual');
                ancestor.showPopover();
                unrelated.showPopover();
                manual.showPopover();
                inside.show();
                checks.dialogPopover = ancestor.matches(':popover-open') &&
                    !unrelated.matches(':popover-open') && manual.matches(':popover-open');
                const reflected = document.createElement('div');
                document.body.append(reflected);
                reflected.popover = 'invalid';
                const invalidDefault = reflected.popover === 'manual' &&
                    reflected.getAttribute('popover') === 'invalid';
                let optionSource;
                reflected.addEventListener('beforetoggle', event => optionSource = event.source, {once:true});
                reflected.showPopover({source:invoker});
                checks.invokerOption = optionSource === invoker && reflected.matches(':popover-open');
                checks.force = reflected.togglePopover({force:true}) === true &&
                    reflected.togglePopover(false) === false && !reflected.matches(':popover-open');
                reflected.showPopover();
                let closes = 0;
                reflected.addEventListener('beforetoggle', event => {
                    if (event.newState === 'closed') {
                        closes++;
                        reflected.popover = 'hint';
                    }
                });
                reflected.popover = 'manual';
                checks.sameMode = reflected.matches(':popover-open') && closes === 0;
                reflected.popover = 'auto';
                checks.modeChange = !reflected.matches(':popover-open') && closes === 1 &&
                    reflected.popover === 'hint';
                reflected.popover = undefined;
                checks.reflect = invalidDefault && reflected.popover === null &&
                    !reflected.hasAttribute('popover');
                reflected.popover = '';
                reflected.showPopover();
                reflected.removeAttribute('popover');
                checks.removedMode = !reflected.matches(':popover-open') && closes === 2;
                console.log('TOGGLE_EVENTS', JSON.stringify(checks));
            }, 0);
        </script>`);
    const match = log.match(/TOGGLE_EVENTS (\{[^\n]+\})/);
    assert(match, log.slice(-3000));
    assert.deepEqual(JSON.parse(match[1]), {cancelable:true,canceled:true,closing:true,
        synchronous:true,popover:true,internal:true,external:true,after:true,order:true,payload:true,
        dialogPopover:true,invokerOption:true,force:true,sameMode:true,modeChange:true,reflect:true,
        removedMode:true,
        closedGeometry:true,zeroBox:true,detachedBox:true});
});

test('automatic and hint popovers preserve ancestors and close dependents', async () => {
    const {log} = await render('html-popover-stacks', `<!doctype html>
        <div id="parent" popover><button popovertarget="child">Open child</button>
            <div id="hint" popover="hint"><div id="under-hint" popover></div></div>
        </div>
        <div id="child" popover></div><div id="other" popover></div>
        <div id="manual" popover="manual"></div>
        <button id="outside" popovertarget="other">Open other</button>
        <script>
            const [parent, child, other, hint, hintChild, manual, outside] =
                ['parent', 'child', 'other', 'hint', 'under-hint', 'manual', 'outside']
                    .map(id => document.getElementById(id));
            const isOpen = el => el.matches(':popover-open');
            const order = [], checks = {};
            parent.showPopover();
            parent.firstElementChild.click();
            hint.showPopover();
            hintChild.showPopover();
            manual.showPopover();
            checks.ancestors = [parent, child, hint, hintChild, manual].every(isOpen);
            [parent, child, hint, hintChild].forEach(el => el.addEventListener('beforetoggle', event => {
                if (event.newState === 'closed') order.push(el.id);
            }));
            parent.hidePopover();
            checks.closeOrder = order.join(',') === 'under-hint,hint,child,parent' &&
                [parent, child, hint, hintChild].every(el => !isOpen(el)) && isOpen(manual);
            parent.showPopover();
            parent.firstElementChild.click();
            parent.remove();
            checks.removeDependent = !isOpen(child) && !isOpen(parent);
            document.body.prepend(parent);
            for (const native of [false, true]) {
                other.popover = 'auto';
                parent.showPopover();
                parent.addEventListener('beforetoggle', () => {other.popover = 'manual'}, {once:true});
                let threw = false;
                try {
                    if (native) outside.click();
                    else other.showPopover();
                } catch (error) {threw = error.name === 'InvalidStateError'}
                checks[native ? 'nativeModeChange' : 'apiModeChange'] =
                    threw === !native && !isOpen(parent) && !isOpen(other) && other.popover === 'manual';
            }
            parent.addEventListener('beforetoggle', () => {
                try {child.showPopover()} catch (error) {
                    checks.reentrant = error.name === 'InvalidStateError';
                }
            }, {once:true});
            parent.showPopover();
            checks.reentrant = checks.reentrant && isOpen(parent) && !isOpen(child);
            console.log('POPOVER_STACKS', JSON.stringify(checks));
        </script>`);
    const match = log.match(/POPOVER_STACKS (\{[^\n]+\})/);
    assert(match, log.slice(-3000));
    assert.deepEqual(JSON.parse(match[1]), {ancestors:true,closeOrder:true,removeDependent:true,
        apiModeChange:true,nativeModeChange:true,reentrant:true});
});

test('ProgressEvent keeps a branded payload through synthetic and file reader dispatch', async () => {
    const {log} = await render('progress-event', `<!doctype html><script>
        const order = [], seen = [];
        const inheritance = [Object.getPrototypeOf(ProgressEvent) === Event,
            Object.getPrototypeOf(ProgressEvent.prototype) === Event.prototype];
        class Sample extends ProgressEvent {}
        const probe = new Sample('probe', {
            get bubbles() { order.push('bubbles'); return true; },
            get lengthComputable() { order.push('lengthComputable'); return 1; },
            get loaded() { order.push('loaded'); return '-1.25'; },
            get total() { order.push('total'); return 5.5; },
        });
        let badNumber = false, badReceiver = false;
        try { new ProgressEvent('probe', {total:Infinity}); }
        catch (err) { badNumber = err instanceof TypeError; }
        const getLoaded = Object.getOwnPropertyDescriptor(ProgressEvent.prototype, 'loaded').get;
        try { getLoaded.call(new Event('probe')); }
        catch (err) { badReceiver = err instanceof TypeError; }
        document.addEventListener('probe', event => seen.push([
            event instanceof Sample, event instanceof Event, event.bubbles, event.isTrusted,
            event.lengthComputable, event.loaded, event.total, Object.prototype.toString.call(event)
        ]));
        document.dispatchEvent(probe);
        const reader = new FileReader();
        reader.addEventListener('loadend', event => console.log('PROGRESS_TYPES', JSON.stringify({
            order, seen, badNumber, badReceiver, inheritance,
            read:[event instanceof ProgressEvent, event.target === reader, event.isTrusted,
                event.lengthComputable, event.loaded, event.total]
        })));
        reader.readAsDataURL(new Blob(['sample']));
    </script>`);
    const match = log.match(/PROGRESS_TYPES (\{[^\n]+\})/);
    assert(match, log.slice(-3000));
    assert.deepEqual(JSON.parse(match[1]), {
        order:['bubbles', 'lengthComputable', 'loaded', 'total'],
        seen:[[true, true, true, false, true, -1.25, 5.5, '[object ProgressEvent]']],
        badNumber:true, badReceiver:true, inheritance:[true, true],
        read:[true, true, true, true, 6, 6],
    });
});

test('button commands preserve form precedence, cancellation and shadow sources', async () => {
    const {log} = await render('button-commands', `<!doctype html>
        <div id="host"></div><dialog id="dialog"></dialog>
        <div id="popup" popover></div>
        <input id="counter" type="number" value="0" oncommand="this.value = Number(this.value) + 1">
        <input id="text" type="text">
        <form id="form"><button id="in-form" command="--increment" commandfor="counter"></button></form>
        <script>
            const [host, dialog, popup, counter, form, formButton] =
                ['host','dialog','popup','counter','form','in-form'].map(id => document.getElementById(id));
            const shadow = host.attachShadow({mode:'closed'});
            const button = shadow.appendChild(document.createElement('button'));
            const checks = {};
            const text = document.getElementById('text');
            text.value = 42;
            checks.textValue = text.value === '42';
            text.value = null;
            checks.nullValue = text.value === '';
            button.command = '--increment';
            button.commandForElement = counter;
            counter.addEventListener('command', event => checks.custom = event instanceof CommandEvent &&
                event.isTrusted && event.cancelable && event.source === host && event.command === '--increment');
            button.click();
            formButton.click();
            checks.inlineAndAuto = counter.valueAsNumber === 1;
            form.addEventListener('submit', event => { event.preventDefault(); checks.form = event.submitter === formButton; });
            formButton.type = 'submit';
            formButton.click();
            checks.noExtraCommand = counter.valueAsNumber === 1;
            button.setAttribute('commandfor', '');
            checks.cleared = button.commandForElement === null;
            button.commandForElement = dialog;
            button.command = 'SHOW-MODAL';
            dialog.addEventListener('command', event => event.preventDefault(), {once:true});
            button.click();
            checks.canceled = !dialog.open;
            const sources = [];
            dialog.addEventListener('beforetoggle', event => sources.push(event.source));
            button.click();
            checks.modal = dialog.matches(':modal') && button.command === 'show-modal';
            button.command = 'request-close';
            button.value = 'done';
            button.click();
            checks.closed = !dialog.open && dialog.returnValue === 'done' && sources.length === 2 &&
                sources.every(source => source === host);
            button.commandForElement = popup;
            button.command = 'toggle-popover';
            button.click();
            checks.popup = popup.matches(':popover-open');
            button.click();
            checks.hidden = !popup.matches(':popover-open');
            console.log('BUTTON_COMMANDS', JSON.stringify(checks));
        </script>`);
    const match = log.match(/BUTTON_COMMANDS (\{[^\n]+\})/);
    assert(match, log.slice(-3000));
    assert.deepEqual(JSON.parse(match[1]), {textValue:true, nullValue:true, custom:true, inlineAndAuto:true, form:true,
        noExtraCommand:true, cleared:true, canceled:true, modal:true, closed:true, popup:true, hidden:true});
});

test('opening a popover focuses its autofocus control through the native focus path', async () => {
    const {log} = await render('html-popover-autofocus', `<!doctype html>
        <button id="before">Before</button><button id="outside">Outside</button>
        <div id="popup" popover><button id="disabled" disabled></button>
            <button id="chosen">Autofocus</button></div>
        <script>
            const before = document.getElementById('before');
            const outside = document.getElementById('outside');
            const popup = document.getElementById('popup');
            const chosen = document.getElementById('chosen');
            const order = [];
            before.focus();
            document.getElementById('disabled').autofocus = true;
            chosen.autofocus = true;
            popup.showPopover();
            const focused = document.activeElement === chosen;
            popup.hidePopover();
            const restored = document.activeElement === before;
            popup.showPopover();
            outside.focus();
            popup.hidePopover();
            const outsideKeptFocus = document.activeElement === outside;
            console.log('POPOVER_FOCUS_RETURN', focused, restored, outsideKeptFocus);
            before.focus();
            before.addEventListener('blur', event => {
                order.push('blur:' + (event.relatedTarget === chosen));
            });
            chosen.addEventListener('focus', event => {
                order.push('focus:' + (document.activeElement === chosen));
                popup.remove();
            });
            popup.addEventListener('beforetoggle', event => order.push(event.newState));
            popup.addEventListener('toggle', event => {
                if (event.newState !== 'open') return;
                order.push(event.oldState + '>' + event.newState);
                console.log('POPOVER_AUTOFOCUS', order.join(','));
            });
            // Let the other visibility transitions deliver their queued task first.
            setTimeout(() => popup.showPopover(), 0);
        </script>`);
    assert.match(log, /POPOVER_FOCUS_RETURN true true true/);
    assert.match(log, /POPOVER_AUTOFOCUS open,blur:true,focus:true,closed>open/);
});

test('dialog opening chooses initial focus and closing restores it when appropriate', async () => {
    const {log} = await render('html-dialog-focus', `<!doctype html>
        <button id="before">Before</button><button id="other">Other</button>
        <dialog id="dialog"><button id="first">First</button><button id="chosen">Chosen</button></dialog>
        <dialog id="empty">Empty dialog</dialog>
        <script>
            const [before, other, dialog, first, chosen, empty] =
                ['before','other','dialog','first','chosen','empty'].map(id => document.getElementById(id));
            const focusIs = element => document.activeElement === element;
            before.focus();
            chosen.autofocus = true;
            dialog.showModal();
            const autofocus = focusIs(chosen);
            dialog.close();
            const modalRestore = focusIs(before);
            chosen.autofocus = false;
            dialog.show();
            const firstControl = focusIs(first);
            other.focus();
            dialog.close();
            const outsideKeptFocus = focusIs(other);
            empty.showModal();
            const dialogFocus = focusIs(empty);
            empty.close();
            const emptyRestore = focusIs(other);
            console.log('DIALOG_FOCUS', autofocus, modalRestore, firstControl,
                outsideKeptFocus, dialogFocus, emptyRestore);
        </script>`);
    assert.match(log, /DIALOG_FOCUS true true true true true true/);
});

test('activeElement follows nested shadow boundaries in the document and roots', async () => {
    const {log} = await render('html-shadow-active-element', `<!doctype html>
        <div id="host"></div><div id="other"></div>
        <script>
            const host = document.getElementById('host');
            const outer = host.attachShadow({mode:'open'});
            outer.innerHTML = '<div></div>';
            const innerHost = outer.firstElementChild;
            const inner = innerHost.attachShadow({mode:'closed'});
            inner.innerHTML = '<button>Focus</button>';
            const target = inner.firstElementChild;
            const unrelated = document.getElementById('other').attachShadow({mode:'open'});
            target.focus();
            console.log('SHADOW_ACTIVE_ELEMENT', document.activeElement === host,
                outer.activeElement === innerHost, inner.activeElement === target,
                unrelated.activeElement === null, host.matches(':focus-within'),
                innerHost.matches(':focus-within'));
        </script>`);
    assert.match(log, /SHADOW_ACTIVE_ELEMENT true true true true true true/, log.slice(-2400));
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
            const fallback = root.querySelector('slot').firstElementChild;
            const hiddenBox = node => {
                const box = node.getBoundingClientRect();
                return box.x === 0 && box.y === 0 && box.width === 0 && box.height === 0 &&
                    node.getClientRects().length === 0;
            };
            second.slot = 'unused';
            const suppressed = hiddenBox(second) && hiddenBox(fallback);
            second.slot = 'named';
            const first = host.firstElementChild;
            const box = first.getBoundingClientRect();
            if (suppressed && box.x === 17 && box.y === 17 && document.elementFromPoint(18, 18) === first)
                console.log('SLOT_GEOMETRY_READY');
        </script>`);
    assert.equal(image.data.equals(reference.image.data), true, 'projected and fallback content must match equivalent paint');
    assert.match(log, /SLOT_GEOMETRY_READY/);
});

test('slot redistribution preserves collapsed margins and replaces obsolete fallback boxes', async () => {
    const {image, log} = await render('slot-margin', `<!doctype html>
        <style>body{margin:0;background:white}.box{width:20px;height:15px;background:green}</style>
        <div id="host"><div class="box" slot="primary"></div></div>
        <script>
            const host = document.getElementById('host');
            const root = host.attachShadow({mode:'open'});
            root.innerHTML = '<div style="margin:9px;padding:4px;background:yellow">' +
                '<slot name="primary"><div style="width:20px;height:17px;background:red"></div></slot></div>';
            const light = host.firstElementChild;
            const fallback = root.querySelector('slot').firstElementChild;
            const initial = light.getBoundingClientRect();
            light.slot = 'missing';
            const uncovered = fallback.getBoundingClientRect();
            light.slot = 'primary';
            const restored = light.getBoundingClientRect();
            const obsolete = fallback.getBoundingClientRect();
            if (initial.y === 13 && uncovered.y === initial.y && uncovered.height === 17 &&
                restored.y === initial.y && restored.height === 15 && obsolete.height === 0 &&
                fallback.getClientRects().length === 0) console.log('SLOT_MARGIN_READY');
            else console.error('Slot margin mismatch: ' + [initial.y, uncovered.y, restored.y, obsolete.height]);
        </script>`);
    assert.equal(log.includes('SLOT_MARGIN_READY'), true, log.split('\n')
        .filter(line => /Slot margin mismatch|exception/.test(line)).join('\n'));
    assert.deepEqual(rgb(image, 14, 14), [0, 128, 0]);
    assert.deepEqual(rgb(image, 14, 7), [255, 255, 255]);
});

test('manual slot assignment orders paint, hit testing and live geometry', async () => {
    const {image, log} = await render('manual-slot-paint', `<!doctype html>
        <style>body{margin:0;background:white}#host {padding:7px}
        #host > div {width:20px;height:10px} #red{background:red} #blue{background:blue}</style>
        <div id="host"><div id="red"></div><div id="blue" slot="primary"></div></div>
        <script>
            const host = document.getElementById('host');
            const red = document.getElementById('red');
            const blue = document.getElementById('blue');
            const root = host.attachShadow({mode:'open', slotAssignment:'manual'});
            root.innerHTML = '<slot name="primary"><div style="width:20px;height:10px;background:orange"></div></slot>' +
                '<slot><div style="width:20px;height:10px;background:green"></div></slot>';
            const primary = root.firstElementChild;
            const secondary = root.lastElementChild;
            const fallback = primary.firstElementChild;
            const checks = {
                initial:root.slotAssignment === 'manual' && blue.assignedSlot === null &&
                    fallback.getBoundingClientRect().y === 7 && primary.assignedNodes({flatten:true})[0] === fallback
            };
            primary.assign(blue, red, blue);
            checks.ordered = primary.assignedNodes().length === 2 && primary.assignedNodes()[0] === blue &&
                primary.assignedElements()[1] === red && blue.assignedSlot === primary;
            try { primary.assign(red, document.createDocumentFragment()); }
            catch (error) {
                checks.invalid = error instanceof TypeError && primary.assignedNodes()[0] === blue;
            }
            checks.boxes = blue.getBoundingClientRect().y === 7 && red.getBoundingClientRect().y === 17 &&
                fallback.getClientRects().length === 0 && document.elementFromPoint(12, 12) === blue;
            red.slot = 'ignored';
            checks.unaffected = red.assignedSlot === primary;
            secondary.assign(blue);
            checks.moved = primary.assignedNodes().length === 1 && primary.assignedNodes()[0] === red &&
                blue.assignedSlot === secondary && red.getBoundingClientRect().y === 7 &&
                blue.getBoundingClientRect().y === 17;
            secondary.assign();
            checks.cleared = blue.assignedSlot === null && blue.getClientRects().length === 0 &&
                secondary.firstElementChild.getBoundingClientRect().y === 17;
            console.log('MANUAL_SLOTS', JSON.stringify(checks));
        </script>`);
    const match = log.match(/MANUAL_SLOTS (\{[^\n]+\})/);
    assert(match, log.slice(-3000));
    assert.deepEqual(JSON.parse(match[1]), {initial:true,ordered:true,invalid:true,boxes:true,unaffected:true,moved:true,cleared:true});
    assert.deepEqual(rgb(image, 12, 12), [255, 0, 0]);
    assert.deepEqual(rgb(image, 12, 22), [0, 128, 0]);
});

test('layout keeps nested projected slots in their source roots', async () => {
    const {image, log} = await render('nested-slot-paint', `<!doctype html>
        <style>body{margin:0;background:white}#light{width:18px;height:12px;background:green}</style>
        <div id="outer"><div id="light"></div></div>
        <script>
            const outer = document.getElementById('outer');
            const light = document.getElementById('light');
            const outerRoot = outer.attachShadow({mode:'open', slotAssignment:'manual'});
            outerRoot.innerHTML = '<div><slot><i style="display:block;width:17px;height:19px;background:red"></i></slot></div>';
            const inner = outerRoot.firstElementChild;
            const outerSlot = inner.firstElementChild;
            const fallback = outerSlot.firstElementChild;
            const innerRoot = inner.attachShadow({mode:'open', slotAssignment:'manual'});
            innerRoot.innerHTML = '<section style="padding:7px"><slot></slot></section>';
            const innerSlot = innerRoot.querySelector('slot');
            innerSlot.assign(outerSlot);
            const checks = {
                fallback:innerSlot.assignedNodes({flatten:true})[0] === fallback &&
                    fallback.getBoundingClientRect().height === 19
            };
            outerSlot.assign(light);
            const box = light.getBoundingClientRect();
            checks.projected = innerSlot.assignedNodes({flatten:true})[0] === light && box.x === 7 &&
                box.y === 7 && box.width === 18 && box.height === 12 && document.elementFromPoint(13, 13) === light;
            console.log('NESTED_SLOTS', JSON.stringify(checks));
        </script>`);
    const match = log.match(/NESTED_SLOTS (\{[^\n]+\})/);
    assert(match, log.slice(-3000));
    assert.deepEqual(JSON.parse(match[1]), {fallback:true,projected:true});
    assert.deepEqual(rgb(image, 13, 13), [0, 128, 0]);
});

test('HTML slots default to display contents and display none suppresses their flattened subtree', async () => {
    const {image, log} = await render('slot-display', `<!doctype html>
        <style>body{margin:0;background:white}#light{width:18px;height:12px;background:green}</style>
        <div id="host"><div id="light" slot="assigned"></div></div><slot id="outside"></slot>
        <script>
            const host = document.getElementById('host');
            const light = document.getElementById('light');
            const root = host.attachShadow({mode:'open'});
            root.innerHTML = '<style>slot.off{display:none}</style><div style="padding:7px">' +
                '<slot name="assigned"></slot><slot><div style="width:18px;height:15px;background:red"></div></slot>' +
                '<div style="width:18px;height:11px;background:blue"></div></div>';
            const [assigned,empty] = root.querySelectorAll('slot');
            const fallback = empty.firstElementChild;
            const after = empty.nextElementSibling;
            const checks = {
                defaults:getComputedStyle(assigned).display === 'contents' &&
                    getComputedStyle(document.getElementById('outside')).display === 'contents' &&
                    light.getBoundingClientRect().y === 7 && fallback.getBoundingClientRect().y === 19
            };
            assigned.style.display = 'none';
            checks.inline = light.assignedSlot === assigned && light.getClientRects().length === 0 &&
                fallback.getBoundingClientRect().y === 7 && document.elementFromPoint(10, 10) === fallback;
            empty.className = 'off';
            checks.scoped = fallback.getClientRects().length === 0 && after.getBoundingClientRect().y === 7 &&
                document.elementFromPoint(10, 10) === after;
            assigned.style.removeProperty('display');
            checks.restored = light.getBoundingClientRect().y === 7 && after.getBoundingClientRect().y === 19 &&
                document.elementFromPoint(10, 10) === light;
            assigned.hidden = true;
            checks.hidden = getComputedStyle(assigned).display === 'none' && light.getClientRects().length === 0 &&
                after.getBoundingClientRect().y === 7;
            console.log('SLOT_DISPLAY', JSON.stringify(checks));
        </script>`);
    const match = log.match(/SLOT_DISPLAY (\{[^\n]+\})/);
    assert(match, log.slice(-3000));
    assert.deepEqual(JSON.parse(match[1]), {defaults:true,inline:true,scoped:true,restored:true,hidden:true});
    assert.deepEqual(rgb(image, 10, 10), [0, 0, 255]);
    assert.deepEqual(rgb(image, 10, 20), [255, 255, 255]);
});

test('hidden until found retains its box and participates in the content-visibility cascade', async () => {
    const {image, log} = await render('hidden-until-found', `<!doctype html>
        <style>body{margin:0}#panel{width:30px;height:30px;background:blue}
        #child{height:20px;background:red}</style>
        <div id="panel"><div id="child" tabindex="0"></div></div>
        <script>
            const panel = document.getElementById('panel');
            const child = document.getElementById('child');
            const computed = getComputedStyle(panel);
            const checks = {initial:panel.hidden === false && computed.contentVisibility === 'visible'};
            panel.hidden = 'UnTiL-FoUnD';
            checks.reflected = panel.hidden === 'until-found' && panel.getAttribute('hidden') === 'until-found';
            checks.retained = computed.display === 'block' && computed.contentVisibility === 'hidden' &&
                panel.getBoundingClientRect().height === 30 && document.elementFromPoint(10,10) === panel;
            child.focus();
            checks.focus = document.activeElement !== child;
            panel.style.contentVisibility = 'visible';
            checks.override = computed.contentVisibility === 'visible' && document.elementFromPoint(10,10) === child;
            panel.style.removeProperty('content-visibility');
            checks.restored = computed.contentVisibility === 'hidden';
            console.log('HIDDEN_CASCADE', JSON.stringify(checks));
        </script>`);
    const match = log.match(/HIDDEN_CASCADE (\{[^\n]+\})/);
    assert(match, log.slice(-3000));
    assert.deepEqual(JSON.parse(match[1]), {initial:true,reflected:true,retained:true,focus:true,override:true,restored:true});
    assert.deepEqual(rgb(image, 10, 10), [0, 0, 255]);
});

test('fragment navigation opens details and reveals until-found ancestors before scrolling', async () => {
    const {log} = await render('reveal-fragment', `<!doctype html>
        <div style="height:2200px"></div>
        <details id="ancestor" hidden="until-found"><summary>Summary</summary>
            <div id="target" hidden="until-found">Destination</div>
        </details>
        <div style="height:180px"></div>
        <script>
            const ancestor = document.getElementById('ancestor');
            const target = document.getElementById('target');
            const callbacks = [];
            ancestor.addEventListener('beforematch', event => {
                callbacks.push([event.target.id,event.target.hidden,ancestor.open,window.pageYOffset,
                    event.isTrusted,event.bubbles,event.cancelable]);
            });
            location.hash = 'target';
            const reveal = location.hash === '#target' && ancestor.open && !target.hidden && !ancestor.hidden;
            const scrolled = window.pageYOffset > 0;
            console.log('FRAGMENT_REVEAL', JSON.stringify({reveal,scrolled,callbacks}));
        </script>`);
    const match = log.match(/FRAGMENT_REVEAL (\{[^\n]+\})/);
    assert(match, log.slice(-3000));
    assert.deepEqual(JSON.parse(match[1]), {reveal:true,scrolled:true,callbacks:[
        ['target','until-found',false,0,true,true,false],
        ['ancestor','until-found',true,0,true,true,false]
    ]});
});

test('slotchange signals assignment and fallback updates after mutation observers', async () => {
    const {image, log} = await render('slot-change', `<!doctype html>
        <body style="margin:0"><div id="result" style="width:20px;height:20px;background:red"></div>
        <script>
            const host = document.createElement('div');
            const root = host.attachShadow({mode:'closed'});
            root.innerHTML = '<slot></slot><slot></slot>';
            const first = root.firstElementChild;
            const last = root.lastElementChild;
            const initiallyUnnamed = first.name === '';
            first.name = 'a';
            last.name = 'b';
            const item = document.createElement('span');
            const records = [];
            let valid = true;
            let next = false;
            function check(condition, message) {
                if (!condition) { valid = false; console.error('Slot change failed: ' + message); }
            }
            const observer = new MutationObserver(() => {
                records.push('observer');
                if (next) { next = false; first.appendChild(document.createTextNode('fallback')); }
            });
            observer.observe(host, {childList:true, attributes:true, subtree:true});
            check(initiallyUnnamed && first.getAttribute('name') === 'a' && last.getAttribute('name') === 'b',
                'slot name reflects its content attribute');
            root.addEventListener('slotchange', event => {
                records.push(event.target.name);
                check(event.isTrusted && event.bubbles && !event.composed && !event.cancelable,
                    'trusted event with shadow-boundary propagation');
            });
            host.addEventListener('slotchange', () => check(false, 'host outside shadow must not receive it'));
            item.slot = 'a';
            host.appendChild(item);
            item.slot = 'b';
            item.slot = 'a';
            check(records.length === 0, 'no synchronous notification');
            queueMicrotask(() => {
                check(records.join(',') === 'observer,a,b', 'each signaled slot follows observer delivery');
                check(first.assignedNodes()[0] === item && last.assignedNodes().length === 0, 'final assignment');
                records.length = 0;
                next = true;
                host.removeChild(item);
                queueMicrotask(() => {
                    check(records.join(',') === 'observer,a', 'observer mutations signal a separate delivery');
                    queueMicrotask(() => {
                        check(records.join(',') === 'observer,a,a' && first.assignedNodes().length === 0 &&
                            first.assignedNodes({flatten:true})[0].data === 'fallback', 'fallback signal and flattening');
                        if (valid) document.getElementById('result').style.background = 'green';
                    });
                });
            });
        </script>`);
    assert.deepEqual(rgb(image, 10, 10), [0, 128, 0], log.split('\n')
        .filter(line => /Slot change failed|exception/.test(line)).join('\n'));
});

test('events route through closed slots and hide closed-tree entries from outside listeners', async () => {
    const {image, log} = await render('slot-event-path', `<!doctype html>
        <body style="margin:0"><div id="result" style="width:20px;height:20px;background:red"></div>
        <div id="host"><span id="light">Assigned</span></div>
        <script>
            const host = document.getElementById('host');
            const light = document.getElementById('light');
            const root = host.attachShadow({mode:'closed'});
            root.innerHTML = '<slot></slot><span>Internal</span>';
            const slot = root.firstElementChild;
            const internal = root.lastElementChild;
            let valid = true;
            let steps = [];
            function check(condition, message) {
                if (!condition) { valid = false; console.error('Slot event path failed: ' + message); }
            }
            slot.addEventListener('projected', event => {
                steps.push('slot');
                const path = event.composedPath();
                check(event.target === light && path[0] === light && path[1] === slot && path[2] === root,
                    'assigned node keeps its target and closed slot in the internal path');
            });
            host.addEventListener('projected', event => {
                steps.push('host');
                const path = event.composedPath();
                check(event.target === light && path[0] === light && path[1] === host &&
                    !path.includes(root) && !path.includes(slot), 'host sees just the public projection');
            });
            light.dispatchEvent(new Event('projected', {bubbles:true}));
            check(steps.join(',') === 'slot,host', 'non-composed event can enter another root through its slot');

            steps = [];
            internal.addEventListener('internal', event => {
                steps.push('inside');
                check(event.target === internal && event.composedPath()[1] === root, 'internal target');
            });
            host.addEventListener('internal', event => {
                steps.push('host');
                check(event.target === host && event.eventPhase === Event.AT_TARGET &&
                    event.composedPath()[0] === host, 'host sees the retargeted composed event');
            });
            document.addEventListener('internal', event => {
                steps.push('document');
                check(event.target === host, 'document sees the host');
            });
            const local = new Event('internal', {bubbles:true});
            internal.dispatchEvent(local);
            check(steps.join(',') === 'inside' && local.target === null, 'non-composed event stays in its source root');
            steps = [];
            internal.dispatchEvent(new Event('internal', {bubbles:true, composed:true}));
            check(steps.join(',') === 'inside,host,document', 'composed event crosses its source root');
            if (valid) document.getElementById('result').style.background = 'green';
        </script>`);
    assert.deepEqual(rgb(image, 10, 10), [0, 128, 0], log.split('\n')
        .filter(line => /Slot event path failed|exception/.test(line)).join('\n'));
});

test('shadow event listeners get retargeted related targets and preserve the enclosing window event', async () => {
    const {log} = await render('shadow-related-target', `<!doctype html>
        <main><div id="first"></div><div id="second"></div></main>
        <script>
            const first = document.getElementById('first');
            const second = document.getElementById('second');
            const root = first.attachShadow({mode:'open'});
            const otherRoot = second.attachShadow({mode:'closed'});
            root.innerHTML = '<i></i><b></b>';
            otherRoot.innerHTML = '<span></span>';
            const target = root.firstElementChild;
            const peer = root.lastElementChild;
            const remote = otherRoot.firstElementChild;
            const checks = {};

            const local = new MouseEvent('local', {bubbles:true, composed:true, relatedTarget:peer});
            const outer = new Event('outer');
            first.addEventListener('outer', () => target.dispatchEvent(local));
            root.addEventListener('local', event => {
                checks.nested = window.event === outer && event.target === target && event.relatedTarget === peer;
                first.appendChild(target);
            });
            first.addEventListener('local', () => { checks.escaped = true; });
            first.dispatchEvent(outer);
            checks.cleared = local.target === null && local.relatedTarget === null && window.event === undefined;

            root.appendChild(target);
            const across = new FocusEvent('across', {bubbles:true, composed:true, relatedTarget:remote});
            root.addEventListener('across', event => {
                checks.inside = event.target === target && event.relatedTarget === second && window.event === undefined;
            });
            document.addEventListener('across', event => {
                checks.outside = event.target === first && event.relatedTarget === second && window.event === event;
            });
            target.dispatchEvent(across);
            checks.publicTargets = across.target === first && across.relatedTarget === second;

            first.addEventListener('suppressed', () => { checks.invoked = true; });
            checks.suppressed = first.dispatchEvent(new FocusEvent('suppressed', {relatedTarget:target}));
            console.log('SHADOW_RETARGET', JSON.stringify(checks));
        </script>`);
    const match = log.match(/SHADOW_RETARGET (\{[^\n]+\})/);
    assert(match, log.slice(-3000));
    assert.deepEqual(JSON.parse(match[1]), {nested:true,cleared:true,inside:true,outside:true,
        publicTargets:true,suppressed:true});
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
