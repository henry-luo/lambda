const {test}=require('node:test');
const assert=require('node:assert/strict');
const {renderFixtures,rgb,layoutCount:profileCount,loggedResult:logged}=require('./render_fixture.cjs');
const {scriptResultFixture}=require('./script_result_fixture.cjs');
const render=renderFixtures('inline-style-mutation');
const cases=[
 ['observers retain original style values',`
 const box=document.getElementById('result');box.style.background='red';
 const observer=new MutationObserver(()=>{});observer.observe(box,{attributes:true,attributeOldValue:true});
 box.style.opacity='0.5';box.style.setProperty('width','40px');box.style.removeProperty('opacity');
 const records=observer.takeRecords();check(records.length===3&&records.every(r=>r.attributeName==='style'&&r.target===box));
 if(records.length===3)check(!records[0].oldValue.includes('opacity')&&records[1].oldValue.includes('0.5')&&records[2].oldValue.includes('40px'));
 `],
 ['style attribute selectors see CSSOM updates',`
 const box=document.getElementById('result'),signal=document.createElement('div');signal.id='signal';box.after(signal);
 const sheet=document.createElement('style');sheet.textContent='#signal{width:100px;height:10px}#result[style*=opacity]+#signal{width:200px}';document.head.append(sheet);
 check(signal.getBoundingClientRect().width===100);box.style.opacity='0.5';check(signal.getBoundingClientRect().width===200);
 box.style.removeProperty('opacity');check(signal.getBoundingClientRect().width===100);
 box.style='opacity:0.5';check(signal.getBoundingClientRect().width===200);
 box.style='';check(signal.getBoundingClientRect().width===100);
 `],
 ['custom element callbacks retain geometry mutations',`
 const signal=document.createElement('div');signal.style.width='100px';document.body.append(signal);
 let calls=0;customElements.define('style-probe',class extends HTMLElement{
 static get observedAttributes(){return ['style']}
 attributeChangedCallback(name,oldValue,newValue){calls++;signal.style.width='200px';check(name==='style'&&oldValue===null&&newValue.includes('opacity'))}
 });
 const box=document.createElement('style-probe');document.body.append(box);signal.getBoundingClientRect();
 box.style.opacity='0.5';check(calls===1&&signal.getBoundingClientRect().width===200);
 `],
 ['cssText retains attribute records and layout updates',`
 const box=document.createElement('div');document.body.append(box);
 const observer=new MutationObserver(()=>{});observer.observe(box,{attributes:true,attributeOldValue:true});
 box.style.cssText='width:100px;height:10px';check(box.getBoundingClientRect().width===100);
 box.style.cssText='width:200px;height:10px';check(box.getBoundingClientRect().width===200);
 const records=observer.takeRecords();check(records.length===2);
 if(records.length===2)check(records[0].oldValue===null&&records[1].oldValue.includes('100px'));
 `],
 ['transform establishment updates fixed containing blocks',`
 const box=document.createElement('div');box.style.cssText='position:relative;margin-top:40px;width:100px;height:100px';
 const fixed=document.createElement('div');fixed.style.cssText='position:fixed;top:5px;left:0;width:10px;height:10px';box.append(fixed);document.body.append(box);
 check(fixed.getBoundingClientRect().y===5);const top=box.getBoundingClientRect().y;
 box.style.transform='translateX(30px)';check(fixed.getBoundingClientRect().y===top+5);
 box.style.transform='none';check(fixed.getBoundingClientRect().y===5);
 `],
 ['transform substitutions and resets update containing blocks',`
 const box=document.createElement('div');box.style.cssText='position:relative;margin-top:40px;width:100px;height:100px;--clear:none';
 const fixed=document.createElement('div');fixed.style.cssText='position:fixed;top:5px;left:0;width:10px;height:10px';box.append(fixed);document.body.append(box);
 const top=box.getBoundingClientRect().y;
 for(const[property,value]of[['transform','translateX(30px)'],['translate','30px'],['rotate','0deg'],['scale','1']]){
 for(const reset of ['var(--clear)','initial','unset','inherit']){
 box.style[property]=value;check(fixed.getBoundingClientRect().y===top+5);
 box.style[property]=reset;const y=fixed.getBoundingClientRect().y;
 console.log('TRANSFORM_RESET',property,reset,y);check(y===5);
 }
 }
 `],
];
for(const[name,source]of cases)test('inline style mutation: '+name,async()=>{
 const {image,log}=await render(name.replaceAll(' ','-'),scriptResultFixture(source,'INLINE_STYLE_ERROR','INLINE_STYLE_RESULT'));
 assert.deepEqual(rgb(image,10,10),[0,255,0],log.slice(-1800));
});
function transformSource(writes,extraCss=''){return `<!doctype html><style>body{margin:0;background:white}#box{width:100px;height:100px;background:red;transform:translateX(0px)}${extraCss}</style><div id=box></div><script>
const box=document.getElementById('box');box.getBoundingClientRect();const positions=[];
for(let i=1;i<=${writes};i++){box.style.transform='translateX('+i*10+'px)';positions.push(box.getBoundingClientRect().x)}
console.log('TRANSFORM_MUTATION',JSON.stringify({positions}));</script>`}
for(const[name,css,retained]of[
 ['plain','',true],
 ['unrelated keyframes','@keyframes ripple{to{opacity:0.5}}',true],
 ['quoted and commented attr','@keyframes ripple{to{content:"attr(style)";/* attr(style) */opacity:0.5}}',true],
 ['keyframe attribute reference','@keyframes ripple{to{width:attr(data-width type(<length>),100px)}}',false],
 ['escaped keyframe attribute reference',String.raw`@keyframes ripple{to{width:a\74 tr(data-width type(<length>),100px)}}`,false],
])test('inline transform geometry: '+name,async()=>{
 const env={...process.env,LAYOUT_PROFILE:'1'},baseline=await render('transform-baseline',transformSource(0,css),env),actual=await render('transform-writes',transformSource(10,css),env);
 assert.deepEqual(logged(actual.log,'TRANSFORM_MUTATION').positions,Array.from({length:10},(_,i)=>(i+1)*10));
 assert.deepEqual(rgb(actual.image,150,50),[255,0,0]);
 assert.ok(profileCount(baseline.log)>0,'layout profiling must be active');
 if(retained)assert.equal(profileCount(actual.log),profileCount(baseline.log),'unrelated keyframes must retain transform geometry');
 else assert.ok(profileCount(actual.log)>profileCount(baseline.log),'keyframe attribute references must retain invalidation');
});
