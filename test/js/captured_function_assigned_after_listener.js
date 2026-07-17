function identity(value) { return value; }

function makeTransition(target) {
  var completion, unused = 1, transitionTarget = target, another = 2;
  return {
    mount: function () {
      identity(transitionTarget).addEventListener('transitionend', function (event) {
        console.log('listener:' + (identity(event.target) === transitionTarget) + ':' + typeof completion);
        if (event.target === transitionTarget && completion) completion();
      });
    },
    start: function (index, transitionTarget) {
      completion = transitionTarget;
    }
  };
}

var target = document.getElementById('target');
var transition = makeTransition(target);
transition.mount();
transition.start(1, function () { console.log('completed'); });
target.dispatchEvent(new Event('transitionend'));
