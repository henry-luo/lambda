(0, eval)(require('fs').readFileSync('test/js/floating-ui.min.js', 'utf8'));

var anchor = document.getElementById('anchor');
var floating = document.getElementById('floating');

FloatingUIDOM.computePosition(anchor, floating, {placement: 'bottom'}).then(function (position) {
  console.log('floating:initial: ' + Math.round(position.x) + ' ' + Math.round(position.y) + ' ' + position.placement);
  anchor.style.width = '120px';
  return new Promise(function (resolve) { setTimeout(resolve, 10); });
}).then(function () {
  return FloatingUIDOM.computePosition(anchor, floating, {placement: 'bottom'});
}).then(function (position) {
  console.log('floating:resized: ' + Math.round(position.x) + ' ' + Math.round(position.y));
});
