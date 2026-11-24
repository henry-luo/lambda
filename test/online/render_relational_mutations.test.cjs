const {test} = require('node:test');
const assert = require('node:assert/strict');
const {renderFixtures, rgb, loggedResult} = require('./render_fixture.cjs');
const {relationalMutationCases, relationalMutationHtml} = require('./relational_mutation_fixtures.cjs');
const render = renderFixtures('relational-mutations');

for (const kind of relationalMutationCases) test('relational mutation scope: ' + kind, async () => {
    const {image, log} = await render(kind, relationalMutationHtml(kind), {...process.env, LAYOUT_DEBUG:'cache'});
    assert.deepEqual(loggedResult(log, 'RELATIONAL_MUTATION_RESULT'), {failures:[]});
    assert.deepEqual(rgb(image, 40, 40), [0, 128, 0]);
    if (kind === 'unrelated' || kind === 'attributes' || kind.startsWith('typed-')) {
        assert.doesNotMatch(log, /mutation-fallback: reason=broad-(structural|relational)-css-risk/);
    }
    if(kind==='prepared-unrelated')assert.equal(/mutation-fallback: reason=broad-(structural|relational)-css-risk/.test(log),false,
        'attributes prepared before insertion must preserve the inserted subtree proof');
    if (kind.startsWith('unrelated-class')) assert.doesNotMatch(log,
        /mutation-fallback: reason=broad-(structural|relational)-css-risk/);
});
