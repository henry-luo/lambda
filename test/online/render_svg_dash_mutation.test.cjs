const {test}=require('node:test');
const assert=require('node:assert/strict');
const {renderFixtures,rgb,layoutCount,loggedResult}=require('./render_fixture.cjs');
const render=renderFixtures('svg-dash-mutation');

function source(mode,writes,dependent=false,transition=false){
 const setter=mode==='style'?"target.style.strokeDashoffset=value":mode==='namespace'?
  "target.setAttributeNS(null,'stroke-dashoffset',value)":"target.setAttribute('stroke-dashoffset',value)";
 return `<!doctype html><head><style>body{margin:0;background:white}svg{display:block}
 #box{width:100px;height:20px;background:red;transform:translateX(0px)}
 ${dependent?'svg:has([stroke-dashoffset="10"])+#box{width:200px}':''}
 ${transition?'#ink{transition:stroke-dashoffset 1s linear}':''}
 </style></head><body><svg id=root xmlns="http://www.w3.org/2000/svg" width=120 height=40 viewBox="0 0 120 40">
 <g id=group><path id=ink d="M10 20H110" fill=none stroke=red stroke-width=12 stroke-dasharray="10 10"/></g></svg><div id=box></div><script>
 const svg=document.getElementById('root'),box=document.getElementById('box');
 const target=document.getElementById('${mode==='group'?'group':'ink'}');
 const observer=new MutationObserver(()=>{});observer.observe(target,{attributes:true,attributeOldValue:true});
 svg.getBoundingClientRect();const positions=[],widths=[],svgWidths=[];
 for(let i=1;i<=${writes};i++){const value=String(i%2?0:10);${setter};box.style.transform='translateX('+i+'px)';
 positions.push(box.getBoundingClientRect().x);widths.push(box.getBoundingClientRect().width);svgWidths.push(svg.getBoundingClientRect().width)}
 const records=observer.takeRecords();console.log('SVG_DASH_RESULT',JSON.stringify({positions,widths,svgWidths,names:records.map(r=>r.attributeName),old:records.map(r=>r.oldValue),targets:records.every(r=>r.target===target)}));
 </script></body>`;
}

for(const[mode,dependent,transition=false]of[['attribute',false],['namespace',false],['group',false],['style',false],['attribute',true],['attribute',false,true]]){
 test(`SVG stroke phase updates: ${mode}, dependent=${dependent}, transition=${transition}`,async()=>{
  const env={...process.env,LAYOUT_PROFILE:'1'};
  const baseline=await render('baseline',source(mode,0,dependent,transition),env);
  const actual=await render('writes',source(mode,10,dependent,transition),env);
  const result=loggedResult(actual.log,'SVG_DASH_RESULT');
  assert.deepEqual(rgb(baseline.image,15,20),[255,0,0],'initial dash is painted');
  // an active transition stays at its initial phase in this frozen zero-time fixture.
  assert.deepEqual(rgb(actual.image,15,20),transition?[255,0,0]:[255,255,255],'old ink follows the selected phase');
  assert.deepEqual(rgb(actual.image,25,20),transition?[255,255,255]:[255,0,0],'new ink follows the selected phase');
  assert.deepEqual(result.positions,Array.from({length:10},(_,i)=>i+1));
  assert.deepEqual(result.widths,Array.from({length:10},(_,i)=>dependent&&i%2?200:100));
  assert.deepEqual(result.svgWidths,Array(10).fill(120));
  assert.deepEqual(result.names,Array(10).fill(mode==='style'?'style':'stroke-dashoffset'));
  assert.equal(result.targets,true);
  if(mode!=='style')assert.deepEqual(result.old,[null,...Array.from({length:9},(_,i)=>String(i%2?10:0))]);
  assert.ok(layoutCount(baseline.log)>0,'profiling must be active');
  if(!dependent&&!transition)assert.equal(layoutCount(actual.log),layoutCount(baseline.log),'stroke phase changes keep HTML layout');
  else assert.ok(layoutCount(actual.log)>layoutCount(baseline.log),'selectors and transitions retain cascade and layout');
 });
}
