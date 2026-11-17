function card({icon=10,percent=100,ratio=1,extra='',css='',heightAttribute='100%',svgFlex=false}={}) {
    return `<!doctype html><style>*{box-sizing:border-box}body{margin:0}.grid{width:400px;display:grid}.article{display:flex;flex-direction:column}.card{padding:20px;background:red;overflow:hidden}.heading{height:40px}.link{display:flex;align-items:flex-start}.icon{width:${icon}px;flex:none;${svgFlex ? 'display:flex' : ''}}.label{height:20px;width:80px}.media{margin-top:20px;width:100%;aspect-ratio:2;background:blue}svg{display:block;${css}}</style><div class="grid"><div><div class="article"><div class="card"><div class="heading"></div><div class="link"><div class="icon"><svg xmlns="http://www.w3.org/2000/svg" width="${percent}%" ${heightAttribute === null ? '' : `height="${heightAttribute}"`} viewBox="0 0 ${ratio} 1" style="${extra}"><path fill="black" d="M0 0h${ratio}v1H0z"/></svg></div><div class="label"></div></div><div class="media"></div></div></div></div></div>`;
}
const fixtures=[
    ['auto SVG height retains viewBox ratio',{icon:80,ratio:2,heightAttribute:'auto'},320],
    ['invalid SVG height does not invent a natural axis',{icon:80,ratio:2,heightAttribute:'invalid'},320],
    ['flex SVG auto height retains its percentage width',{icon:80,ratio:2,heightAttribute:'auto',svgFlex:true},320],
    ['flex SVG omitted height control',{icon:80,ratio:2,heightAttribute:null,svgFlex:true},320],
    ['numeric SVG height remains a natural axis',{icon:80,ratio:2,heightAttribute:'50'},330],
    ['percentage icon retains its definite containing width',{},300],
    ['partial percentage and nonsquare ratio',{icon:80,percent:50,ratio:2},300],
    ['oversized percentage contributes its actual height',{icon:40,percent:200},360],
    ['zero containing width stays zero',{icon:0},300],
    ['minimum width constrains the ratio transfer',{icon:10,css:'min-width:40px'},320],
    ['maximum width constrains the ratio transfer',{icon:80,css:'max-width:12px'},300],
    ['CSS dimensions override percentage attributes',{extra:'width:16px;height:auto'},300],
];
module.exports={card,fixtures};
