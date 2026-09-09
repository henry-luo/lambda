let source = '';
for (let i = 0; i < 17; i++) {
    source += 'with ({ value: ' + i + ' }) {';
}
source += 'let read = function() { gc(); return value; }; console.log(read());';
for (let i = 0; i < 17; i++) {
    source += '}';
}

eval(source);
