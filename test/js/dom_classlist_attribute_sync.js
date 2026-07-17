var target = document.getElementById('target');

target.classList.add('active');
console.log('add:' + target.className + ':' + target.getAttribute('class'));

target.classList.remove('old');
console.log('remove:' + target.className + ':' + target.getAttribute('class'));

target.classList.toggle('active');
console.log('toggle:' + target.className + ':' + target.getAttribute('class'));
