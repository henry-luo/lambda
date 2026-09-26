// Ledger §15.1 / JO12: the document event-loop drain is where a SIGSEGV
// guard once hid "heap corruption in timer callbacks". Each callback here
// builds, attributes and removes DOM nodes and schedules more work, so a
// forced-GC run exercises the document's callback roots. The summary prints
// when the last callback finishes, not on a clock, and sorts what it reports:
// timers of different delays interleave by wall-clock granularity.
const list = document.getElementById("list");
let pending = 0;

function done() {
    if (--pending > 0) return;
    const items = list.querySelectorAll("li");
    console.log(items.length);
    console.log(Array.from(items).map((li) => li.getAttribute("data-t")).sort().join(","));
    console.log(list.getAttribute("data-last"));
}

for (let t = 0; t < 12; t++) {
    pending++;
    setTimeout(() => {
        const li = document.createElement("li");
        li.setAttribute("data-t", "t" + t);
        li.textContent = "item " + t + " " + "x".repeat(t);
        list.appendChild(li);
        if (t % 4 === 3) {
            pending++;
            setTimeout(() => {
                const doomed = document.createElement("li");
                doomed.setAttribute("data-t", "doomed" + t);
                list.appendChild(doomed);
                list.removeChild(doomed);
                list.setAttribute("data-last", "nested" + t);
                done();
            }, 0);
        }
        done();
    }, t % 3);
}
