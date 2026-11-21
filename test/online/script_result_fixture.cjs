function scriptResultFixture(source,errorTag,resultTag,head='',strict=false){
 return `<!doctype html><head>${head}<style>html,body{margin:0}#result{width:30px;height:30px;background:red}</style></head><body><div id=result></div><script>
let okay=true;function check(value){okay=okay&&!!value}
(async()=>{${strict?"'use strict';":''}try{${source}}catch(error){okay=false;console.log(${JSON.stringify(errorTag)},error&&error.name,error&&error.message)}
console.log(${JSON.stringify(resultTag)},okay);if(okay)document.getElementById('result').style.background='lime'})();</script>`;
}
module.exports={scriptResultFixture};
