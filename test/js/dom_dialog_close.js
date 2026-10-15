const dialog = document.createElement('dialog');
document.body.appendChild(dialog);
let closes = 0;
let eventShape = true;
dialog.addEventListener('close', event => {
    closes++;
    eventShape = eventShape && event.target === dialog && !event.bubbles && !event.cancelable;
});
console.log(dialog.returnValue === '');
dialog.close('ignored');
dialog.showModal();
const opened = dialog.hasAttribute('open') && dialog.matches(':modal');
dialog.close('done');
console.log(opened, !dialog.hasAttribute('open'), !dialog.matches(':modal'),
    dialog.returnValue === 'done', closes === 0);
dialog.close('ignored');
setTimeout(() => {
    console.log(closes === 1, eventShape, dialog.returnValue === 'done');
    dialog.showModal();
    dialog.close();
    console.log(dialog.returnValue === 'done');
    dialog.returnValue = 17;
    console.log(dialog.returnValue === '17');
    setTimeout(() => console.log(closes === 2, eventShape), 0);
}, 0);
