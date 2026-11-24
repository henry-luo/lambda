const {test}=require('node:test');
const assert=require('node:assert/strict');
const {renderFixtures,loggedResult}=require('./render_fixture.cjs');
const render=renderFixtures('style-old-values');

function exercise(mode) {
    const box=document.createElement('div');
    box.id='box';
    box.setAttribute('style','opacity:.4');
    document.body.appendChild(box);
    const observer=new MutationObserver(()=>{});
    observer.observe(box,{attributes:true,attributeOldValue:true,attributeFilter:['style']});
    const before=box.getAttribute('style');
    const initial=document.getElementById('witness').getBoundingClientRect().width;
    if(mode==='property')box.style.setProperty('opacity','0.6');
    else if(mode==='css-text')box.style.cssText='opacity:.6';
    else box.style.removeProperty('opacity');
    const records=observer.takeRecords();
    console.log('STYLE_OLD_VALUE_RESULT',JSON.stringify({
        count:records.length,old:records[0]?.oldValue,expected:before,
        initial,final:document.getElementById('witness').getBoundingClientRect().width,
    }));
}

for(const mode of ['property','css-text','remove'])test('CSSOM retains synthetic style old value: '+mode,async()=>{
    const {log}=await render(mode,`<!doctype html><style>
        #witness{width:40px;height:20px;background:blue}
        body:has([style*=".4"]) #witness{width:120px}
        </style><div id=witness></div><script>(${exercise.toString()})(${JSON.stringify(mode)})</script>`);
    const result=loggedResult(log,'STYLE_OLD_VALUE_RESULT');
    assert.equal(result.count,1);
    assert.equal(result.old,result.expected);
    assert.equal(result.initial,120);
    assert.equal(result.final,40);
});
