const {scriptResultFixture}=require('./script_result_fixture.cjs');
const cases=[
 {name:'ordinary text retains inherited direction',tag:'div',broad:false},
 {name:'explicit direction stops an automatic ancestor',tag:'div',dir:'ltr',outer:true,broad:false},
 {name:'automatic text follows first strong characters',tag:'div',dir:'auto',broad:true},
 {name:'bdi retains automatic direction updates',tag:'bdi',broad:true},
 {name:'hidden text still contributes first strong characters',tag:'div',dir:'auto',hidden:true,broad:true},
 {name:'ordinary control value retains inherited direction',tag:'input',value:true,broad:false},
 {name:'automatic input value updates its own direction',tag:'input',dir:'auto',value:true,broad:true},
 {name:'automatic textarea value updates its own direction',tag:'textarea',dir:'auto',value:true,broad:true},
];
function fixtureHtml(fixture){
 const attrs=`id=scope${fixture.dir?' dir='+fixture.dir:''}`;
 const content=fixture.hidden?'<span id=text-holder hidden>English</span>':'English';
 const scope=fixture.tag==='input'?`<input ${attrs} value=English>`:`<${fixture.tag} ${attrs}>${content}</${fixture.tag}>`;
 const nodes=scope+'<div id=paint></div>';
 const head=`<meta charset=utf-8><style>#paint{width:100px;height:40px;background:blue}#scope:dir(rtl)+#paint{width:200px}</style>`;
 const source=`
 const holder=document.createElement('section');
 ${fixture.outer?"holder.dir='auto';":''}
 holder.innerHTML=${JSON.stringify(nodes)};document.body.append(holder);
 const scope=document.getElementById('scope'),paint=document.getElementById('paint'),widths=[];
 paint.getBoundingClientRect();
 for(const text of ['אבג','English','אבג']){
  ${fixture.value?'scope.value=text':(fixture.hidden?"document.getElementById('text-holder')":"scope")+'.firstChild.data=text'};
  widths.push(paint.getBoundingClientRect().width);
 }
 const expected=${JSON.stringify(fixture.broad?[200,100,200]:[100,100,100])};
 check(widths.every((width,i)=>width===expected[i]));
 console.log('DIRECTION_MUTATION',JSON.stringify({widths}));
 `;
 return scriptResultFixture(source,'DIRECTION_MUTATION_ERROR','DIRECTION_MUTATION_RESULT',head,true);
}
module.exports={cases,fixtureHtml};
