let received = 'before';
window.addEventListener('message', (event) => {
    received = event.data;
});
window.postMessage('after', '*');
console.log(received);
setTimeout(() => console.log(received), 0);
