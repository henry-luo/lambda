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
text.textContent = null;
console.log(text.data);
