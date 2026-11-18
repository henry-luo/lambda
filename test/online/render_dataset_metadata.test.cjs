const {test} = require('node:test');
const assert = require('node:assert/strict');
const {renderFixtures, rgb} = require('./render_fixture.cjs');
const render = renderFixtures('dataset-metadata');

function writeExpression(mode) {
    return mode === 'dataset' ? 'box.dataset.phase=value'
        : "box.setAttribute('data-phase',value)";
}

function source(mode, dependent, changing, writes=10) {
    return `<!doctype html><style>body{margin:0;background:white}
#box{width:100px;height:100px;background:red}
${dependent ? '#box[data-phase=wide]{width:200px}' : ''}
</style><div id=box data-phase=narrow></div><script>
const box=document.getElementById('box');
const observer=new MutationObserver(()=>{});
observer.observe(box,{attributes:true,attributeOldValue:true});
box.getBoundingClientRect();
const widths=[];
for(let i=0;i<${writes};i++){
    const value=${changing ? "i%2?'wide':'narrow'" : "'narrow'"};
    ${writeExpression(mode)};widths.push(box.getBoundingClientRect().width);
}

const records=observer.takeRecords();
console.log('DATASET_RESULT',JSON.stringify({widths,old:records.map(r=>r.oldValue),names:records.map(r=>r.attributeName)}));
</script>`;
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
            const match=/DATASET_RESULT (\{[^\n]+\})/.exec(log);
            assert.ok(match,log);
            const result=JSON.parse(match[1]);
            assert.deepEqual(result.names,Array(10).fill('data-phase'));
            assert.deepEqual(result.old,Array.from({length:10},(_,i)=>
                fixture.changing && i>0 && i%2===0 ? 'wide' : 'narrow'));
            assert.deepEqual(result.widths,Array.from({length:10},(_,i)=>
                fixture.dependent && i%2 ? 200 : 100));
            assert.deepEqual(rgb(image,150,50),fixture.dependent ? [255,0,0] : [255,255,255]);
            results.push((log.match(/\[LAYOUT_PROFILE\] retained-stage:/g)||[]).length);
        }
        assert.ok(results[0]>0,'layout profiling must be active');
        assert.equal(results[1],results[0],`dataset performed ${results[1]} layouts; setAttribute performed ${results[0]}`);
    });
}

test('identical CSS-dependent metadata preserves geometry while notifying observers', async () => {
    const env={...process.env,LAYOUT_PROFILE:'1'};
    const baseline=await render('unchanged-baseline',source('attribute',true,false,0),env);
    const layouts=log=>(log.match(/\[LAYOUT_PROFILE\] retained-stage:/g)||[]).length;
    assert.ok(layouts(baseline.log)>0,'layout profiling must be active');
    for(const mode of ['attribute','dataset']){
        const {log}=await render('unchanged-'+mode,source(mode,true,false),env);
        const match=/DATASET_RESULT (\{[^\n]+\})/.exec(log);
        assert.ok(match,log);
        const result=JSON.parse(match[1]);
        assert.deepEqual(result.widths,Array(10).fill(100));
        assert.deepEqual(result.names,Array(10).fill('data-phase'));
        assert.deepEqual(result.old,Array(10).fill('narrow'));
        assert.equal(layouts(log),layouts(baseline.log),mode+' should retain unchanged geometry');
    }
});

test('unchanged metadata custom-element callbacks can still invalidate geometry', async () => {
    for(const mode of ['attribute','dataset']){
        const {image,log}=await render('custom-element-'+mode,`<!doctype html>
<style>body{margin:0;background:white}meta-probe{display:block;width:100px;height:100px;background:red}
meta-probe[data-phase=narrow]{height:100px}</style><meta-probe id=box data-phase=narrow></meta-probe><script>
let calls=0;
customElements.define('meta-probe',class extends HTMLElement{
    static get observedAttributes(){return ['data-phase']}
    attributeChangedCallback(){calls++;this.style.width=(100+calls*10)+'px'}
});
calls=0;
const box=document.getElementById('box');box.style.width='100px';
const observer=new MutationObserver(()=>{});
observer.observe(box,{attributes:true,attributeFilter:['data-phase'],attributeOldValue:true});
box.getBoundingClientRect();const widths=[];
for(let i=0;i<10;i++){const value='narrow';${writeExpression(mode)};widths.push(box.getBoundingClientRect().width)}
console.log('CUSTOM_METADATA',JSON.stringify({calls,widths,old:observer.takeRecords().map(r=>r.oldValue)}));
</script>`);
        const match=/CUSTOM_METADATA (\{[^\n]+\})/.exec(log);
        assert.ok(match,log);
        const result=JSON.parse(match[1]);
        assert.equal(result.calls,10);
        assert.deepEqual(result.old,Array(10).fill('narrow'));
        assert.deepEqual(result.widths,Array.from({length:10},(_,i)=>110+i*10));
        assert.deepEqual(rgb(image,150,50),[255,0,0]);
    }
});
