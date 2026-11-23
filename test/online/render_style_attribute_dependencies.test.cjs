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
