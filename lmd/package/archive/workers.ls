// The caller's side of the worker (AR-O12): write the request into the staging
// folder, run `lambda run worker.ls` through io.exec with the request's path in
// LAMBDA_ARCHIVE_REQUEST (`lambda run` takes no script arguments), forward its step lines
// as `render` progress (AR12v2), and read derived.json back. A worker that
// fails leaves every requested output failed with its message; the capture
// itself is untouched (AR9).
let SCRIPT = "/package/archive/worker.ls"

fn tail_line(text) => if (text == null) "" else (let lines = [for (l in split(trim(string(text)), "\n") where trim(l) != "") l],
    if (len(lines) == 0) "" else lines[len(lines) - 1])
fn failed_all(outputs, message) => reduce([{}, *outputs], (acc, o) => {*: acc, [o]: {status: "failed", message: message}})

// the lambda executable running this script, which the worker reuses
pub pn executable() string { sys.proc.self.argv#[0] }

pub pn derive(staging, request, settings, notify) map^ {
    let request_path = staging ++ "/request.json"
    // the worker resolves assets/ against the folder as a file: URL, so it gets the absolute path
    let dir = if (starts_with(staging, "/")) staging else sys.proc.self.cwd# ++ "/" ++ staging
    output({*: request, staging: dir}, request_path, 'json')^
    let progress = if (notify == null) null else pn (p) => {
        let parts = split(trim(p.line), " ")
        if (len(parts) == 4 and parts[0] == "step") {
            notify({type: "progress", stage: "render", step: parts[3], received: int(parts[1]) - 1, total: int(parts[2]), rate: null})
        }
    }
    // --no-log: a worker writing ./log.txt would replace the caller's log
    let args = ["run", "--no-log", sys.lambda.home# ++ SCRIPT]
    let exec_options = {env: {LAMBDA_ARCHIVE_REQUEST: request_path}, timeout_ms: int(settings.render_timeout_s * 1000),
        progress: progress, progress_ms: int(settings.progress_ms)}
    var result = null
    var failure = null
    io.exec(executable(), args, exec_options) ^ { failure = ^ } ~ { result = ~ }
    // a cancelled job stops here; any other worker failure is recorded per output (AR9)
    if (failure is error and failure.code == 319) raise failure
    let derived_path = staging ++ "/derived.json"
    let message = if (failure is error) "worker: " ++ failure.message
        else if (result.timed_out) "worker: timed out after " ++ string(settings.render_timeout_s) ++ " s"
        else if (result.exit_code != 0 or not exists(derived_path)) "worker: exit " ++ string(result.exit_code) ++ ": " ++ tail_line(result.stderr)
        else null
    if (message != null) {meta: null, readable: null, outputs: failed_all(request.outputs, message)}
    else input(derived_path, 'json')^
}
