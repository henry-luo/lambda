const fixtures=[
 {name:'nowrap control',wrap:'nowrap',expected:{x:260,width:80,height:40,children:[260]}},
 {name:'wrapping row remains content sized',expected:{x:260,width:80,height:40,children:[260]}},
 {name:'reverse wrapping remains content sized',wrap:'wrap-reverse',expected:{x:260,width:80,height:40,children:[260]}},
 {name:'multiple items include their gap',widths:[80,60],gap:20,expected:{x:220,width:160,height:40,children:[220,320]}},
 {name:'available cross space wraps the contents',outerWidth:100,widths:[80,80],gap:20,expected:{x:0,width:100,height:100,children:[0,0]}},
 {name:'maximum width clamps before wrapping',inner:'max-width:100px',widths:[80,80],gap:20,expected:{x:250,width:100,height:100,children:[250,250]}},
 {name:'padding and border remain in the fitted box',inner:'padding:10px;border:2px solid navy',expected:{x:248,width:104,height:64,children:[260]}},
 {name:'minimum width remains a lower bound',inner:'min-width:120px',expected:{x:240,width:120,height:40,children:[240]}},
 {name:'explicit width control',inner:'width:200px',expected:{x:200,width:200,height:40,children:[200]}},
 {name:'stretch control',align:'stretch',expected:{x:0,width:600,height:40,children:[0]}},
 {name:'start aligns the fitted width',align:'flex-start',expected:{x:0,width:80,height:40,children:[0]}},
 {name:'end aligns the fitted width',align:'flex-end',expected:{x:520,width:80,height:40,children:[520]}},
 {name:'automatic margins control',align:'stretch',inner:'margin-left:auto;margin-right:auto',expected:{x:260,width:80,height:40,children:[260]}},
 {name:'row parent control',direction:'row',expected:{x:260,width:80,height:40,children:[260]}},
];
function fixtureHtml(fixture) {
 return `<!doctype html><style>html,body{margin:0}#outer{width:${fixture.outerWidth||600}px;height:160px;
    display:flex;flex-direction:${fixture.direction||'column'};align-items:${fixture.align||'center'};justify-content:center}
    #inner{display:flex;flex-wrap:${fixture.wrap||'wrap'};gap:${fixture.gap||0}px;background:blue;${fixture.inner||''}}
    .item{height:40px;flex:none;background:lime}</style><body><div id=outer><div id=inner>
    ${(fixture.widths||[80]).map(width=>`<div class=item style="width:${width}px"></div>`).join('')}
    </div></div><script>addEventListener('load',()=>{
    const inner=document.getElementById('inner'),r=inner.getBoundingClientRect();
    const result={x:r.x,width:r.width,height:r.height,children:Array.from(inner.children,e=>e.getBoundingClientRect().x)};
    window.__flexCrossResult=result;console.log('FLEX_WRAP_CROSS_RESULT',JSON.stringify(result));
    });</script>`;
}
module.exports={fixtures,fixtureHtml};
