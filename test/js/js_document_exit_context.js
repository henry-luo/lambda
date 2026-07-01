"use strict";
function doIntern(t){ var o = {}; o['k'+t] = 1; var m = new Map(); m.set('key'+t, 2); return o['k'+t] + m.get('key'+t); }
console.log('A plain-call=' + doIntern('A'));
var d = document.createElement('div'); document.body.appendChild(d);
d.addEventListener('keydown', function(){ globalThis.__B = doIntern('B'); });
d.dispatchEvent(new KeyboardEvent('keydown', { key:'x', bubbles:true }));
console.log('B dispatched=' + globalThis.__B);
