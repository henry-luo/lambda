const {pathToFileURL}=require('node:url');
const fontFile='/System/Library/Fonts/SFNS.ttf';
const fixtures=[
    {name:'omitted weight retains the requested variable instance',declaration:'',weight:700},
    {name:'auto weight retains the requested variable instance',declaration:'font-weight:auto;',weight:700},
    {name:'invalid weight leaves the automatic default',declaration:'font-weight:400 500 700;',weight:700},
    {name:'explicit single weight remains clamped',declaration:'font-weight:400;',weight:400},
];
function fixtureHtml(f,reference) {
    const face=reference?'font-weight:'+f.weight+';':f.declaration;
    return '<!doctype html><style>@font-face{font-family:Probe;src:url("'+pathToFileURL(fontFile).href+'");'+face+'}'+
        'html,body{margin:0;background:white}div{font:700 90px Probe;color:black;font-synthesis-weight:none}'+
        '</style><div>Wide MMMM</div>';
}
module.exports={fixtures,fixtureHtml,fontFile};
