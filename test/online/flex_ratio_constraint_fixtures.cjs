const fixtures=[
    {name:'row main maximum remains an upper bound',constraint:'max-width:500px',expected:[0,0,200,100]},
    {name:'row percentage maximum remains an upper bound',constraint:'max-width:100%',expected:[0,0,200,100]},
    {name:'row cross maximum remains an upper bound',constraint:'max-height:300px',expected:[0,0,200,100]},
    {name:'row main minimum still clamps the result',constraint:'min-width:400px',expected:[0,0,400,200]},
    {name:'row cross minimum still transfers the ratio',constraint:'min-height:200px',expected:[0,0,400,200]},
    {name:'column main maximum remains an upper bound',direction:'column',constraint:'max-height:300px',expected:[200,0,200,100]},
    {name:'column cross maximum remains an upper bound',direction:'column',constraint:'max-width:500px',expected:[200,0,200,100]},
    {name:'definite cross size still transfers the ratio',constraint:'height:150px',expected:[0,0,300,150]},
    {name:'definite stretch still transfers the ratio',parent:'height:150px;align-items:stretch',expected:[0,0,300,150]},
    {name:'grid flex picture centers at intrinsic size',picture:true,expected:[200,0,200,100]},
    {name:'grid row height follows flex shrink',flow:'row',basis:'flex:0 1 400px',expected:[300,0,300,150]},
    {name:'grid row height follows flex growth',flow:'row',basis:'flex:1 0 0px',expected:[300,0,300,150]},
    {name:'grid column height follows natural cross widths',flow:'column',basis:'flex:none',expected:[200,120,200,100]},
    {name:'grid wrapped row height includes the line gap',flow:'row',basis:'flex:0 0 400px',wrap:true,expected:[0,220,400,200]},
];
function fixtureHtml(f){
    const image='data:image/svg+xml,'+encodeURIComponent('<svg xmlns="http://www.w3.org/2000/svg" width="200" height="100"><rect width="200" height="100" fill="lime"/></svg>');
    const styles=f.flow?
        '.container{display:grid;width:600px}.figure{display:flex;align-items:center;flex-direction:'+f.flow+';gap:20px 0;'+(f.wrap?'flex-wrap:wrap;':'')+'}.item{aspect-ratio:2;min-width:0;min-height:0;'+f.basis+'}.inside{width:200px;height:100px;background:lime}':f.picture?
        '.container{display:grid;width:600px;min-height:100px}.container>*{grid-area:1/1}.figure{display:flex;justify-content:center;align-self:stretch;justify-self:stretch;width:calc(100% + 0px);max-width:100%}picture{aspect-ratio:2;max-width:100%;height:auto}img{display:block;max-width:100%;height:auto}':
        '.container{display:flex;width:600px;align-items:center;flex-direction:'+(f.direction||'row')+';'+(f.parent||'')+'}.item{aspect-ratio:2;flex:0 0 auto;min-width:0;min-height:0;'+(f.constraint||'')+'}.inside{width:200px;height:100px;background:lime}';
    const content=f.flow?'<div class=container><div class=figure><div class=item><div class=inside></div></div><div id=item class=item><div class=inside></div></div></div></div>':f.picture?'<div class=container><div class=figure><picture id=item><img src="'+image+'" width=200 height=100></picture></div></div>':
        '<div class=container><div id=item class=item><div class=inside></div></div></div>';
    return '<!doctype html><style>body{margin:0;background:white}'+styles+'</style>'+content+
        '<script>const r=document.getElementById("item").getBoundingClientRect();console.log("FLEX_RATIO_MEASURE",JSON.stringify([r.x,r.y,r.width,r.height]));</script>';
}
module.exports={fixtures,fixtureHtml};
