function card({icon=10,percent=100,ratio=1,extra='',css=''}={}) {
    return `<!doctype html><style>*{box-sizing:border-box}body{margin:0}.grid{width:400px;display:grid}.article{display:flex;flex-direction:column}.card{padding:20px;background:red;overflow:hidden}.heading{height:40px}.link{display:flex;align-items:flex-start}.icon{width:${icon}px;flex:none}.label{height:20px;width:80px}.media{margin-top:20px;width:100%;aspect-ratio:2;background:blue}svg{display:block;${css}}</style><div class="grid"><div><div class="article"><div class="card"><div class="heading"></div><div class="link"><div class="icon"><svg xmlns="http://www.w3.org/2000/svg" width="${percent}%" height="100%" viewBox="0 0 ${ratio} 1" style="${extra}"><path fill="black" d="M0 0h${ratio}v1H0z"/></svg></div><div class="label"></div></div><div class="media"></div></div></div></div></div>`;
}
const fixtures=[
    ['percentage icon retains its definite containing width',{},300],
    ['partial percentage and nonsquare ratio',{icon:80,percent:50,ratio:2},300],
    ['oversized percentage contributes its actual height',{icon:40,percent:200},360],
    ['zero containing width stays zero',{icon:0},300],
    ['minimum width constrains the ratio transfer',{icon:10,css:'min-width:40px'},320],
    ['maximum width constrains the ratio transfer',{icon:80,css:'max-width:12px'},300],
    ['CSS dimensions override percentage attributes',{extra:'width:16px;height:auto'},300],
];
module.exports={card,fixtures};
