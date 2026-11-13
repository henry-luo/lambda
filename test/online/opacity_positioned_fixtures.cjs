const fixtures = [
    {name:'zero opacity groups an absolute descendant outside its border box', parent:'opacity:0', expected:[255,255,255]},
    {name:'half opacity groups a zero-height parent', parent:'opacity:.5', expected:[255,127,127]},
    {name:'overlapping descendants composite once through the parent', parent:'opacity:.5', overlap:true, expected:[127,127,255]},
    {name:'a sized static opacity parent owns viewport-positioned children', parent:'opacity:0;height:200px', expected:[255,255,255]},
    {name:'fixed descendants retain the ancestor opacity scope', parent:'opacity:0', child:'position:fixed', expected:[255,255,255]},
    {name:'positive z descendants retain the existing opacity grouping', parent:'opacity:.5', child:'z-index:1', expected:[255,127,127]},
    {name:'a local relative containing block keeps its coordinate basis', parent:'opacity:.5', middle:'position:relative;height:80px', sampleY:230, expected:[255,127,127]},
    {name:'absolute descendants escape ordinary overflow below their containing block', parent:'opacity:1', middle:'overflow:hidden;width:200px;height:40px', expected:[255,0,0]},
];
function fixtureHtml(fixture) {
    return '<!doctype html><style>body{margin:0;background:white}#lead{height:200px}' +
        '.child{position:absolute;left:20px;top:20px;width:80px;height:60px;background:red}' +
        '</style><div id="lead"></div><div id="parent" style="'+fixture.parent+'">' +
        '<div id="middle" style="'+(fixture.middle||'')+'"><div class="child" style="'+(fixture.child||'')+'"></div>' +
        (fixture.overlap?'<div class="child" style="background:blue;left:25px"></div>':'') + '</div></div>';
}
module.exports = {fixtures, fixtureHtml};
