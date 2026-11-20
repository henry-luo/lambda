const fixtures=[
    {name:'visible backdrop preserves its foreground',style:'backdrop-filter:blur(8px)',red:true,blur:true},
    {name:'ordinary filter still blurs its foreground',style:'filter:blur(8px)',noBlur:true},
    {name:'transparent backdrop has no effect',style:'opacity:0;backdrop-filter:blur(8px)',hidden:true},
    {name:'transparent ancestor suppresses backdrop',style:'backdrop-filter:blur(8px)',hidden:true,ancestor:'opacity:0'},
    {name:'hidden backdrop has no effect',style:'visibility:hidden;backdrop-filter:blur(8px)',hidden:true},
    {name:'hidden ancestor suppresses backdrop',style:'backdrop-filter:blur(8px)',hidden:true,ancestor:'visibility:hidden'},
    {name:'display none ancestor suppresses backdrop',style:'backdrop-filter:blur(8px)',hidden:true,ancestor:'display:none'},
    {name:'hiding after measurement suppresses backdrop',style:'backdrop-filter:blur(8px)',hidden:true,ancestor:'',dynamic:true},
    {name:'visible child does not reveal hidden backdrop',style:'visibility:hidden;backdrop-filter:blur(8px)',childVisible:true,red:true,noBlur:true},
    {name:'ordinary filter still affects visible child',style:'visibility:hidden;filter:blur(8px)',childVisible:true,noBlur:true},
];
function fixtureHtml(f){
    const ancestor=f.ancestor!==undefined;
    return '<!doctype html><style>body{margin:0;background:lime;height:800px}'+
        '.underlay{position:absolute;left:150px;top:0;width:150px;height:200px;background:blue}'+
        '.panel{position:absolute;left:0;top:0;width:300px;height:200px;'+f.style+'}'+
        '.child{position:absolute;left:100px;top:50px;width:100px;height:100px;background:red;'+(f.childVisible?'visibility:visible':'')+'}'+
        '</style><div class=underlay></div>'+(ancestor?'<div id=wrapper style="'+f.ancestor+'">':'')+
        '<div class=panel><div class=child></div></div>'+(ancestor?'</div>':'')+
        (f.dynamic?'<script>document.getElementById("wrapper").getBoundingClientRect();document.getElementById("wrapper").style.display="none";</script>':'');
}
module.exports={fixtures,fixtureHtml};
