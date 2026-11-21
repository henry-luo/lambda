const {test} = require('node:test');
const assert = require('node:assert/strict');
const {renderFixtures, loggedResult} = require('./render_fixture.cjs');

const render = renderFixtures('node-constants');

test('inherited Node constants stop a scroll-parent walk at the Document', async () => {
    const {log} = await render('scroll-parent', `<!doctype html><div id="target"></div><script>
        function scrollParent(element) {
            while ((element = element.parentNode) &&
                    !('nodeType' in element && element.nodeType === document.DOCUMENT_NODE)) {
                const position = element.style.getPropertyValue('position') || getComputedStyle(element).position;
                if (position === 'fixed') return element;
            }
            return document;
        }
        let result;
        try { result = {document: scrollParent(document.getElementById('target')) === document}; }
        catch (error) { result = {error: String(error)}; }
        console.log('NODE_CONSTANT_RESULT', JSON.stringify(result));
    </script>`);
    assert.deepEqual(loggedResult(log, 'NODE_CONSTANT_RESULT'), {document: true});
});

test('Node constants inherit through node interfaces with WebIDL descriptors', async () => {
    const {log} = await render('constant-descriptors', `<!doctype html><div id="target"></div><script>
        const element = document.getElementById('target');
        const text = document.createTextNode('text');
        const fragment = document.createDocumentFragment();
        const descriptor = Object.getOwnPropertyDescriptor(Node.prototype, 'DOCUMENT_NODE');
        const cssDescriptor = Object.getOwnPropertyDescriptor(CSSRule.prototype, 'STYLE_RULE');
        let strictRejected = false;
        try { (function(){'use strict'; document.DOCUMENT_NODE = 77;})(); }
        catch (error) { strictRejected = error instanceof TypeError; }
        const sloppy = Function('node', 'node.DOCUMENT_NODE=77;return node.DOCUMENT_NODE')(document);
        console.log('NODE_CONSTANT_RESULT', JSON.stringify({
            kinds: document.DOCUMENT_NODE === document.nodeType && element.ELEMENT_NODE === element.nodeType &&
                text.TEXT_NODE === text.nodeType && fragment.DOCUMENT_FRAGMENT_NODE === fragment.nodeType,
            inherited: !Object.prototype.hasOwnProperty.call(document, 'DOCUMENT_NODE'),
            descriptor: !!descriptor && descriptor.value === 9 && descriptor.enumerable &&
                !descriptor.writable && !descriptor.configurable,
            constructor: Node.DOCUMENT_NODE === 9,
            cssRule: CSSRule.STYLE_RULE === 1 && !!cssDescriptor && cssDescriptor.value === 1 &&
                cssDescriptor.enumerable && !cssDescriptor.writable && !cssDescriptor.configurable,
            readonly: strictRejected && sloppy === 9,
            position: !!(document.compareDocumentPosition(element) & document.DOCUMENT_POSITION_CONTAINED_BY)
        }));
    </script>`);
    assert.deepEqual(loggedResult(log, 'NODE_CONSTANT_RESULT'), {
        kinds: true, inherited: true, descriptor: true, constructor: true, cssRule: true,
        readonly: true, position: true
    });
});
