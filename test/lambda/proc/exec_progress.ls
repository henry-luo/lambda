import io

// io.exec {progress} (AR-E5v2): the latest complete stdout line reaches the
// callback on the task's own stack while the process runs; the trailing
// unterminated line arrives before the result.
pn collect() {
    var hits = 0
    var seen = null
    var running = true
    while (running) {
        let m = receive()^
        if (m.type == 'stop') {
            send(m.reply, {hits: hits, seen: seen})^
            running = false
        } else {
            hits = hits + 1
            seen = m.line
        }
    }
}

pn main() {
    let c = start(collect)
    let script = "import time\nfor i in range(5):\n    print('line', i, flush=True)\n    time.sleep(0.02)\nprint('tail', end='')"
    let r = io.exec("python3", ["-u", "-c", script], {progress_ms: 0,
        progress: pn (p) => { send(c, {type: 'line', line: p.line})^ }})^
    send(c, {type: 'stop', reply: self()})^
    let s = receive()^
    // a quiet run makes no progress calls; a raising callback terminates the process
    let quiet = io.exec("python3", ["-c", "pass"], {progress: pn (p) => { raise error("unexpected") }})^
    var aborted = null
    io.exec("python3", ["-u", "-c", "import time\nprint('x', flush=True)\ntime.sleep(5)"],
        {progress_ms: 0, progress: pn (p) => { raise error("enough") }}) ^ { aborted = ^ };
    [r.exit_code == 0, r.stdout == binary("line 0\nline 1\nline 2\nline 3\nline 4\ntail"),
     s.hits >= 2, s.seen == "tail", quiet.exit_code == 0,
     aborted is error and aborted.message == "enough"]
}
