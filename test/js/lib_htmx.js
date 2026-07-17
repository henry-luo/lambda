(0, eval)(require('fs').readFileSync('test/js/htmx.min.js', 'utf8'));

var htmxAfterSwap = 0;
document.body.addEventListener('htmx:afterSwap', function () { htmxAfterSwap++; });
htmx.process(document.body);
document.getElementById('htmx-load').click();

setTimeout(function () {
  console.log('htmx:swap:', document.getElementById('htmx-target').textContent.trim());
  console.log('htmx:event:', htmxAfterSwap);
  console.log('htmx:history:', history.length > 1, location.pathname.indexOf('htmx_payload.html') >= 0);
}, 80);
