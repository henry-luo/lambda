(function () {
  var seed = 'ok';

  function outer() {
    function sibling(value) {
      return seed + value;
    }

    function selected() {
      return seed;
    }

    return (function () {
      return (function () {
        [1].filter(function (value) {
          return value;
        }).forEach(function () {
          [].map(sibling);
        });
        return [selected === sibling, selected()];
      })();
    })();
  }

  console.log('nested-reuse:', outer().join(' '));
})();
