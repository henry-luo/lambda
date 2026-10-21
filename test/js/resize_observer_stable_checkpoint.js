var box = document.getElementById('box');
var child = document.getElementById('child');
var calls = 0, mutationCalls = 0, lastHeight = -1, temporary = -1, restored = -1;
var mutations = new MutationObserver(function () { mutationCalls++; });
mutations.observe(box, {childList:true, subtree:true, characterData:true});
var observer = new ResizeObserver(function (entries) {
    calls++;
    lastHeight = entries[entries.length - 1].contentRect.height;
    if (calls === 1) {
        child.style.display = 'none';
        temporary = box.offsetHeight;
        child.style.display = 'block';
        restored = box.offsetHeight;
    }
});
observer.observe(box);
console.log(calls === 0);
setTimeout(function () {
    console.log(calls === 1 && lastHeight === 20 && temporary === 0 && restored === 20);
    child.style.height = '30px';
    var ignored = box.offsetHeight;
    setTimeout(function () {
        console.log(calls === 2 && lastHeight === 30);
        console.log(mutationCalls === 0);
        observer.disconnect();
        mutations.disconnect();
        console.log('RESIZE_OBSERVER_STABLE_CHECKPOINT_DONE');
    }, 50);
}, 50);
