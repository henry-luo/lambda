const fixtures=[
    {name:'empty generated marker retains selected navigation flow',before:"''"},
    {name:'generated text retains selected navigation flow',before:"'A'"},
    {name:'both generated sides retain their own font styles',before:"'A'",after:true},
];
function fixtureHtml(f,probe){
    return '<!doctype html><style>body{font:20px/24px Arial}ul{display:flex;flex-wrap:wrap;justify-content:flex-end;padding:0;height:100%;align-items:center}li{list-style:none}a{position:relative;padding:0}a::before{content:'+f.before+';position:absolute;left:-12px;top:50%;width:6px;height:6px;font:12px/14px monospace;transform:translateY(-50%)}'+(f.after?'a::after{content:"B";font:10px/12px monospace;color:green}':'')+'</style><nav><ul><li><a href="#"><span>First label</span></a></li><li><a href="#"><span>Second label</span></a></li></ul></nav><section'+(probe?' data-radiant-layout="unregistered-probe"':'')+'></section>';
}
module.exports={fixtures,fixtureHtml};
