var fs = require('fs');
globalThis.__JQUERY_LIBRARY_ONLY__ = true;
(0, eval)(fs.readFileSync('test/js/dom_jquery_lib.js', 'utf8'));
delete globalThis.__JQUERY_LIBRARY_ONLY__;

var completed = false;
$('#box').fadeIn(20, function () {
  completed = true;
  console.log('callback:' + completed);
  console.log('display:' + ($('#box').css('display') !== 'none'));
  console.log('opacity:' + Math.round(parseFloat($('#box').css('opacity'))));
});
