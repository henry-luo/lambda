const {test} = require('node:test');
const assert = require('node:assert/strict');
const {renderFixtures, rgb} = require('./render_fixture.cjs');

const render = renderFixtures('web-crypto');

async function verify(label, checks) {
    const {image, log} = await render(label, `<!doctype html><body style="margin:0;background:red"><script>
        function check(condition, reason) { if (!condition) throw new Error(reason); }
        function rejects(action, expected, name, code) {
            try { action(); } catch (error) {
                check(error instanceof expected && error.name === name &&
                    (code === undefined || error.code === code), 'wrong exception: ' + error);
                return error;
            }
            throw new Error('missing exception: ' + name);
        }
        ${checks}
        document.body.style.background = 'lime';
    </script></body>`);
    assert.deepEqual(rgb(image, 10, 10), [0, 255, 0], log);
}

test('Web Crypto fills the supplied integer view, including the maximum request', async () => {
    await verify('integer-views', `
        const random = window.crypto;
        check(globalThis.crypto === random && random instanceof Crypto, 'Crypto identity');
        check(random.getRandomValues.name === 'getRandomValues' && random.getRandomValues.length === 1,
            'method signature');
        const backing = new Uint8Array(48);
        backing.fill(35);
        const slice = new Uint8Array(backing.buffer, 8, 32);
        check(random.getRandomValues(slice) === slice, 'returned subview');
        check(backing[7] === 35 && backing[40] === 35 && slice.some(byte => byte !== 35), 'subview extent');
        class TypedSubclass extends Uint8Array {}
        const integerTypes = [Int8Array, Uint8Array, Uint8ClampedArray, Int16Array, Uint16Array,
            Int32Array, Uint32Array, BigInt64Array, BigUint64Array, TypedSubclass];
        for (const Type of integerTypes) {
            const view = new Type(32);
            check(random.getRandomValues(view) === view, 'integer view identity');
            const empty = new Type(0);
            check(random.getRandomValues(empty) === empty, 'empty view identity');
        }
        const large = new Uint32Array(16384);
        check(random.getRandomValues(large) === large, '65536 byte request');
        check(large.subarray(0, 8).some(number => number !== 0) &&
            large.subarray(16376).some(number => number !== 0), 'filled first and last provider chunks');
    `);
});

test('Web Crypto validates buffers, receiver and byte quota', async () => {
    await verify('web-idl-validation', `
        const quota = rejects(() => crypto.getRandomValues(new Uint32Array(16385)),
            QuotaExceededError, 'QuotaExceededError', 22);
        check(quota instanceof DOMException && quota.quota === null && quota.requested === null, 'quota detail');
        const custom = new QuotaExceededError('example', {quota: 1.5, requested: 4});
        check(custom.message === 'example' && custom.quota === 1.5 && custom.requested === 4, 'named quota options');
        rejects(() => new QuotaExceededError('', {quota: 2, requested: 1}), RangeError, 'RangeError');
        rejects(() => crypto.getRandomValues(new Float32Array(20000)), DOMException, 'TypeMismatchError', 17);
        rejects(() => crypto.getRandomValues(new DataView(new ArrayBuffer(8))), DOMException, 'TypeMismatchError', 17);
        rejects(() => crypto.getRandomValues({}), TypeError, 'TypeError');
        rejects(() => crypto.getRandomValues.call(Crypto.prototype, new Uint8Array(4)), TypeError, 'TypeError');
        rejects(() => new Crypto(), TypeError, 'TypeError');
        rejects(() => crypto.getRandomValues(new Uint8Array(new ArrayBuffer(8, {maxByteLength: 16}))),
            TypeError, 'TypeError');
        if (typeof SharedArrayBuffer !== 'undefined')
            rejects(() => crypto.getRandomValues(new Uint8Array(new SharedArrayBuffer(8))), TypeError, 'TypeError');
        const owner = new ArrayBuffer(8);
        const view = new Uint8Array(owner);
        owner.transfer();
        check(crypto.getRandomValues(view) === view && view.length === 0, 'detached integer view');
    `);
});
