const fixtures=[
    {name:'zero layers across block wrappers',expected:[0,128,0]},
    {name:'zero foreground across an inline wrapper',inline:true,expected:[0,128,0]},
    {name:'foreground retains its ordinary ancestor clip',clip:true,expected:[0,128,0]},
    {name:'static opacity context owns both lifted layers',hidden:true,expected:[255,255,255]},
    {name:'zero effect layer paints once',alpha:true,expected:[128,64,0]},
    {name:'positive descendant escapes an auto-level relative ancestor',positive:true,expected:[0,0,255]},
    {name:'explicit zero context retains its positive descendant',positive:true,zero:true,expected:[255,0,0]},
];
function fixtureHtml(f) {
    const foreground='<div id="front">'+(f.positive?'<div id="positive"></div>':'')+'</div>';
    return '<!doctype html><style>html,body{margin:0;background:white}'+
        '#stage{position:relative;width:160px;height:120px}'+
        '#wallpaper{position:absolute;inset:0;background:red}'+
        '#front{position:relative;width:100px;height:80px;background:green;'+(f.alpha?'opacity:.5;':'')+(f.zero?'z-index:0;':'')+'}'+
        (f.clip?'#wrap{overflow:hidden;height:40px}':'')+(f.hidden?'#wrap{opacity:0}':'')+
        (f.positive?'#positive{position:absolute;inset:0;background:blue;z-index:2}#later{position:absolute;inset:0;background:red;z-index:1}':'')+
        '</style><div id="stage"><div id="wrap"><span><div id="wallpaper"></div></span>'+
        (f.inline?'<span>'+foreground+'</span>':foreground)+'</div>'+(f.positive?'<div id="later"></div>':'')+'</div>';
}
module.exports={fixtures,fixtureHtml};
