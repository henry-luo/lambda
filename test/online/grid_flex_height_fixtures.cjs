function fixtureHtml(align) {
    return `<!doctype html><style>
    *{box-sizing:border-box}body{margin:0;font:16px/1.4 Arial}
    #grid{display:grid;width:400px;height:600px;grid-template-columns:1fr 1fr;align-items:${align}}
    #item,#text{display:flex;flex-direction:column;row-gap:32px}
    h1,p{margin:0}h1{font-size:64px;line-height:64px;width:150%}
    #actions{height:160px;flex-shrink:0;background:green}
    </style><div id="grid"><div id="item"><div id="text"><h1>The Agentic Content Framework</h1>
    <p>Move from intention to implementation faster with an open-source framework to build anything.</p></div>
    <div id="actions">Actions</div></div></div><script>
    const item=document.getElementById('item').getBoundingClientRect();
    const actions=document.getElementById('actions').getBoundingClientRect();
    console.log('GRID_FLEX_HEIGHT',JSON.stringify({height:item.height,contentBottom:actions.bottom-item.top}));
    </script>`;
}
module.exports = {fixtureHtml};
