// F17: a JS Event wrapper and its dispatch pipeline share one native record.
const target = new EventTarget();
const event = new MouseEvent("record", {bubbles: true, cancelable: true});
event.expando = "before";

target.addEventListener("record", function(received) {
    console.log([
        received === event,
        received instanceof Event,
        received instanceof MouseEvent,
        received.type,
        received.bubbles,
        received.cancelable,
        received.defaultPrevented,
        received.isTrusted,
        received.expando
    ].join("|"));
    received.expando = "after";
    received.preventDefault();
});

console.log(target.dispatchEvent(event), event.defaultPrevented,
            event.composedPath().length, event.expando);
