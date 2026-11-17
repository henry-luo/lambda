const fixtures=['flex','grid'].flatMap(display=>[-1,0,2].map(z=>({display,z,name:display+' static z-index '+z,expected:z<0?[255,0,0]:[0,128,0]})));
fixtures.push({display:'block',z:2,name:'ordinary static block ignores z-index',expected:[255,0,0]});
function fixtureHtml(f) {
    const overlap=f.display==='grid'?'grid-area:1/1;':f.display==='flex'?'margin-left:-80px;':'margin-top:-80px;';
    return '<!doctype html><style>html,body{margin:0;background:white}'+
        '#stage{position:relative;isolation:isolate;display:'+f.display+';width:160px;height:100px}'+
        '#back,#front{width:80px;height:80px;flex:none}#back{background:red}'+
        (f.display==='grid'?'#back{grid-area:1/1}':'')+
        '#front{background:green;'+overlap+'z-index:'+f.z+'}'+
        (f.z>=0?'#back{position:relative;z-index:'+(f.z===0?0:1)+'}':'')+
        '</style><div id="stage"><div id="back"></div><div id="front"></div></div>';
}
module.exports={fixtures,fixtureHtml};
