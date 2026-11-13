const fixtures = [
    {name: 'absolute image percentage math uses the completed parent', style: 'height:calc(100% + 40px)', height: 200, image: true},
    {name: 'absolute block percentage math uses the completed parent', style: 'height:calc(50% + 20px)', height: 100},
    {name: 'absolute percentage math retains owner units and variables', style: '--part:50%;font-size:20px;height:calc(var(--part) + 2em)', height: 120},
    {name: 'late percentage math retains the authored maximum', style: 'height:calc(100% + 40px);max-height:180px', height: 180},
    {name: 'absolute definite height remains independent', style: 'height:40px', height: 40},
    {name: 'absolute bare percentage retains late resolution', style: 'height:100%', height: 160},
];
function fixtureHtml(fixture) {
    const content = fixture.image
        ? '<img class="target" src="data:image/svg+xml,%3Csvg xmlns=\'http://www.w3.org/2000/svg\' width=\'80\' height=\'80\'%3E%3Cpath fill=\'red\' d=\'M0 0H80V80H0Z\'/%3E%3C/svg%3E">'
        : '<div class="target"></div>';
    return `<!doctype html><style>body{margin:0}.parent{position:relative;width:200px}.filler{height:160px}.target{display:block;position:absolute;top:0;left:0;width:80px;background:red;${fixture.style}}</style><div class="parent"><div class="filler"></div>${content}</div>`;
}
module.exports = {fixtures, fixtureHtml};
