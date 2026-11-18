const fixtures=[
    {name:'row centered',css:'align-items:center'},
    {name:'row reverse',css:'flex-direction:row-reverse;align-items:center'},
    {name:'column centered',css:'flex-direction:column;height:160px;align-items:center'},
    {name:'column reverse',css:'flex-direction:column-reverse;height:160px;align-items:center'},
    {name:'wrap reverse',css:'width:100px;height:160px;flex-wrap:wrap-reverse;align-content:center'},
    {name:'RTL row',css:'direction:rtl;align-items:center'},
    {name:'vertical writing mode',css:'writing-mode:vertical-rl;height:160px;align-items:center'},
    {name:'boxless wrapper decorations',css:'align-items:center',wrapper:'padding:17px;border:9px solid black'},
];
function fixtureHtml(fixture){
    const items=['red','green','blue'].map(color=>`<i style="background:${color}"></i>`);
    const wrapped=items[0]+`<div class="contents" style="${fixture.wrapper||''}">${items[1]}<span class="contents">${items[2]}</span></div>`;
    return `<!doctype html><style>body{margin:0;background:white}
.container{position:absolute;left:10px;top:10px;display:flex;width:200px;height:80px;padding:20px 10px;border:5px solid gray;gap:7px;${fixture.css}}
.copy{top:310px}.contents{display:contents}i{display:block;flex:none;width:40px;height:20px}
</style><div class="container">${items.join('')}</div><div class="container copy">${wrapped}</div>`;
}
function differingPixels(image){
    let different=0;
    for(let y=0;y<280;y++)for(let x=0;x<300;x++){
        const a=(y*image.width+x)*4,b=((y+300)*image.width+x)*4;
        if(image.data[a]!==image.data[b]||image.data[a+1]!==image.data[b+1]||image.data[a+2]!==image.data[b+2])different++;
    }
    return different;
}
module.exports={fixtures,fixtureHtml,differingPixels};
