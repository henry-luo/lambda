var target = document.getElementById('target');

target.classList.add('active');
console.log('add:' + target.className + ':' + target.getAttribute('class'));

target.classList.remove('old');
console.log('remove:' + target.className + ':' + target.getAttribute('class'));

target.classList.toggle('active');
console.log('toggle:' + target.className + ':' + target.getAttribute('class'));

// classList assignment forwards to its live token list instead of replacing it.
var tokens = target.classList;
target.classList = 'ready active';
console.log('assign:' + target.className + ':' + target.getAttribute('class'));
console.log('live:' + (tokens === target.classList) + ':' + tokens.contains('ready'));
target['classList'] = '';
console.log('empty:' + tokens.length + ':' + target.getAttribute('class'));

document.documentElement.className = 'reftest-wait';
document.addEventListener('TestRendered', function () {
    document.documentElement.classList = 'tweak';
});
document.documentElement.dispatchEvent(new Event('TestRendered', {bubbles: true}));
console.log('snapshot:' + document.documentElement.className);
