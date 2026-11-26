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

test('a broad stylesheet rebuild retains its starting container measurements', async () => {
    const {log} = await render('stylesheet-container-query', `<!doctype html><style>
        #scope{container:panel / inline-size;width:120px}
        #witness{width:40px;height:20px}
        @container panel (min-width:100px){#witness{width:60px}}
        </style><style id=dynamic></style>
        <section id=scope><div id=witness></div></section><script>
        window.addEventListener('load',function(){
            var sheet=document.getElementById('dynamic'), witness=document.getElementById('witness');
            var widths=[witness.offsetWidth];
            sheet.textContent='#witness{color:blue}';
            widths.push(witness.offsetWidth);
            var color=getComputedStyle(witness).color;
            sheet.textContent='#scope{width:80px}';
            widths.push(witness.offsetWidth);
            sheet.textContent='#scope{container-name:other}';
            widths.push(witness.offsetWidth);
            sheet.textContent='';
            widths.push(witness.offsetWidth);
            console.log('STYLESHEET_QUERY_RESULT',JSON.stringify({widths,color}));
        });</script>`, {...process.env, LAYOUT_DEBUG:'cache', LAYOUT_PROFILE:'1'});
    assert.deepEqual(loggedResult(log, 'STYLESHEET_QUERY_RESULT'), {
        widths:[60,60,40,40,60], color:'rgb(0, 0, 255)',
    });
    const layouts = Array.from(log.matchAll(/mutation-fallback: reason=stylesheet-mutation[^]*?\[LAYOUT_PROFILE\] root_total:/g));
    assert.deepEqual(layouts.map(([layout]) => (layout.match(/\[LAYOUT_PROFILE\] settle:/g) || []).length), [2, 3, 3, 3]);
});
