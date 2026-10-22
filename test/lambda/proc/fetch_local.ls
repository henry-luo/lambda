import io

// io.fetch returns one response map (AR-E1), streams to disk with `to`, and
// reports progress through a callback run on the task's own stack (AR-E3v2).
// A collector task accumulates the levels: a closure's captured vars are copies.
pn collect() {
    var hits = 0
    var seen = -1
    var ordered = true
    var total = null
    var running = true
    while (running) {
        let m = receive()^
        if (m.type == 'stop') {
            send(m.reply, {hits: hits, seen: seen, ordered: ordered, total: total})^
            running = false
        } else {
            hits = hits + 1
            if (m.received < seen) { ordered = false }
            seen = m.received
            total = m.total
        }
    }
}

pn main() {
    let page = io.fetch("test/input/test_data.html")^
    output(b'\x00010203FF', "temp/fetch_bytes.bin")^
    let bytes = io.fetch("temp/fetch_bytes.bin")^
    var missing = null
    io.fetch("test/input/does_not_exist.txt") ^ { missing = ^ }
    // a local source copied in chunks exercises the same progress path as a download
    io.delete("temp/fetch_source.txt") ^ { null }
    let chunk = format([for (i in 1 to 50000) i], 'json')
    for (k in 1 to 6) { output(chunk, "temp/fetch_source.txt", {mode: 'append'})^ }
    let size = len(chunk) * 6
    let c = start(collect)
    let copied = io.fetch("temp/fetch_source.txt", {to: "temp/fetch_copy.txt", progress_ms: 0,
        progress: pn (p) => { send(c, {type: 'progress', received: p.received, total: p.total})^ }})^
    send(c, {type: 'stop', reply: self()})^
    let summary = receive()^
    // a callback that raises aborts the transfer and leaves no partial file
    var aborted = null
    io.fetch("temp/fetch_source.txt", {to: "temp/fetch_abort.txt", progress_ms: 0,
        progress: pn (p) => { raise error("stop here") }}) ^ { aborted = ^ }
    var limited = null
    io.fetch("temp/fetch_source.txt", {max_bytes: 1000}) ^ { limited = ^ }
    // a single byte range is honoured on a local source as a server would (HLS byte ranges)
    let ranged = io.fetch("temp/fetch_source.txt", {headers: {Range: "bytes=1-5"}})^
    let open_ended = io.fetch("temp/fetch_source.txt", {headers: {Range: "bytes=" ++ string(size - 3) ++ "-"}})^
    let past = io.fetch("temp/fetch_source.txt", {headers: {range: "bytes=" ++ string(size) ++ "-"}})^;
    [page.status == 200 and page.ok, contains(page.url, "/test/input/test_data.html") and starts_with(page.url, "file://"),
     page.headers.'content-type' == "text/html", page.body is string and contains(page.body, "<h1>Welcome</h1>"),
     page.bytes > 0, bytes.body is binary and bytes.bytes == 5 and bytes.headers.'content-type' != null,
     missing is error and contains(missing.message, "does_not_exist"),
     copied.status == 200 and copied.body == null and copied.bytes == size,
     summary.hits > 0 and summary.ordered and summary.seen == size and summary.total == size,
     input("temp/fetch_copy.txt", 'binary')^ == input("temp/fetch_source.txt", 'binary')^,
     aborted is error and aborted.message == "stop here" and not exists("temp/fetch_abort.txt")
        and not exists("temp/fetch_abort.txt.part"),
     limited is error and contains(limited.message, "limit"),
     ranged.status == 206 and ranged.ok and ranged.body == slice(chunk, 1, 6) and ranged.bytes == 5 and
        ranged.headers.'content-range' == "bytes 1-5/" ++ string(size),
     open_ended.status == 206 and open_ended.body == slice(chunk, len(chunk) - 3, len(chunk)),
     past.status == 416 and not past.ok and past.bytes == 0]
}
