const eventTargetPrototypeCases = ['inheritance', 'assignment', 'undefined', 'prototype', 'descriptor'];

function exerciseEventTargetPrototype(kind) {
    const failures = [];
    function check(value, label) { if (!value) failures.push(label); }
    function nodes() {
        return [document, document.documentElement, document.body, document.createElement('div'),
            document.createElementNS('http://www.w3.org/2000/svg', 'circle'),
            document.createTextNode('text'), document.createComment('comment'),
            document.createDocumentFragment(), document.createAttribute('data-probe'),
            document.implementation.createHTMLDocument('foreign')];
    }
    const methods = ['addEventListener', 'removeEventListener', 'dispatchEvent'];
    if (kind === 'inheritance') {
        check(Object.getPrototypeOf(Node.prototype) === EventTarget.prototype, 'Node prototype parent');
        check(Object.getPrototypeOf(Node) === EventTarget, 'Node constructor parent');
        methods.forEach(name => check(window[name] === EventTarget.prototype[name], name + ' Window inheritance'));
        nodes().forEach((node, index) => {
            check(node instanceof EventTarget, 'EventTarget instance ' + index);
            methods.forEach(name => {
                check(node[name] === EventTarget.prototype[name], name + ' identity ' + index);
                check(!Object.prototype.hasOwnProperty.call(node, name), name + ' inherited ' + index);
            });
        });
    } else if (kind === 'assignment' || kind === 'undefined') {
        nodes().forEach((node, index) => methods.forEach(name => {
            const original = node[name], replacement = kind === 'undefined' ? undefined : function () { return 17; };
            node[name] = replacement;
            const descriptor = Object.getOwnPropertyDescriptor(node, name);
            check(node[name] === replacement, name + ' override ' + index);
            check(descriptor && descriptor.value === replacement && descriptor.writable &&
                descriptor.enumerable && descriptor.configurable, name + ' descriptor ' + index);
            check(Object.getOwnPropertyNames(node).includes(name), name + ' own key ' + index);
            check(delete node[name], name + ' delete ' + index);
            check(node[name] === original, name + ' restore ' + index);
        }));
    } else if (kind === 'prototype') {
        let adds = 0, removes = 0, dispatches = 0, events = 0;
        const original = methods.map(name => EventTarget.prototype[name]);
        EventTarget.prototype.addEventListener = function (type, listener, options) {
            adds++; return original[0].call(this, type, listener, options);
        };
        EventTarget.prototype.removeEventListener = function (type, listener, options) {
            removes++; return original[1].call(this, type, listener, options);
        };
        EventTarget.prototype.dispatchEvent = function (event) {
            dispatches++; return original[2].call(this, event);
        };
        const targets = nodes().concat([window, new EventTarget()]);
        targets.forEach((target, index) => {
            const listener = function (event) { check(event.target === target, 'event target ' + index); events++; };
            target.addEventListener('prototype-probe', listener);
            target.dispatchEvent(new Event('prototype-probe'));
            target.removeEventListener('prototype-probe', listener);
            target.dispatchEvent(new Event('prototype-probe'));
        });
        check(adds === targets.length, 'prototype add interception');
        check(removes === targets.length, 'prototype remove interception');
        check(dispatches === targets.length * 2, 'prototype dispatch interception');
        check(events === targets.length, 'native listener delivery and removal');
        methods.forEach((name, index) => EventTarget.prototype[name] = original[index]);
    } else if (kind === 'descriptor') {
        const node = document, name = 'addEventListener', original = node[name];
        let receiver, written;
        Object.defineProperty(node, name, {configurable:true,
            get() { receiver = this; return 19; }, set(value) { receiver = this; written = value; }});
        check(node[name] === 19 && receiver === node, 'own getter receiver');
        check(Reflect.set(node, name, 23) && written === 23 && receiver === node, 'own setter receiver');
        delete node[name];
        check(node[name] === original, 'accessor removal');
        const descriptor = Object.getOwnPropertyDescriptor(EventTarget.prototype, name);
        Object.defineProperty(EventTarget.prototype, name, {writable:false});
        check(Reflect.set(node, name, function () {}) === false, 'inherited readonly completion');
        check(!Object.prototype.hasOwnProperty.call(node, name), 'readonly does not create own method');
        let threw = false;
        try { (function () { 'use strict'; node[name] = function () {}; })(); }
        catch (error) { threw = error instanceof TypeError; }
        check(threw, 'strict inherited readonly assignment');
        Object.defineProperty(EventTarget.prototype, name, descriptor);
    }
    console.log('EVENT_TARGET_PROTOTYPE_RESULT', JSON.stringify({failures}));
    if (!failures.length) document.getElementById('probe').style.background = 'green';
}

function eventTargetPrototypeHtml(kind) {
    return '<!doctype html><style>body{margin:0}#probe{width:80px;height:80px;background:red}</style>' +
        '<div id="probe"></div><script>(' + exerciseEventTargetPrototype.toString() + ')(' +
        JSON.stringify(kind) + ');</script>';
}

module.exports = {eventTargetPrototypeCases, eventTargetPrototypeHtml};
