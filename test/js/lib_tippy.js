(0, eval)(require('fs').readFileSync('test/js/popper_src_min.js', 'utf8'));
(0, eval)(require('fs').readFileSync('test/js/tippy.min.js', 'utf8'));

var tip = tippy(document.getElementById('tip-anchor'), {
  content: 'positioned tip',
  trigger: 'manual',
  duration: 0,
  animation: false
});
tip.show();
setTimeout(function () {
  console.log('tippy:shown: ' + tip.state.isVisible + ' ' + tip.state.isMounted);
  console.log('tippy:content: ' + tip.popper.querySelector('.tippy-content').textContent);
  tip.hide();
  setTimeout(function () {
    console.log('tippy:hidden: ' + tip.state.isVisible + ' ' + tip.state.isMounted);
  }, 10);
}, 10);
