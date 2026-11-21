const {test} = require('node:test');
const assert = require('node:assert/strict');
const {renderFixtures, rgb, layoutCount: layouts, loggedResult} = require('./render_fixture.cjs');
const {scriptResultFixture}=require('./script_result_fixture.cjs');
const render = renderFixtures('dataset-metadata');

function resultFromLog(log,tag='DATASET_RESULT'){
    return loggedResult(log,tag);
}

function writeExpression(mode) {
    const inert={
        'inert-attribute':"box.setAttribute('inert','')",
        'inert-namespace':"box.setAttributeNS(null,'inert','')",
        'inert-property':'box.inert=true',
        'inert-value':"box.setAttribute('inert',value)",
    };
    if(inert[mode])return inert[mode];
    if(mode==='namespace')return "box.setAttributeNS(null,'data-phase',value)";
    if(mode==='remove')return "box.removeAttribute('data-phase')";
    if(mode==='remove-ns')return "box.removeAttributeNS(null,'data-phase')";
    return mode === 'dataset' ? 'box.dataset.phase=value'
        : "box.setAttribute('data-phase',value)";
}

for(const mode of ['inert-attribute','inert-namespace','inert-property'])test('stable inert state retains geometry: '+mode,async()=>{
    const element='<style>#box[inert]{width:200px}</style><div id=box inert=""></div>',env={...process.env,LAYOUT_PROFILE:'1'};
    const baseline=await render(mode+'-baseline',source(mode,false,false,0,element),env);
    const actual=await render(mode,source(mode,false,false,10,element),env),result=resultFromLog(actual.log);
    assert.deepEqual(result.names,Array(10).fill('inert'));
    assert.deepEqual(result.old,Array(10).fill(''));
    assert.deepEqual(result.widths,Array(10).fill(200));
    assert.deepEqual(rgb(actual.image,150,50),[255,0,0]);
    assert.ok(layouts(baseline.log)>0,'profiling must be active');
    assert.equal(layouts(actual.log),layouts(baseline.log),'an already inert element keeps the same CSS/focus state');
});

for(const dependent of [false,true])test('null-namespace metadata shares invalidation: dependent='+dependent,async()=>{
    const env={...process.env,LAYOUT_PROFILE:'1'},baseline=await render('namespace-baseline',source('namespace',dependent,true,0),env);
    const actual=await render('namespace-writes',source('namespace',dependent,true,10),env),result=resultFromLog(actual.log);
    assert.deepEqual(result.names,Array(10).fill('data-phase'));
    assert.deepEqual(result.widths,Array.from({length:10},(_,i)=>dependent&&i%2?200:100));
    assert.deepEqual(result.old,Array.from({length:10},(_,i)=>i>0&&i%2===0?'wide':'narrow'));
    assert.ok(layouts(baseline.log)>0,'profiling must be active');
    if(!dependent)assert.equal(layouts(actual.log),layouts(baseline.log),'unrelated metadata is independent of the namespace entry point');
});

function source(mode, dependent, changing, writes=10, element='<div id=box data-phase=narrow></div>') {
    const attribute=mode.startsWith('inert-')?'inert':'data-phase';
    return `<!doctype html><style>body{margin:0;background:white}
#box,#probe{width:100px;height:100px;background:red}
${dependent ? `#box[${attribute}=wide],#box[${attribute}=wide]+#probe{width:200px}` : ''}
</style><body>${element}<script>
const box=document.getElementById('box');
const measure=document.getElementById('probe')||box;
const observer=new MutationObserver(()=>{});
observer.observe(box,{attributes:true,attributeOldValue:true});
measure.getBoundingClientRect();
const widths=[];
for(let i=0;i<${writes};i++){
    const value=${changing ? "i%2?'wide':'narrow'" : "'narrow'"};
    ${writeExpression(mode)};widths.push(measure.getBoundingClientRect().width);
}

const records=observer.takeRecords();
console.log('DATASET_RESULT',JSON.stringify({widths,old:records.map(r=>r.oldValue),names:records.map(r=>r.attributeName)}));
</script>`;
}

test('inert lexical value changes retain selector invalidation',async()=>{
    const actual=await render('inert-values',source('inert-value',true,true,10,'<div id=box inert="narrow"></div>'));
    const result=resultFromLog(actual.log);
    assert.deepEqual(result.names,Array(10).fill('inert'));
    assert.deepEqual(result.widths,Array.from({length:10},(_,i)=>i%2?200:100));
});

for(const rel of ['preload','icon'])for(const dependent of [false,true]){
    test(`link metadata preserves CSS dependencies: ${rel}, dependent=${dependent}`,async()=>{
        const element=`<link id=box rel=${rel} data-phase=narrow><div id=probe></div>`;
        const env={...process.env,LAYOUT_PROFILE:'1'};
        const baseline=await render(rel+'-baseline',source('attribute',dependent,true,0,element),env);
        for(const mode of ['attribute','dataset']){
            const {log}=await render(rel+'-'+mode,source(mode,dependent,true,10,element),env);
            const result=resultFromLog(log);
            assert.deepEqual(result.names,Array(10).fill('data-phase'));
            assert.deepEqual(result.old,Array.from({length:10},(_,i)=>i>0&&i%2===0?'wide':'narrow'));
            assert.deepEqual(result.widths,Array.from({length:10},(_,i)=>dependent&&i%2?200:100));
            assert.ok(layouts(baseline.log)>0,'profiling must be active');
            if(!dependent)assert.equal(layouts(log),layouts(baseline.log),'unrelated metadata must retain geometry');
        }
    });
}

for (const fixture of [
    {name:'identical metadata', dependent:false, changing:false},
    {name:'changed metadata', dependent:false, changing:true},
    {name:'CSS-dependent values', dependent:true, changing:true},
]) {
    test('dataset shares attribute invalidation: ' + fixture.name, async () => {
        const results=[];
        for (const mode of ['attribute','dataset']) {
            const {image,log}=await render(mode+'-'+fixture.name.replaceAll(' ','-'),
                source(mode,fixture.dependent,fixture.changing), {...process.env,LAYOUT_PROFILE:'1'});
            const result=resultFromLog(log);
            assert.deepEqual(result.names,Array(10).fill('data-phase'));
            assert.deepEqual(result.old,Array.from({length:10},(_,i)=>
                fixture.changing && i>0 && i%2===0 ? 'wide' : 'narrow'));
            assert.deepEqual(result.widths,Array.from({length:10},(_,i)=>
                fixture.dependent && i%2 ? 200 : 100));
            assert.deepEqual(rgb(image,150,50),fixture.dependent ? [255,0,0] : [255,255,255]);
            results.push(layouts(log));
        }
        assert.ok(results[0]>0,'layout profiling must be active');
        assert.equal(results[1],results[0],`dataset performed ${results[1]} layouts; setAttribute performed ${results[0]}`);
    });
}

test('identical CSS-dependent metadata preserves geometry while notifying observers', async () => {
    const env={...process.env,LAYOUT_PROFILE:'1'};
    const baseline=await render('unchanged-baseline',source('attribute',true,false,0),env);
    assert.ok(layouts(baseline.log)>0,'layout profiling must be active');
    for(const mode of ['attribute','dataset']){
        const {log}=await render('unchanged-'+mode,source(mode,true,false),env);
        const result=resultFromLog(log);
        assert.deepEqual(result.widths,Array(10).fill(100));
        assert.deepEqual(result.names,Array(10).fill('data-phase'));
        assert.deepEqual(result.old,Array(10).fill('narrow'));
        assert.equal(layouts(log),layouts(baseline.log),mode+' should retain unchanged geometry');
    }
});

test('unchanged metadata custom-element callbacks can still invalidate geometry', async () => {
    for(const mode of ['attribute','dataset','namespace','inert-attribute','inert-namespace','inert-property']){
        const inert=mode.startsWith('inert-'),attribute=inert?'inert':'data-phase';
        const {image,log}=await render('custom-element-'+mode,`<!doctype html>
<style>body{margin:0;background:white}meta-probe{display:block;width:100px;height:100px;background:red}
meta-probe[data-phase=narrow]{height:100px}</style><meta-probe id=box data-phase=narrow ${inert?'inert=""':''}></meta-probe><script>
let calls=0;
customElements.define('meta-probe',class extends HTMLElement{
    static get observedAttributes(){return [${JSON.stringify(attribute)}]}
    attributeChangedCallback(){calls++;this.style.width=(100+calls*10)+'px'}
});
calls=0;
const box=document.getElementById('box');box.style.width='100px';
const observer=new MutationObserver(()=>{});
observer.observe(box,{attributes:true,attributeFilter:[${JSON.stringify(attribute)}],attributeOldValue:true});
box.getBoundingClientRect();const widths=[];
for(let i=0;i<10;i++){const value='narrow';${writeExpression(mode)};widths.push(box.getBoundingClientRect().width)}
console.log('CUSTOM_METADATA',JSON.stringify({calls,widths,old:observer.takeRecords().map(r=>r.oldValue)}));
</script>`);
        const result=resultFromLog(log,'CUSTOM_METADATA');
        assert.equal(result.calls,10);
        assert.deepEqual(result.old,Array(10).fill(inert?'':'narrow'));
        assert.deepEqual(result.widths,Array.from({length:10},(_,i)=>110+i*10));
        assert.deepEqual(rgb(image,150,50),[255,0,0]);
    }
});

test('inert state changes retain focus and selector effects',async()=>{
    const {image,log}=await render('inert-state',scriptResultFixture(`
 const box=document.createElement('div');box.id='probe';const button=document.createElement('button');box.append(button);document.body.append(box);
 const observer=new MutationObserver(()=>{});observer.observe(box,{attributes:true,attributeOldValue:true});
 button.focus();check(document.activeElement===button&&box.getBoundingClientRect().height===40);
 box.inert=true;check(document.activeElement!==button&&box.getBoundingClientRect().width===200&&box.getBoundingClientRect().height===30);
 box.inert=true;check(box.getBoundingClientRect().width===200);
 box.inert=false;check(box.getBoundingClientRect().width===100);button.focus();check(document.activeElement===button&&box.getBoundingClientRect().height===40);
 const records=observer.takeRecords();check(records.length===3&&records.every(r=>r.attributeName==='inert'));
 if(records.length===3)check(records[0].oldValue===null&&records[1].oldValue===''&&records[2].oldValue==='');
 `,'INERT_ERROR','INERT_RESULT','<style>#probe{width:100px;height:30px}#probe[inert]{width:200px}#probe:focus-within{height:40px}</style>'));
    assert.deepEqual(rgb(image,10,10),[0,255,0],log.slice(-1600));
});

test('null-namespace metadata preserves colliding attribute identities',async()=>{
    const {image,log}=await render('metadata-namespaces',scriptResultFixture(`
 for(const foreignFirst of [false,true]){
 const box=document.createElement('div');document.body.append(box);
 if(!foreignFirst)box.setAttributeNS(null,'data-phase','local');
 box.setAttributeNS('urn:foreign','data-phase','foreign');
 if(foreignFirst)box.setAttributeNS(null,'data-phase','local');
 const local=box.getAttributeNodeNS(null,'data-phase'),foreign=box.getAttributeNodeNS('urn:foreign','data-phase');
 box.setAttributeNS(null,'data-phase','changed');
 check(box.getAttributeNodeNS(null,'data-phase')===local&&local.value==='changed');
 check(box.getAttributeNodeNS('urn:foreign','data-phase')===foreign&&foreign.value==='foreign');
 }
 `,'METADATA_NS_ERROR','METADATA_NS_RESULT'));
    assert.deepEqual(rgb(image,10,10),[0,255,0],log.slice(-1600));
});

for(const mode of ['remove','remove-ns'])test('absent attribute removal has no mutation or reflow: '+mode,async()=>{
    const env={...process.env,LAYOUT_PROFILE:'1'};
    const baseline=await render(mode+'-single',source(mode,true,false,1),env);
    const repeated=await render(mode+'-repeated',source(mode,true,false,10),env);
    const result=resultFromLog(repeated.log);
    assert.deepEqual(result.names,['data-phase']);
    assert.deepEqual(result.old,['narrow']);
    assert.deepEqual(result.widths,Array(10).fill(100));
    assert.ok(layouts(baseline.log)>0,'layout profiling must be active');
    assert.equal(layouts(repeated.log),layouts(baseline.log),'absent removals must retain geometry');
});

for(const mode of ['remove','remove-ns'])test('empty attribute removal notifies once: '+mode,async()=>{
    const {log}=await render(mode+'-empty-custom',`<!doctype html><meta-probe id=box data-phase></meta-probe><script>
let calls=[];
customElements.define('meta-probe',class extends HTMLElement{
    static get observedAttributes(){return ['data-phase']}
    attributeChangedCallback(name,oldValue,newValue){calls.push([oldValue,newValue])}
});
calls=[];const box=document.getElementById('box');
const observer=new MutationObserver(()=>{});
observer.observe(box,{attributes:true,attributeOldValue:true});
${writeExpression(mode)};${writeExpression(mode)};
console.log('EMPTY_REMOVAL',JSON.stringify({calls,present:box.hasAttribute('data-phase'),
    old:observer.takeRecords().map(r=>r.oldValue)}));
</script>`);
    const match=/EMPTY_REMOVAL (\{[^\n]+\})/.exec(log);
    assert.ok(match,log);
    assert.deepEqual(JSON.parse(match[1]),{calls:[['',null]],present:false,old:['']});
});
