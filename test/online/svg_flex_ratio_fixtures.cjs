const svgFlexRatioCases = [
    {name:'pixel-height', height:'16px', width:14, usedHeight:16},
    {name:'font-height', height:'1em', width:14, usedHeight:16},
    {name:'border-box', height:'16px', extra:'box-sizing:border-box;padding:2px;border:1px solid black', width:14.75, usedHeight:16},
    {name:'specified-ratio', height:'16px', extra:'aspect-ratio:2', width:32, usedHeight:16},
    {name:'max-width', height:'16px', extra:'max-width:10px', width:10, usedHeight:16},
    {name:'definite-width', height:'16px', extra:'width:20px', width:20, usedHeight:16},
    {name:'zero-height', height:'0px', width:0, usedHeight:0},
    {name:'dynamic-height', height:'16px', dynamic:true, width:28, usedHeight:32},
    {name:'column-stretch', height:'16px', column:true, width:200, usedHeight:16},
];

function svgFlexRatioHtml(fixture) {
    return `<!doctype html><style>body{margin:0}
    .row{display:flex;width:200px;gap:8px;font-size:16px;${fixture.column?'flex-direction:column;':'align-items:flex-start;'}}
    svg{height:${fixture.height};width:auto;flex-shrink:0;display:inline-block;${fixture.extra||''}}
    .marker{width:100px;height:20px;background:blue;flex-shrink:0}
    </style><div class=row><svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 448 512">
    <path fill="green" d="M0 0H448V512H0Z"/></svg><div class=marker></div></div><script>
    var svg=document.querySelector('svg');${fixture.dynamic?"svg.getBoundingClientRect();svg.style.height='32px';":''}
    var box=svg.getBoundingClientRect();
    console.log('SVG_FLEX_RATIO_RESULT',JSON.stringify({width:box.width,height:box.height}));</script>`;
}

module.exports = {svgFlexRatioCases, svgFlexRatioHtml};
