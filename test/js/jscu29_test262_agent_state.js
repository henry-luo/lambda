for (let i = 0; i < 17; i++) {
    $262.agent.start(
        '$262.agent.receiveBroadcast(function(value) {' +
        'globalThis.__jscu29_received = (globalThis.__jscu29_received || 0) + value;' +
        '});'
    );
}
$262.agent.broadcast(1);

for (let i = 0; i < 65; i++) {
    $262.agent.report(String(i));
}
let reports = 0;
while ($262.agent.getReport() !== null) {
    reports++;
}
console.log(globalThis.__jscu29_received, reports);

for (let i = 0; i < 17; i++) {
    $262.agent.start(
        'var buffer = new SharedArrayBuffer(4);' +
        'var view = new Int32Array(buffer);' +
        'Atomics.waitAsync(view, 0, 0, 1);' +
        '$262.agent.report("W timed-out");'
    );
}
let atomics_reports = 0;
while ($262.agent.getReport() !== null) {
    atomics_reports++;
}
console.log(atomics_reports);

var shared_view = new Int32Array(new SharedArrayBuffer(4));
for (let i = 0; i < 129; i++) {
    $262.agent.start(
        'Atomics.waitAsync(shared_view, 0, 0);' +
        '$262.agent.report("W ok");'
    );
}
console.log(Atomics.notify(shared_view, 0));
let notified_reports = 0;
while ($262.agent.getReport() !== null) {
    notified_reports++;
}
console.log(notified_reports);
