const rangeFragmentCases = ['default-context', 'context-table', 'namespace', 'foreign-document',
    'scripting', 'coercion'];

function exerciseRangeFragment(kind) {
    const failures = [], host = document.getElementById('host');
    function check(value, label) { if (!value) failures.push(label); }
    function unchanged(range, call) {
        const saved = [range.startContainer, range.startOffset, range.endContainer, range.endOffset];
        const fragment = call();
        check(range.startContainer === saved[0] && range.startOffset === saved[1] &&
            range.endContainer === saved[2] && range.endOffset === saved[3], 'range boundaries preserved');
        return fragment;
    }
    function throwsNamed(call, name) {
        try { call(); } catch (error) { return error.name === name; }
        return false;
    }
    function script(source) { return '<script>' + source + '<' + '/script>'; }
    try {
        const range = document.createRange(), parse = Range.prototype.createContextualFragment;
        check(typeof parse === 'function' && parse.length === 1, 'prototype method and arity');
        if (kind === 'default-context') {
            for (const node of [null, document, document.documentElement]) {
                if (node) range.selectNodeContents(node);
                const fragment = unchanged(range, () => range.createContextualFragment('<tr><td>x</td></tr><p>body</p>'));
                check(fragment instanceof DocumentFragment && fragment.parentNode === null &&
                    fragment.ownerDocument === document, 'detached fragment identity');
                check(fragment.firstChild.nodeType === 3 && fragment.firstChild.data === 'x' &&
                    fragment.lastChild.tagName === 'P', 'non-element and html contexts use body parsing');
            }
            const a = range.createContextualFragment(''), b = range.createContextualFragment('');
            check(a !== b && a.childNodes.length === 0 && b.childNodes.length === 0, 'fresh empty fragments');
        } else if (kind === 'context-table') {
            const table = document.createElement('table'), body = document.createElement('tbody');
            const text = document.createTextNode('context'), comment = document.createComment('context');
            table.appendChild(body); body.appendChild(text); body.appendChild(comment);
            for (const node of [body, text, comment]) {
                range.selectNodeContents(node);
                const fragment = unchanged(range, () => range.createContextualFragment('<!--mark--><tr><td>cell</td></tr>'));
                check(fragment.firstChild.nodeType === 8 && fragment.firstElementChild.tagName === 'TR' &&
                    fragment.querySelector('td').textContent === 'cell', 'table context from element or CharacterData');
                check(body.childNodes.length === 2, 'context children untouched');
            }
        } else if (kind === 'namespace') {
            const ns = 'http://www.w3.org/2000/svg', svg = document.createElementNS(ns, 'svg');
            range.selectNodeContents(svg);
            const fragment = range.createContextualFragment('<circle r="8"/><foreignObject><div>html</div></foreignObject>');
            check(fragment.firstChild.namespaceURI === ns && fragment.firstChild.localName === 'circle', 'SVG namespace');
            check(fragment.lastChild.namespaceURI === ns &&
                fragment.lastChild.firstChild.namespaceURI === 'http://www.w3.org/1999/xhtml', 'foreignObject integration');
        } else if (kind === 'foreign-document') {
            const doc = document.implementation.createHTMLDocument('foreign'), r = doc.createRange();
            const sentinel = document.createElement('span'); host.appendChild(sentinel);
            doc.body.innerHTML = '<p>kept</p>'; r.selectNodeContents(doc.body);
            const fragment = unchanged(r, () => r.createContextualFragment('<em>owned</em>'));
            check(fragment.ownerDocument === doc && fragment.firstChild.ownerDocument === doc, 'foreign ownership');
            check(doc.body.textContent === 'kept' && sentinel.parentNode === host, 'both documents preserved');
            doc.body.appendChild(fragment);
            check(doc.body.textContent === 'keptowned', 'foreign fragment insertion');
        } else if (kind === 'scripting') {
            window.contextualRuns = 0; range.selectNodeContents(host);
            const fragment = range.createContextualFragment(script('window.contextualRuns++') +
                '<div style="width:80px;height:80px;background:blue"></div>');
            const executable = fragment.firstChild;
            check(window.contextualRuns === 0, 'parsing does not execute');
            host.appendChild(fragment);
            check(window.contextualRuns === 1 && fragment.childNodes.length === 0, 'insertion executes once');
            executable.remove(); host.appendChild(executable);
            check(window.contextualRuns === 1, 'started script stays started');
            const inert = document.createElement('div'); inert.innerHTML = script('window.contextualRuns+=100');
            host.appendChild(inert.firstChild);
            check(window.contextualRuns === 1, 'innerHTML remains inert');
        } else if (kind === 'coercion') {
            let converted = 0;
            check(throwsNamed(() => parse.call({}, {toString(){ converted++; return 'bad'; }}), 'TypeError') &&
                converted === 0, 'receiver checked before conversion');
            check(throwsNamed(() => range.createContextualFragment(), 'TypeError'), 'required argument');
            check(throwsNamed(() => range.createContextualFragment(Symbol('markup')), 'TypeError'), 'DOMString symbol');
            for (const value of [null, undefined, 0])
                check(range.createContextualFragment(value).textContent === String(value), 'DOMString conversion');
            const body = document.createElement('tbody');
            const fragment = range.createContextualFragment({toString(){
                range.selectNodeContents(body); if (typeof gc === 'function') gc(); return '<tr><td>converted</td></tr>';
            }});
            check(fragment.firstChild.tagName === 'TR' && fragment.textContent === 'converted', 'context after conversion');
            const marker = {}; let caught;
            try { range.createContextualFragment({toString(){ throw marker; }}); } catch (error) { caught = error; }
            check(caught === marker, 'conversion exception preserved');
        }
    } catch (error) { failures.push('unexpected: ' + error.name + ': ' + error.message); }
    console.log('RANGE_FRAGMENT_RESULT', JSON.stringify({failures}));
    document.getElementById('probe').style.background = failures.length ? 'red' : 'green';
}

function rangeFragmentHtml(kind) {
    return '<!doctype html><style>body{margin:0}#probe,#host{position:fixed;top:0;width:80px;height:80px}' +
        '#probe{left:0;background:red}#host{left:100px}</style><div id="probe"></div><div id="host"></div>' +
        '<script>(' + exerciseRangeFragment.toString() + ')(' + JSON.stringify(kind) + ');</script>';
}

module.exports = {rangeFragmentCases, rangeFragmentHtml};
