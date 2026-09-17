console.log(document.cookie === '');
document.cookie = 'theme=dark; path=/';
console.log(document.cookie.match(/theme=(light|dark)/)[1]);
