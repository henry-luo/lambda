(0, eval)(require('fs').readFileSync('test/js/alpine.min.js', 'utf8'));

var alpineCleanupCount = 0;
Alpine.directive('cleanup-probe', function (element, directive, utilities) {
  utilities.cleanup(function () { alpineCleanupCount++; });
});

setTimeout(function () {
  document.getElementById('alpine-increment').click();
  setTimeout(function () {
    console.log('alpine:boot:', document.getElementById('alpine-count').textContent,
      document.getElementById('alpine-visible').style.display !== 'none');

    document.getElementById('alpine-host').innerHTML =
      '<div id="alpine-dynamic" x-data="{ count: 4 }" x-cleanup-probe>' +
      '<button id="alpine-dynamic-increment" x-on:click="count++">add</button>' +
      '<span id="alpine-dynamic-count" x-text="count"></span></div>';

    setTimeout(function () {
      document.getElementById('alpine-dynamic-increment').click();
      setTimeout(function () {
        console.log('alpine:dynamic:', document.getElementById('alpine-dynamic-count').textContent);
        document.getElementById('alpine-dynamic').remove();
        setTimeout(function () {
          console.log('alpine:cleanup:', alpineCleanupCount);
        }, 10);
      }, 10);
    }, 0);
  }, 10);
}, 0);
