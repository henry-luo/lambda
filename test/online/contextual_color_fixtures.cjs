const fixtures=[
    {name:'normal selects light',paint:'light-dark(red,lime)',expected:[255,0,0]},
    {name:'dark selects dark',paint:'light-dark(red,lime)',scheme:'dark',expected:[0,255,0]},
    {name:'declaration order does not select the scheme',paint:'light-dark(red,lime);color-scheme:dark',expected:[0,255,0]},
    {name:'nested elements inherit the scheme',paint:'light-dark(red,lime)',root:'color-scheme:dark',expected:[0,255,0]},
    {name:'normal resets an inherited dark scheme',paint:'light-dark(red,lime)',root:'color-scheme:dark',scheme:'normal',expected:[255,0,0]},
    {name:'inherited variable is computed at its owner',paint:'light-dark(red,lime)',root:'--scheme:dark;color-scheme:var(--scheme)',style:'--scheme:light',expected:[0,255,0]},
    {name:'unrecognized scheme uses the normal palette',paint:'light-dark(red,lime)',scheme:'custom-scheme',expected:[255,0,0]},
    {name:'relative rgb preserves alpha',paint:'rgb(from rgb(10 20 30/.6) r g b/50%)',expected:[132,137,142]},
    {name:'relative rgb evaluates channel math',paint:'rgb(from #336699 calc(r + 10) calc(g * .5) b/calc(alpha * .5))',expected:[158,153,204]},
    {name:'relative hsl uses numeric channel ranges',paint:'hsl(from rgb(255 0 0/.5) h s calc(l + 20))',expected:[255,179,179]},
    {name:'dark branch retains relative alpha',paint:'light-dark(red,rgb(from #fff r g b/80%))',scheme:'dark',body:'black',expected:[204,204,204]},
    {name:'relative source can use currentColor',paint:'rgb(from currentColor calc(r + 10) g b/.5)',style:'color:#336699',expected:[158,179,204]},
    {name:'gradient stops select the used scheme',paint:'linear-gradient(to right,light-dark(red,lime),light-dark(red,lime))',scheme:'dark',expected:[0,255,0]},
    {name:'pseudo element can override the scheme',pseudo:true,expected:[0,255,0]},
    {name:'scheme mutation refreshes retained descendants',paint:'light-dark(red,lime)',root:'color-scheme:light',script:"document.getElementById('box').getBoundingClientRect();document.documentElement.style.setProperty('color-scheme','dark')",expected:[0,255,0]},
    {name:'inherited foreground keeps its computed branch',paint:'currentColor',root:'color:light-dark(red,blue);color-scheme:light',scheme:'dark',expected:[255,0,0]},
    {name:'inherited currentColor mix is computed once',paint:'currentColor',root:'color:red',parent:'color:color-mix(in srgb,currentColor,blue)',expected:[128,0,128]},
    {name:'inherited foreground variables keep their owner',paint:'currentColor',parent:'--tone:red;color:var(--tone)',style:'--tone:blue',expected:[255,0,0]},
    {name:'invalid unselected branch does not replace a valid color',paint:'lime;background:light-dark(red,12px)',expected:[0,255,0]},
    {name:'feature queries use the shared color grammar',paint:'red',extra:'@supports(color-scheme:dark) and (color:light-dark(red,rgb(from blue r g b))){#box{background:lime}}',expected:[0,255,0]},
];
function fixtureHtml(f){
    return '<!doctype html><style>html{'+(f.root||'')+'}body{margin:0;background:'+(f.body||'white')+'}'+
        '#parent{'+(f.parent||'')+'}#box{width:100px;height:100px;background:'+(f.paint||'transparent')+';'+
        (f.scheme?'color-scheme:'+f.scheme+';':'')+(f.style||'')+'}'+
        (f.pseudo?'#box::before{content:"";display:block;width:100px;height:100px;color-scheme:dark;background:light-dark(red,lime)}':'')+
        (f.extra||'')+'</style><div id=parent><div id=box></div></div>'+
        '<div id=mirror style="position:absolute;left:150px;top:0;width:100px;height:100px"></div><script>'+
        (f.script||'')+';'+(!f.paint||!f.paint.includes('gradient(')?
        'document.getElementById("mirror").style.background=getComputedStyle(document.getElementById("box")'+
        (f.pseudo?',"::before"':'')+').backgroundColor;':'')+'</script>';
}
module.exports={fixtures,fixtureHtml};
