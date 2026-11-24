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
    if (kind.startsWith('unrelated-leading-')) assert.doesNotMatch(log,
        /mutation-fallback: reason=broad-(structural|relational)-css-risk/);
});

test('local style writes still settle size queries beside sibling relational rules', async () => {
    const {image, log} = await render('sibling-size-query', `<!doctype html><style>
        body{margin:0}#container{container-type:inline-size;width:80px}
        #paint{height:80px;background:green}.lead:has(~ .marker){color:red}
        @container(min-width:100px){#paint{background:blue}}
        </style><div id=container><div id=paint></div></div><script>
        var scope=document.getElementById('container'),paint=document.getElementById('paint'),checks=[];
        for(var width of [80,120,80]){scope.style.width=width+'px';scope.getBoundingClientRect();
            checks.push(getComputedStyle(paint).backgroundColor===(width===120?'rgb(0, 0, 255)':'rgb(0, 128, 0)'));}
        console.log('SIBLING_CONTAINER_RESULT',JSON.stringify({checks}));</script>`,
        {...process.env, LAYOUT_DEBUG:'cache'});
    assert.deepEqual(loggedResult(log, 'SIBLING_CONTAINER_RESULT'), {checks:[true,true,true]});
    assert.deepEqual(rgb(image, 40, 40), [0, 128, 0]);
});
