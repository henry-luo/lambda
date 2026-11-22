const cases=[
    {name:'text maximum width',style:'max-width:40px',expected:[0,0,40,20]},
    {name:'text maximum height',style:'max-height:20px',expected:[0,0,40,20]},
    {name:'block text maximum width',display:'block',style:'max-width:40px',expected:[0,0,40,20]},
    {name:'automatic text cross size',expected:[0,0,100,50]},
    {name:'text grows after its basis',style:'flex:1 1 auto',expected:[0,0,200,100]},
    {name:'automatic minimum exceeds an explicit basis',style:'flex:0 0 10px',expected:[0,0,100,50]},
    {name:'automatic minimum survives main shrink',parent:'height:10px',expected:[0,0,100,50]},
    {name:'zero minimum permits main shrink',parent:'height:10px',style:'min-height:0',expected:[0,0,20,10]},
    {name:'explicit cross size',style:'width:80px',expected:[0,0,80,40]},
    {name:'explicit main size',style:'height:30px',expected:[0,0,60,30]},
    {name:'stretch supplies cross size',parent:'align-items:stretch',expected:[0,0,100,50]},
    {name:'wrapping text cross size',parent:'flex-wrap:wrap',expected:[0,0,100,50]},
    {name:'proportional fixed content',fixed:100,expected:[0,0,200,100]},
    {name:'tall fixed content',fixed:300,expected:[0,0,600,300]},
    {name:'scrollable fixed content',fixed:300,style:'overflow:auto',expected:[0,0,200,100]},
    {name:'fixed content with zero minimum',fixed:300,style:'min-height:0',expected:[0,0,200,100]},
    {name:'fixed content maximum width',fixed:300,style:'max-width:80px',expected:[0,0,80,40]},
];

const columnRatioFixtures=cases.map(fixture=>({
    name:'column ratio fit: '+fixture.name,text:true,direction:'column',
    parent:'width:100px;height:100px;'+(fixture.parent||''),
    constraint:'flex:0 1 auto;display:'+(fixture.display||'flex')+';'+(fixture.style||''),
    ...(fixture.fixed?{content:`<div style="width:200px;height:${fixture.fixed}px;background:lime"></div>`}:{}),
    expected:fixture.expected,
}));
module.exports={columnRatioFixtures};
