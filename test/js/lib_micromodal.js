(0, eval)(require('fs').readFileSync('test/js/micromodal.min.js', 'utf8'));

var trigger = document.getElementById('modal-trigger');
trigger.focus();
MicroModal.init({ awaitOpenAnimation: false, awaitCloseAnimation: false });
MicroModal.show('modal', { awaitOpenAnimation: false, awaitCloseAnimation: false });
var modal = document.getElementById('modal');

console.log('micromodal:open:', modal.classList.contains('is-open'), modal.getAttribute('aria-hidden'));
console.log('micromodal:focus:', document.activeElement.id);
MicroModal.close('modal');
console.log('micromodal:close:', modal.classList.contains('is-open'), modal.getAttribute('aria-hidden'));
console.log('micromodal:return:', document.activeElement.id);
