var host = document.getElementById('host');
var text = document.getElementById('text-node').firstChild;

host.textContent = 42;
console.log(host.textContent);
host.textContent = true;
console.log(host.textContent);
host.textContent = null;
console.log('empty:' + host.textContent.length);

text.data = 17;
console.log(text.data);
text.nodeValue = false;
console.log(text.textContent);
// The nullable Node accessors clear the text. CharacterData.data stringifies null.
text.textContent = null;
console.log('empty:' + text.data.length);
text.data = null;
console.log(JSON.stringify(text.data));
text.nodeValue = null;
console.log('empty:' + text.data.length);
