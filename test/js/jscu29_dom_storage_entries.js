localStorage.clear();
for (let i = 0; i < 129; i++) {
    localStorage.setItem('key-' + i, 'value-' + i);
}
console.log(localStorage.length);
console.log(localStorage.getItem('key-128'));
localStorage.removeItem('key-0');
console.log(localStorage.length);
localStorage.clear();
console.log(localStorage.length);
