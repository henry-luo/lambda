const shadowPositionedCases = [
    {name: 'fixed-open', position: 'fixed', mode: 'open'},
    {name: 'absolute-open', position: 'absolute', mode: 'open'},
    {name: 'fixed-closed', position: 'fixed', mode: 'closed'},
    {name: 'fixed-nested', position: 'fixed', mode: 'open', nested: true},
    {name: 'fixed-transformed-host', position: 'fixed', mode: 'open', transform: true},
    {name: 'fixed-light-dom', position: 'fixed', light: true},
    {name: 'fixed-top-layer', position: 'fixed', mode: 'open', topLayer: true},
];

function shadowPositionedHtml(fixture) {
    return `<!doctype html><style>
        html{height:100%}body{margin:0}
        #host{display:block;width:200px;height:120px;margin:24px 40px;
            ${fixture.transform ? 'transform:translate(0);' : ''}}
    </style><section id=host></section><script>
        var host=document.getElementById('host');
        var root=${fixture.light ? 'host' : `host.attachShadow({mode:'${fixture.mode}'})`};
        ${fixture.nested ? "var inner=document.createElement('section');root.appendChild(inner);root=inner.attachShadow({mode:'open'});" : ''}
        var panel=document.createElement('${fixture.topLayer ? 'dialog' : 'div'}');
        panel.style='position:${fixture.position};left:0;right:0;bottom:0;top:auto;width:auto;height:40px;max-width:none;max-height:none;border:0;padding:0;margin:0;background:lime';
        root.appendChild(panel);
        ${fixture.topLayer ? 'panel.showModal();' : ''}
        var rect=panel.getBoundingClientRect();
        console.log('SHADOW_POSITIONED',JSON.stringify({x:rect.x,y:rect.y,width:rect.width,height:rect.height}));
    </script>`;
}

function shadowPositionedExpected(fixture) {
    return fixture.transform ? {x:40,y:104,width:200,height:40}
        : {x:0,y:760,width:1200,height:40};
}

module.exports = {shadowPositionedCases, shadowPositionedHtml, shadowPositionedExpected};
