async function checkAgentState() {
    let buffer = new SharedArrayBuffer(4), received = new Int32Array(buffer);
    for (let i = 0; i < 17; i++) {
        $262.agent.start(
            '$262.agent.receiveBroadcast(function(buffer) {' +
            'Atomics.add(new Int32Array(buffer), 0, 1);' +
            '$262.agent.report("received");$262.agent.leaving();});'
        );
    }
    $262.agent.broadcast(buffer);
    let messages = 0;
    while (messages < 17) {
        if ($262.agent.getReport() !== null) messages++;
    }
    for (let i = 0; i < 65; i++) $262.agent.report(String(i));
    let reports = 0;
    while ($262.agent.getReport() !== null) reports++;
    console.log(received[0], reports);

    for (let i = 0; i < 17; i++) {
        $262.agent.start(
            'let view = new Int32Array(new SharedArrayBuffer(4));' +
            'Atomics.waitAsync(view, 0, 0, 1).value.then(function(value) {' +
            '$262.agent.report(value);$262.agent.leaving();});'
        );
    }
    let atomicsReports = 0;
    while (atomicsReports < 17) {
        // let real timeout callbacks run; reports contain the actual settled status.
        await new Promise(resolve => setTimeout(resolve, 2));
        let report;
        while ((report = $262.agent.getReport()) !== null) {
            if (report !== 'timed-out') throw new Error('unexpected timeout status');
            atomicsReports++;
        }
    }
    console.log(atomicsReports);

    let shared = new SharedArrayBuffer(4), sharedView = new Int32Array(shared);
    for (let i = 0; i < 129; i++) {
        $262.agent.start(
            '$262.agent.receiveBroadcast(function(buffer) {' +
            'Atomics.waitAsync(new Int32Array(buffer), 0, 0).value.then(function(value) {' +
            '$262.agent.report(value);$262.agent.leaving();});' +
            '$262.agent.report("ready");});'
        );
    }
    $262.agent.broadcast(shared);
    let ready = 0;
    while (ready < 129) {
        if ($262.agent.getReport() === 'ready') ready++;
    }
    console.log(Atomics.notify(sharedView, 0));
    let notifiedReports = 0;
    while (notifiedReports < 129) {
        let report = $262.agent.getReport();
        if (report === 'ok') notifiedReports++;
        else if (report !== null) throw new Error('unexpected notify status');
    }
    console.log(notifiedReports);
}
checkAgentState();
