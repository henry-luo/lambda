const htmlInsertionCases = ['positions', 'stylesheet', 'contexts', 'scripting', 'conversion', 'reactions'];

async function exerciseHtmlInsertion(kind) {
    const failures = [], host = document.getElementById('host');
    function check(value, label) { if (!value) failures.push(label); }
    function throwsNamed(call, name) { try { call(); } catch (error) { return error.name === name; } return false; }
    try {
        if (kind === 'positions') {
            for (const position of ['beforebegin', 'afterbegin', 'beforeend', 'afterend']) {
                const parent = document.createElement('div'); parent.innerHTML = '<i><em>kept</em></i>';
                host.appendChild(parent); const target = parent.firstChild;
                const result = target.insertAdjacentHTML(position, '<b>inserted</b><!--marker-->tail');
                check(result === undefined, 'undefined result: ' + position);
                const inside = position === 'afterbegin' || position === 'beforeend';
                const owner = inside ? target : parent, before = position === 'beforebegin' || position === 'afterbegin';
                check(owner.querySelectorAll('b').length === 1 && target.querySelector('em').textContent === 'kept', 'inserted nodes');
                check((before ? owner.firstChild : owner.lastChild).nodeName === (before ? 'B' : '#text'), 'ordered slots');
                check(parent.innerHTML.includes('<b>inserted</b><!--marker-->tail'), 'serialized backing');
            }
        } else if (kind === 'stylesheet') {
            const source = '<style id="inserted-style">#paint{background:blue;width:80px;height:80px}</style>';
            const parsed = new DOMParser().parseFromString(source, 'text/html');
            document.head.insertAdjacentHTML('beforeend', parsed.head.innerHTML + parsed.body.innerHTML);
            const style = document.getElementById('inserted-style'), target = document.getElementById('paint');
            check(style.parentNode === document.head && style.ownerDocument === document, 'style ownership');
            check(getComputedStyle(target).backgroundColor === 'rgb(0, 0, 255)', 'style reaches cascade');
            check(Array.from(document.styleSheets).some(sheet => sheet.ownerNode === style), 'stylesheet publication');
        } else if (kind === 'contexts') {
            const table = document.createElement('table'), body = document.createElement('tbody');
            table.appendChild(body); host.appendChild(table);
            body.insertAdjacentHTML('beforeend', '<tr><td>cell</td></tr>');
            check(body.firstChild.tagName === 'TR' && body.firstChild.firstChild.textContent === 'cell', 'table context');
            const fragment = document.createDocumentFragment(), anchor = document.createElement('i'); fragment.appendChild(anchor);
            anchor.insertAdjacentHTML('beforebegin', '<tr><td>text</td></tr><b>body</b>');
            check(fragment.firstChild.nodeType === 3 && fragment.firstChild.data === 'text' &&
                fragment.childNodes[1].tagName === 'B', 'fragment parent uses body context');
            const foreign = document.implementation.createHTMLDocument('foreign');
            foreign.body.insertAdjacentHTML('afterbegin', '<p>owned</p>');
            check(foreign.body.firstChild.ownerDocument === foreign && foreign.body.textContent === 'owned', 'foreign document');
        } else if (kind === 'scripting') {
            window.insertedRuns = 0;
            host.insertAdjacentHTML('beforeend', '<script>window.insertedRuns++<' + '/script><b>kept</b>');
            const script = host.firstChild; script.remove(); document.body.appendChild(script);
            check(window.insertedRuns === 0 && host.textContent === 'kept', 'inserted scripts remain inert');
        } else if (kind === 'conversion') {
            const target = document.createElement('i'); host.appendChild(target); let converted = 0;
            const result = target.insertAdjacentHTML({toString(){ converted++; return 'afterbegin'; }},
                {toString(){ converted++; if (typeof gc === 'function') gc(); return '<b>converted</b>'; }});
            check(converted === 2 && target.firstChild.textContent === 'converted' && result === undefined, 'DOMString conversions');
            check(throwsNamed(() => target.insertAdjacentHTML('wrong', ''), 'SyntaxError'), 'invalid position');
            check(throwsNamed(() => document.createElement('i').insertAdjacentHTML('beforebegin', ''), 'NoModificationAllowedError'), 'detached outer insertion');
            check(throwsNamed(() => document.documentElement.insertAdjacentHTML('afterend', ''), 'NoModificationAllowedError'), 'document parent insertion');
            check(throwsNamed(() => target.insertAdjacentHTML('beforeend', Symbol('html')), 'TypeError'), 'symbol conversion');
            let caught; const marker = {};
            try { target.insertAdjacentHTML('beforeend', {toString(){ throw marker; }}); } catch (error) { caught = error; }
            check(caught === marker, 'conversion exception');
        } else if (kind === 'reactions') {
            let built = 0, connected = 0, added = 0;
            customElements.define('inserted-probe', class extends HTMLElement {
                constructor() { super(); built++; if (typeof gc === 'function') gc(); }
                connectedCallback() { connected++; }
            });
            const observer = new MutationObserver(records => { for (const record of records) added += record.addedNodes.length; });
            observer.observe(host, {childList:true});
            const iterator = NodeList.prototype[Symbol.iterator]; let iteratorCalls = 0;
            NodeList.prototype[Symbol.iterator] = function(){ iteratorCalls++; throw new Error('internal iterator'); };
            try { host.insertAdjacentHTML('beforeend', '<inserted-probe><b>child</b></inserted-probe>tail'); }
            finally { NodeList.prototype[Symbol.iterator] = iterator; }
            check(iteratorCalls === 0, 'native insertion avoids script iterators');
            await Promise.resolve();
            check(built === 1 && connected === 1 && host.firstChild.isConnected, 'custom element lifecycle');
            check(added === 2, 'child-list publication');
            observer.disconnect();
        }
    } catch (error) { failures.push('unexpected: ' + error.name + ': ' + error.message); }
    console.log('HTML_INSERTION_RESULT', JSON.stringify({failures}));
    document.getElementById('probe').style.background = failures.length ? 'red' : 'green';
}

function htmlInsertionHtml(kind) {
    return '<!doctype html><style>body{margin:0}#probe,#paint{position:fixed;top:0;width:80px;height:80px}' +
        '#probe{left:0;background:red}#paint{left:100px;background:red}#host{position:absolute;top:100px}</style>' +
        '<div id="probe"></div><div id="paint" class="paint-target"></div><div id="host"></div>' +
        '<script>(' + exerciseHtmlInsertion.toString() + ')(' + JSON.stringify(kind) + ');</script>';
}

module.exports = {htmlInsertionCases, htmlInsertionHtml};
