const fixtures = [
    ['one factor','1',340], ['two factors','1 1',340],
    ['zero factors','0 0',340], ['explicit percent','1 1 0%',340],
    ['explicit pixels','1 1 0px',40], ['explicit unitless zero','1 1 0',40],
    ['auto basis','auto',340], ['inflexible auto','none',340],
].map(([name,flex,height])=>({name,flex,height}));
function fixtureHtml(f) {
    return '<!doctype html><style>html,body{margin:0;background:white}'+
        '#column{display:flex;flex-direction:column;width:200px}'+
        '#item{display:flex;flex-direction:column;flex:'+f.flex+';overflow:hidden;'+
        'padding:20px;background:green}#content{height:300px;background:blue}'+
        '#after{height:40px;background:yellow}</style><div id="column">'+
        '<div id="item"><div id="content"></div></div></div><div id="after"></div>';
}
module.exports={fixtures,fixtureHtml};
