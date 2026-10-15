const blob = new Blob([new Uint8Array([0, 255, 64])], {type:'application/octet-stream'});
const reader = new FileReader();
let events = [];
console.log(reader.readyState === FileReader.EMPTY, reader.result === null, reader.error === null);
reader.addEventListener('loadstart', event => {
    events.push(event.type);
    console.log(reader.readyState === reader.LOADING, event.target === reader,
        event.loaded === 0, event.total === 3);
});
reader.onprogress = event => {
    events.push(event.type);
    console.log(event.loaded === 3, event.total === 3);
};
reader.onload = event => {
    events.push(event.type);
    console.log(reader.readyState === reader.DONE, reader.result === 'data:application/octet-stream;base64,AP9A',
        reader.error === null, event.target === reader);
};
reader.onloadend = event => {
    events.push(event.type);
    console.log(events.join(',') === 'loadstart,progress,load,loadend');
    const cancelled = new FileReader();
    let cancelledEvents = [];
    cancelled.onload = () => cancelledEvents.push('unexpected-load');
    cancelled.onabort = () => cancelledEvents.push('abort');
    cancelled.onloadend = () => cancelledEvents.push('loadend');
    cancelled.readAsDataURL(blob);
    cancelled.abort();
    console.log(cancelled.readyState === cancelled.DONE, cancelled.result === null, cancelled.error === null);
    setTimeout(() => console.log(cancelledEvents.join(',') === 'abort,loadend'), 0);
};
reader.readAsDataURL(blob);
let busy = false;
try { reader.readAsDataURL(blob); } catch (error) { busy = error.name === 'InvalidStateError'; }
console.log(reader.readyState === reader.LOADING, reader.result === null, events.length === 0, busy);
const firstUrl = URL.createObjectURL(blob);
const secondUrl = URL.createObjectURL(blob);
console.log(firstUrl.startsWith('blob:'), firstUrl !== secondUrl);
URL.revokeObjectURL(firstUrl);
URL.revokeObjectURL(firstUrl);
