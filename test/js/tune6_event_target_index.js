let targets = [];
let total = 0;

function indexedListener(event) {
    total += event.target.tune6Index;
}

function indexedHandler(event) {
    total += 1000 + event.target.tune6Index;
}

for (let i = 0; i < 256; i++) {
    let target = new EventTarget();
    target.tune6Index = i;
    target.addEventListener("tune6", indexedListener);
    target.ontune6 = indexedHandler;
    targets.push(target);
}

for (let i = targets.length - 1; i >= 0; i--) {
    targets[i].dispatchEvent(new Event("tune6"));
}

console.log(targets.length, total);
