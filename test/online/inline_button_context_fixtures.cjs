const fixtures = [
    {name:'inline button isolates its indented label',display:'inline',extra:''},
    {name:'inline-block button control',display:'inline-block',extra:''},
    {name:'inline-flex button control',display:'inline-flex',extra:''},
    {name:'translated inline button retains intrinsic width',display:'inline',extra:'transform:translate(40px,50px)'},
];
function fixtureHtml(f,pretty) {
    return '<!doctype html><style>html,body{margin:0;background:white}'+
        '#host{display:inline-block;font:16px/16px Arial;padding:5.6px 3.2px 4.8px;'+
        'border-right:1px solid;box-sizing:border-box;'+f.extra+'}'+
        'span,input,button{display:inline-block;box-sizing:border-box}'+
        'span{width:38.5px;height:28px;margin-right:8px;vertical-align:middle}'+
        'input{font:inherit;line-height:26px;width:224px;margin:2px 8px 2px 0;'+
        'padding:6.4px 8px 4.8px;border:0;vertical-align:middle}'+
        'button{display:'+f.display+';font:inherit;line-height:26px;white-space:nowrap;'+
        'padding:6.4px 12px 5.6px;border:1px solid;margin:0 3.2px 7px 0;'+
        'vertical-align:baseline;background:green;color:white}</style><div id="host"><div>'+
        '<span></span>\n<input>\n<button>'+(pretty?'\n GO\n ':'GO')+'</button></div></div>';
}
module.exports={fixtures,fixtureHtml};
