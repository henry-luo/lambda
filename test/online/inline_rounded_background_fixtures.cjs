const fixtures = [
    {name:'oversized inline background stays within its fragment',extra:''},
    {name:'transformed inline background retains constrained geometry',extra:'transform:translate(40px,30px) scale(.8);transform-origin:0 0'},
    {name:'offscreen inline background cannot cover the viewport',extra:'margin-top:900px',offscreen:true},
];
function fixtureHtml(f,radius) {
    return '<!doctype html><style>html,body{margin:0;background:white}'+
        '#line{margin-left:100px;padding-top:100px;font:20px/30px Arial;'+f.extra+'}'+
        'span{background:lime;padding:10px;border-radius:'+radius+'px}</style>'+
        '<div id="line"><span>Rounded inline label</span></div>';
}
module.exports={fixtures,fixtureHtml};
