const fixtures = [
    {name:'numeric shorthand survives virtual longhand winners', css:'#probe{font:700 38px/1.1 Arial,sans-serif}', weight:700, family:'Arial', style:'normal'},
    {name:'keyword shorthand survives virtual longhand winners', css:'#probe{font:bold 38px/1.1 Arial,sans-serif}', weight:700, family:'Arial', style:'normal'},
    {name:'later explicit components still win', css:'#probe{font:italic 700 38px/1.1 Arial,sans-serif;font-weight:400;font-family:monospace;font-style:normal}', weight:400, family:'monospace', style:'normal'},
    {name:'stronger shorthand replaces weaker components', css:'h2{font-weight:400;font-family:serif}#probe{font:italic 700 38px/1.1 Arial,sans-serif}', weight:700, family:'Arial', style:'italic'},
    {name:'substituted shorthand uses the shorthand grammar', css:'#probe{--face:italic 700 38px/1.1 Arial,sans-serif;font:var(--face)}', weight:700, family:'Arial', style:'italic'},
    {name:'important shorthand overrides normal components', css:'#probe{font:italic 700 38px/1.1 Arial,sans-serif!important;font-weight:400;font-family:monospace}', weight:700, family:'Arial', style:'italic'},
    {name:'zero-specificity shorthand wins over inherited components', css:'body{font-weight:400;font-family:serif;font-style:normal}:where(#probe){font:italic 700 38px/1.1 Arial,sans-serif}', weight:700, family:'Arial', style:'italic', flush:true},
    {name:'children inherit zero-specificity shorthand components', css:'body{font-weight:400;font-family:serif;font-style:normal}:where(#probe){font:italic 700 38px/1.1 Arial,sans-serif}', weight:700, family:'Arial', style:'italic', child:true, flush:true},
    {name:'children inherit the resolved shorthand font', css:'#probe{font:italic 700 38px/1.1 Arial,sans-serif}', weight:700, family:'Arial', style:'italic', child:true},
    {name:'children inherit computed font without substituting their own variables', css:'#probe{--weight:700;font:italic var(--weight) 38px/1.1 Arial,sans-serif}#child{--weight:400}', weight:700, family:'Arial', style:'italic', child:true, flush:true},
    {name:'children inherit the computed small-caps variant', css:':where(#probe){font:small-caps 700 38px/1.1 Arial,sans-serif}', weight:700, family:'Arial', style:'normal', variant:'small-caps', child:true, flush:true},
    {name:'native heading weight remains a local UA declaration', css:'body{font-family:Arial,sans-serif}#probe{font-size:38px}', weight:700, family:'Arial', style:'normal', flush:true},
    {name:'native emphasis style remains a local UA declaration', css:'body{font-family:Arial,sans-serif}#probe{font-size:38px}', tag:'em', weight:400, family:'Arial', style:'italic', flush:true},
    {name:'explicit all unset inherits instead of keeping UA weight', css:'body{font-family:Arial,sans-serif}#probe{all:unset;font-size:38px}', weight:400, family:'Arial', style:'normal', flush:true},
    {name:'pseudo font preserves shorthand components', css:'#probe::before{content:"Label";font:italic 700 38px/1.1 Arial,sans-serif}', weight:700, family:'Arial', style:'italic', pseudo:true}
];
function fixtureHtml(f) {
    return `<!doctype html><style>${f.css}</style><${f.tag||'h2'} id="probe"><span id="child">Heading</span></${f.tag||'h2'}><script>
    ${f.flush?"document.getElementById('probe').getBoundingClientRect();":''}
    const computed=getComputedStyle(document.getElementById('${f.child?'child':'probe'}')${f.pseudo?',"::before"':''});
    console.log('FONT_SHORTHAND_WINNER',JSON.stringify({weight:computed.fontWeight==='bold'?700:computed.fontWeight==='normal'?400:Number(computed.fontWeight),family:computed.fontFamily,style:computed.fontStyle,variant:computed.fontVariant,size:computed.fontSize}));
    </script>`;
}
module.exports = {fixtures,fixtureHtml};
