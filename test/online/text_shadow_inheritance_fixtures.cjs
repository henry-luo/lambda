const outline='-2px 0 red,0 -2px red,2px 0 red,0 2px red,-5px 5px red';
const fixtures=[
    {name:'missing variable after layout',shadow:'var(--shadow)',
        script:"heading.style.setProperty('--shadow', '"+outline+"')"},
    {name:'remove inherited shadows',shadow:outline,
        script:"heading.style.textShadow='none'",absent:true},
    {name:'replace inherited shadow with computed font offsets',shadow:'2em 0 red',parentSize:10,childSize:40,
        script:"heading.style.fontSize='15px'",minX:[30,40]},
    {name:'child variable does not substitute inherited value',shadow:'var(--shadow)',variable:outline,
        childStyle:'--shadow:none',script:"heading.style.setProperty('--shadow', '"+outline+"')"},
    {name:'explicit inherit',shadow:outline,childStyle:'text-shadow:inherit'},
    {name:'explicit unset',shadow:outline,childStyle:'text-shadow:unset'},
    {name:'local none wins after parent mutation',shadow:'none',childStyle:'text-shadow:none',
        script:"heading.style.textShadow='"+outline+"'",absent:true},
    {name:'nested transformed children refresh',shadow:'var(--shadow)',
        nested:true,childStyle:'display:block;position:absolute;top:0;transform:rotate(-2deg);font-size:60px',
        script:"heading.style.setProperty('--shadow', '"+outline+"')"},
    {name:'initial suppresses inherited shadows',shadow:outline,childStyle:'text-shadow:initial',absent:true},
];
function fixtureHtml(fixture){
    const content=fixture.nested?'<span style="display:block">ONLINE</span>':fixture.minX?'M':'ONLINE';
    return `<!doctype html><style>body{margin:0;background:white}
#heading{position:relative;margin:0;color:white;font:${fixture.parentSize||80}px Arial;${fixture.variable?'--shadow:'+fixture.variable+';':''}text-shadow:${fixture.shadow}}
#child{${fixture.childSize?'font-size:'+fixture.childSize+'px;':''}${fixture.childStyle||''}}</style>
<div id="heading"><span id="child">${content}</span></div><script>
const heading=document.getElementById('heading');heading.getBoundingClientRect();
${fixture.script||''};heading.getBoundingClientRect();</script>`;
}
function shadowInk(image){
    let count=0,minX=Infinity;
    for(let y=0;y<180;y++)for(let x=0;x<600;x++){
        const i=(y*image.width+x)*4;
        if(image.data[i]>150&&image.data[i+1]<100&&image.data[i+2]<100){count++;minX=Math.min(minX,x);}
    }
    return {count,minX};
}
module.exports={fixtures,fixtureHtml,shadowInk};
