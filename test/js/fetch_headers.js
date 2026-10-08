'use strict';

function check(condition, message) {
    if (!condition) throw new Error(message);
}

async function exerciseHeaders() {
    const OriginalHeaders = Headers;
    const input = new Headers([['X-Example', 'value']]);
    globalThis.Headers = function() { throw new Error('global constructor replacement'); };
    const response = await fetch('fetch_json_spread_call.json', {headers: input});
    check(response.headers instanceof OriginalHeaders, 'retained native constructor');
    check(response.headers.get('missing') === null, 'response lookup');
    check(response.headers.getSetCookie().length === 0, 'response cookie filtering');
    for (const operation of ['append', 'set', 'delete']) {
        let rejected = false;
        try { response.headers[operation]('x', 'value'); }
        catch (error) { rejected = error instanceof TypeError; }
        check(rejected, 'immutable fetched response');
    }
    check((await response.text()).length > 0, 'response body retained');
    globalThis.Headers = OriginalHeaders;
    console.log('fetch native response headers and immutable guard passed');

    for (const init of [null, 5, [['bad name', 'value']], [['x', 'bad\nvalue']], {x: '\u0100'}]) {
        const promise = fetch('fetch_json_spread_call.json', {headers: init});
        check(promise instanceof Promise, 'validation returns a promise');
        let rejected = false;
        try { await promise; } catch (error) { rejected = error instanceof TypeError; }
        check(rejected, 'invalid HeadersInit rejects');
    }
    const sentinel = new Error('headers getter');
    try {
        await fetch('fetch_json_spread_call.json', {body: 'retained until rejection', get headers() { throw sentinel; }});
        throw new Error('missing getter exception');
    } catch (error) { check(error === sentinel, 'getter exception identity'); }
    console.log('fetch header validation rejection passed');

    let invalidUrlRejected = false;
    try { await fetch('http://['); }
    catch (error) { invalidUrlRejected = error instanceof TypeError; }
    check(invalidUrlRejected, 'invalid URLs retain TypeError identity');
    console.log('fetch invalid URL rejection passed');
}
exerciseHeaders().catch(error => { console.log('FAIL ' + error.message); });
