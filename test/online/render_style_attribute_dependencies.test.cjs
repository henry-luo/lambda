const {test}=require('node:test');
const assert=require('node:assert/strict');
const {renderFixtures,loggedResult}=require('./render_fixture.cjs');
const render=renderFixtures('style-attribute-dependencies');

function exercise(fixture) {
    const box=document.getElementById('box'),scope=document.getElementById('scope');
    const witness=document.getElementById('witness'),paint=document.getElementById('paint');
    const checks=[],start=box.getBoundingClientRect().x;
    const width=()=>witness.getBoundingClientRect().width;
    if(fixture.kind==='opacity'||fixture.kind==='transform') {
        for(let i=1;i<=3;i++) {
            if(fixture.kind==='opacity')box.style.opacity=String(.2+i*.2);
            else box.style.transform='translateX('+i*12+'px)';
            const rect=box.getBoundingClientRect();
            checks.push(rect.width===80&&width()===80&&(fixture.kind==='opacity'
                ?Math.abs(Number(getComputedStyle(box).opacity)-(.2+i*.2))<.001
                :Math.abs(rect.x-start-i*12)<.01));
        }
    } else if(fixture.kind==='size-query') {
        checks.push(getComputedStyle(paint).backgroundColor==='rgb(0, 255, 0)');
        scope.style.width='200px';
        checks.push(scope.getBoundingClientRect().width===200&&getComputedStyle(paint).backgroundColor==='rgb(255, 0, 0)');
        scope.style.width='400px';
        checks.push(scope.getBoundingClientRect().width===400&&getComputedStyle(paint).backgroundColor==='rgb(0, 255, 0)');
    } else {
        const initiallyMatching=fixture.kind==='empty'||fixture.kind==='quirks-case';
        checks.push(width()===(initiallyMatching?120:80));
        if(fixture.kind==='matching')box.style.transform='translateX(12px)';
        else box.style.opacity='0.4';
        checks.push(width()===(initiallyMatching?80:120));
        if(fixture.kind==='matching')box.style.transform='translateX(24px)';
        else box.style.opacity='0.6';
        checks.push(width()===80);
    }
    console.log('STYLE_DEPENDENCY_RESULT',JSON.stringify({checks}));
}

const cases=[
    {kind:'opacity',stable:true}, {kind:'transform',stable:true},
    {kind:'matching',selector:'[style*="12px"]'},
    {kind:'case-flag',selector:'[style*="OPACITY: 0.4" i]'},
    {kind:'empty',selector:'[style=""]',initial:''},
    {kind:'quirks-case',selector:'[style*="TRANSFORM"]',initial:'TRANSFORM:translateX(0px)',quirks:true},
    {kind:'size-query'},
];
for(const fixture of cases)test('style attribute dependency: '+fixture.kind,async()=>{
    const {log}=await render(fixture.kind,`${fixture.quirks?'':'<!doctype html>'}<style>
        #scope{container:site / inline-size;width:400px}
        #box{width:80px;height:80px}#paint{width:20px;height:20px;background:red}
        #witness{width:80px;height:10px}
        body:has(${fixture.selector||'[style*="writing-mode"]'}) #witness{width:120px}
        @container site (min-width:300px){#paint{background:lime}}
        </style><div id=scope><div id=box style="${fixture.initial??'transform:translateX(0px)'}"><div id=paint></div></div></div><div id=witness></div>
        <script>window.addEventListener('load',function(){(${exercise.toString()})(${JSON.stringify(fixture)})});</script>`,
        {...process.env,LAYOUT_DEBUG:'cache'});
    assert.deepEqual(loggedResult(log,'STYLE_DEPENDENCY_RESULT'),{checks:[true,true,true]});
    if(fixture.stable)assert.equal(/mutation-fallback: reason=broad-relational-css-risk/.test(log),false,
        'unchanged selector predicates must preserve the retained paint path');
});

test('layout-capable CSSOM writes settle container sizes with an unrelated shadow tree', async () => {
    const {log} = await render('shadow-container-query', `<!doctype html><style>
        body:has(.witness-state) #other{width:38px}
        #scope{container:panel / inline-size;width:var(--panel-width,80px)}
        #query{width:30px;height:20px}
        @container panel (min-width:100px){#query{width:60px}}
        </style><div id=shadow-host></div><div id=other></div>
        <section id=scope><div id=query></div></section><script>
        const root=document.getElementById('shadow-host').attachShadow({mode:'open'});
        root.innerHTML='<style>:host{display:block}</style><span>shadow</span>';
        window.addEventListener('load',function(){
            const scope=document.getElementById('scope'), query=document.getElementById('query');
            const widths=[query.offsetWidth];
            for(const width of [140,80]){
                scope.style.setProperty('--panel-width',width+'px');
                widths.push(query.offsetWidth);
            }
            console.log('SHADOW_CONTAINER_SIZE',JSON.stringify({widths}));
        });</script>`, {...process.env, LAYOUT_DEBUG:'cache', LAYOUT_PROFILE:'1'});
    assert.deepEqual(loggedResult(log, 'SHADOW_CONTAINER_SIZE'), {widths:[30,60,30]});
    const writes = Array.from(log.matchAll(/mutation-record:[^\n]+style=--panel-width:[^]*?\[LAYOUT_PROFILE\] root_total:/g));
    assert.equal(writes.length, 2);
    for (const [write] of writes) assert.doesNotMatch(write, /mutation-fallback: reason=broad-relational-css-risk/);
});

test('a shadow style predicate still recascades a sibling after a CSSOM write', async () => {
    const {log} = await render('shadow-style-predicate', `<!doctype html><style>
        #scope{container:inline-size;width:120px}
        @container (min-width:100px){#host{display:block}}
        </style><section id=scope><div id=host></div></section><script>
        const tree=document.getElementById('host').attachShadow({mode:'open'});
        tree.innerHTML='<style>#peer{width:30px;height:15px}.group:has([style*="--activated"]) #peer{width:60px}</style>'+
            '<div class="group"><span id="flag"></span><div id="peer"></div></div>';
        window.addEventListener('load',function(){
            const flag=tree.querySelector('#flag'), peer=tree.querySelector('#peer');
            const widths=[peer.offsetWidth];
            flag.style.setProperty('--activated','1');
            widths.push(peer.offsetWidth);
            flag.style.removeProperty('--activated');
            widths.push(peer.offsetWidth);
            console.log('SHADOW_STYLE_MATCH',JSON.stringify({widths}));
        });</script>`, {...process.env, LAYOUT_DEBUG:'cache'});
    assert.deepEqual(loggedResult(log, 'SHADOW_STYLE_MATCH'), {widths:[30,60,30]});
});
