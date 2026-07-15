var box = document.getElementById('box');
box.addEventListener('transitionend', function (event) {
  console.log('event:' + event.type);
  console.log('property:' + event.propertyName);
  console.log('elapsed:' + (event.elapsedTime > 0));
});
box.style.width = '20px';
box.getBoundingClientRect();
setTimeout(function () {
  box.style.opacity = '1';
  box.getBoundingClientRect();
}, 0);
