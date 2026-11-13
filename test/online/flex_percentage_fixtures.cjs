const fixtures = [
    {name: 'cyclic preferred width', style: 'width:100%'},
    {name: 'cyclic half width', style: 'width:50%'},
    {name: 'cyclic math width', style: 'width:calc(100% - 0px)'},
    {name: 'cyclic variable width', style: '--size:100%;width:var(--size)'},
    {name: 'cyclic logical width', style: 'inline-size:100%'},
    {name: 'automatic width control', style: 'width:auto'},
    {name: 'explicit zero minimum control', style: 'width:100%', row: 'min-width:0'},
    {name: 'nowrap minimum control', style: 'width:100%', text: 'white-space:nowrap', overflow: true},
    {name: 'fixed preferred width control', style: 'width:300px', overflow: true},
    {name: 'fixed computed width control', style: 'width:calc(300px + 0px)', overflow: true},
    {name: 'fixed substituted width control', style: '--size:300px;width:var(--size)', overflow: true},
    {name: 'width mutation after a layout read', style: 'width:300px', mutation: 'width:100%'},
    {name: 'variable mutation after a layout read', style: '--size:300px;width:var(--size)', mutation: '--size:100%;width:var(--size)'},
];

function fixtureHtml(fixture) {
    return `<!doctype html><meta charset="utf-8"><style>
body{margin:0;font:16px Arial}*{box-sizing:border-box}
#outer{display:flex;width:270px;border:1px solid black;padding:16px}
#row{display:flex;align-items:stretch;${fixture.row || ''}}
#icon{flex:none;width:64px;height:64px;margin-right:16px;background:blue}
#content{flex:auto;${fixture.style};background:red}
#text{margin:0;line-height:20px;${fixture.text || ''}}
</style><div id="outer"><div id="row"><div id="icon"></div><div id="content">
<p id="text">Design your app using the Azure Architecture Center</p>
</div></div></div>${fixture.mutation ? `<script>
const content = document.getElementById('content');
content.getBoundingClientRect();
content.setAttribute('style', '${fixture.mutation}');
</script>` : ''}`;
}

module.exports = {fixtures, fixtureHtml};
