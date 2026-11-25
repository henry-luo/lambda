const {scriptResultFixture}=require('./script_result_fixture.cjs');
const fixtures=[
 {name:'indexed dynamic frame keeps fingerprint probes out of the parent',body:`
    const marker=document.createElement('p');marker.id='parent-marker';document.body.appendChild(marker);
    const index=self.length,holder=document.createElement('div'),fragment=document.createDocumentFragment();
    holder.innerHTML='<iframe></iframe>';fragment.appendChild(holder);document.body.appendChild(fragment);
    const frame=holder.firstElementChild,chosen=self[index]||self;
    chosen.document.body.innerHTML='<style>body{--probe:1}</style><p id="frame-marker">probe</p>';
    return {indexed:chosen===frame.contentWindow,parent:!!document.getElementById('parent-marker'),
        child:!!frame.contentDocument.getElementById('frame-marker'),count:self.length===index+1,
        frames:window.frames[index]===frame.contentWindow};`,
    expected:{indexed:true,parent:true,child:true,count:true,frames:true}},
 {name:'initial frame exposes a Window index',body:`return {count:window.length,
    indexed:self[0]===document.querySelector('iframe').contentWindow};`,initial:'<iframe></iframe>',expected:{count:1,indexed:true}},
 {name:'frame removal and innerHTML cleanup retire shifted indices',body:`
    const holder=document.createElement('div');document.body.appendChild(holder);
    holder.innerHTML='<iframe id="one"></iframe><iframe id="two"></iframe>';
    const one=holder.children[0],two=holder.children[1],second=two.contentWindow;
    const first=window[0]===one.contentWindow&&window[1]===second&&window.length===2;
    one.remove();const shifted=window.length===1&&window[0]===second&&window[1]===undefined;
    holder.innerHTML='';return {first,shifted,empty:window.length===0&&window[0]===undefined};`,
    expected:{first:true,shifted:true,empty:true}},
 {name:'frame moves retain the current set of indexed contexts',body:`
    const holder=document.createElement('div');document.body.appendChild(holder);
    holder.innerHTML='<iframe id="one"></iframe><iframe id="two"></iframe>';
    const one=holder.children[0],two=holder.children[1];
    holder.insertBefore(two,one);const indexed=[window[0],window[1]];
    return {count:window.length,complete:indexed.includes(one.contentWindow)&&indexed.includes(two.contentWindow),distinct:indexed[0]!==indexed[1]};`,
    expected:{count:2,complete:true,distinct:true}},
];
function fixtureHtml(fixture){return scriptResultFixture(`
    const result=(function(){${fixture.body}})();window.__windowFrameResult=result;
    console.log('WINDOW_FRAME_DETAILS',JSON.stringify(result));
    check(JSON.stringify(result)===${JSON.stringify(JSON.stringify(fixture.expected))});`,
    'WINDOW_FRAME_ERROR','WINDOW_FRAME_RESULT').replace('<body>','<body>'+(fixture.initial||''));}
module.exports={fixtures,fixtureHtml};
