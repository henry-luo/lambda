const clickReentrancyCases = [
    {name:'self', count:2, other:0},
    {name:'cycle', count:2, other:2},
    {name:'removed', count:2, other:0},
    {name:'exception', count:2, other:0},
    {name:'microtask', count:2, other:0},
    {name:'dispatch', count:2, other:0},
    {name:'clone', count:2, other:2},
];

function clickReentrancyHtml(name) {
    const handlers = {
        self: `a.addEventListener('click',()=>{if(++count<4)a.click()});a.click();a.click();`,
        cycle: `a.addEventListener('click',()=>{if(++count<4)b.click()});
            b.addEventListener('click',()=>{if(++other<4)a.click()});a.click();a.click();`,
        removed: `a.addEventListener('click',()=>{count++;a.remove();
            document.body.getBoundingClientRect();if(count<4)a.click()});
            a.click();document.body.appendChild(a);a.click();`,
        exception: `a.addEventListener('click',()=>{if(++count<4)a.click();
            throw new Error('expected click listener failure')});a.click();a.click();`,
        microtask: `a.addEventListener('click',()=>{count++;
            if(count===1)queueMicrotask(()=>a.click())});a.click();`,
        dispatch: `a.addEventListener('click',()=>{count++;
            if(count===1)a.dispatchEvent(new MouseEvent('click'));
            else if(count===2)a.click()});a.click();`,
        clone: `a.addEventListener('click',()=>{count++;const clone=a.cloneNode(true);
            let nested=0;clone.addEventListener('click',()=>{other++;if(++nested<4)clone.click()});
            clone.click()});a.click();a.click();`,
    };
    return `<!doctype html><style>body{margin:0}#band{width:80px;height:80px;background:green}</style>
        <div id="band"></div><button id="a">A</button><button id="b">B</button><script>
        const a=document.getElementById('a'),b=document.getElementById('b');
        let count=0,other=0;
        ${handlers[name]}
        window.addEventListener('load',()=>console.log('CLICK_REENTRY_RESULT',JSON.stringify({count,other})));
        </script>`;
}

module.exports = {clickReentrancyCases, clickReentrancyHtml};
