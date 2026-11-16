const fixtures=[
    {name:'rounded inset retains flex label glyphs',extra:''},
    {name:'rounded inset retains transformed label glyphs',extra:'transform:translate(25px,30px) scale(1.25);transform-origin:0 0;'},
];
function fixtureHtml(radius,extra) {
    return '<!doctype html><style>html,body{margin:0;background:white}'+
        '#button{position:absolute;left:80px;top:60px;width:180px;height:40px;'+
        'display:flex;align-items:center;justify-content:center;font:20px Arial;'+
        'background:black;color:white;'+extra+'clip-path:inset(0 -1% 0 0 round '+radius+'px)}'+
        '</style><div id="button"><span>MMMM</span></div>';
}
module.exports={fixtures,fixtureHtml};
