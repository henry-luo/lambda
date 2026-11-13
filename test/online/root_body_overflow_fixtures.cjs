const fixtures = [
    {name:'body scroll overflow propagates to the viewport', root:'', body:'overflow:scroll', expected:[0,255,0]},
    {name:'explicit root overflow keeps body clipping local', root:'overflow:hidden', body:'overflow:scroll', expected:[255,255,255]},
    {name:'root style containment disables body propagation', root:'contain:style', body:'overflow:scroll', expected:[255,255,255]},
    {name:'body layout containment disables propagation', root:'', body:'overflow:scroll;contain:layout', expected:[255,255,255]},
    {name:'body hidden overflow propagates without clipping its box', root:'', body:'overflow:hidden', expected:[0,255,0]},
    {name:'an ordinary box retains its own overflow clip', root:'', body:'overflow:visible', ordinary:true, expected:[255,255,255]},
    {name:'body auto overflow propagates to the viewport', root:'', body:'overflow:auto', expected:[0,255,0]},
    {name:'body paint containment retains local clipping', root:'', body:'overflow:scroll;contain:paint', expected:[255,255,255]},
];
function fixtureHtml(fixture) {
    const child='<div style="position:absolute;left:20px;top:20px;width:80px;height:60px;background:lime"></div>';
    return '<!doctype html><html style="min-height:100%;'+fixture.root+'"><style>body{margin:0;position:relative}</style><body style="'+fixture.body+'">'+
        (fixture.ordinary?'<div style="position:relative;height:0;overflow:hidden">'+child+'</div>':child)+'</body></html>';
}
module.exports={fixtures,fixtureHtml};
