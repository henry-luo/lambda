// Typed text benchmark: parse, filter, group, and aggregate mixed log records.
//
// `key: string` on set_field is the load-bearing annotation: its eight-arm
// `key == "literal"` dispatch is the whole cost of field assignment, and a string
// parameter takes each arm off generic `fn_eq`. The record and group maps stay
// untyped -- they are handed to `var` parameters, where a named contract would
// reify the packed layout on every construction.

let log_rounds = 180
let log_count = 12000
let modulus = 1000000007

fn pad2(value: int) {
    if (value < 10) { "0" ++ string(value) }
    else { string(value) }
}

fn log_level(index: int) {
    if (index % 13 == 0) { "ERROR" }
    else if (index % 5 == 0) { "WARN" }
    else { "INFO" }
}

fn log_status(index: int) {
    if (index % 19 == 0) { 503 }
    else if (index % 7 == 0) { 404 }
    else { 200 }
}

fn log_route(index: int) {
    if (index % 2 == 0) { "/v1/items" } else { "/v1/search" }
}

fn log_message(index: int) {
    if (index % 11 == 0) { "retry-scheduled" } else { "request-complete" }
}

fn make_log_line(index: int) {
    let hour = pad2(index % 24)
    let minute = pad2(index % 60)
    let second = pad2((index * 7) % 60)
    let timestamp = "2026-09-07T" ++ hour ++ ":" ++ minute ++ ":" ++ second ++ "Z"
    let level = log_level(index)
    let services = ["api", "worker", "db", "cache"]
    let regions = ["us-east", "eu-west", "ap-south"]
    let service = services[index % len(services)]
    let region = regions[(index * 3) % len(regions)]
    let status = log_status(index)
    let latency = (index * 37) % 900 + 4
    let bytes = (index * 113) % 50000 + 512
    let route = log_route(index)
    let message = log_message(index)
    if (index % 3 == 0) {
        timestamp ++
            " level=" ++ level ++
            " service=" ++ service ++
            " status=" ++ string(status) ++
            " latency=" ++ string(latency) ++
            " region=" ++ region ++
            " route=" ++ route ++
            " bytes=" ++ string(bytes) ++
            " message=" ++ message
    } else {
        timestamp ++ " " ++ level ++ " " ++ service ++
            " status=" ++ string(status) ++
            " latency=" ++ string(latency) ++
            " region=" ++ region ++
            " route=" ++ route ++
            " bytes=" ++ string(bytes) ++
            " message=" ++ message
    }
}

pn build_logs() {
    var lines = []
    var index = 0
    while (index < log_count) {
        lines.push(make_log_line(index))
        index = index + 1
    }
    lines
}

pn set_field(var record, key: string, value: string) {
    if (key == "level") { record.level = value }
    else if (key == "service") { record.service = value }
    else if (key == "status") { record.status = int(value) }
    else if (key == "latency") { record.latency = int(value) }
    else if (key == "region") { record.region = value }
    else if (key == "route") { record.route = value }
    else if (key == "bytes") { record.bytes = int(value) }
    else if (key == "message") { record.message = value }
}

pn parse_log_line(line: string) {
    let fields = split(line, " ")
    var record = {
        timestamp: fields[0], level: "", service: "", status: 0, latency: 0,
        region: "", route: "", bytes: 0, message: ""
    }
    var field_start = 1
    if (index_of(fields[1], "=") == null) {
        record.level = fields[1]
        record.service = fields[2]
        field_start = 3
    }
    var index = field_start
    while (index < len(fields)) {
        let token = fields[index]
        let separator = index_of(token, "=")
        if (separator != null) {
            set_field(record, slice(token, 0, separator), slice(token, separator + 1, len(token)))
        }
        index = index + 1
    }
    record
}

fn empty_group() => {count: 0, errors: 0, slow: 0, totalLatency: 0, totalBytes: 0}

pn add_to_group(var group, record) {
    group.count = group.count + 1
    group.totalLatency = group.totalLatency + record.latency
    group.totalBytes = group.totalBytes + record.bytes
    if (record.latency >= 500) { group.slow = group.slow + 1 }
}

pn process_logs(lines) {
    var groups = {api: empty_group(), worker: empty_group(), db: empty_group(), cache: empty_group()}
    var accepted = 0
    var rejected = 0
    var index = 0
    while (index < len(lines)) {
        let record = parse_log_line(lines[index])
        if (record.status >= 500 or record.level == "ERROR") {
            rejected = rejected + 1
        } else {
            if (record.service == "api") { add_to_group(groups.api, record) }
            else if (record.service == "worker") { add_to_group(groups.worker, record) }
            else if (record.service == "db") { add_to_group(groups.db, record) }
            else { add_to_group(groups.cache, record) }
            accepted = accepted + 1
        }
        index = index + 1
    }
    {groups: groups, accepted: accepted, rejected: rejected}
}

pn main() {
    let logs = build_logs()
    var checksum: int = 0
    let t0 = clock()
    var pass: int = 0
    while (pass < log_rounds) {
        let result = process_logs(logs)
        checksum = (checksum + result.accepted * 31 + result.rejected * 17 +
            result.groups.api.totalLatency + result.groups.worker.totalBytes + pass) % modulus
        pass = pass + 1
    }
    if (checksum == 292634526) {
        print("log_pipeline: CHECKSUM:" ++ string(checksum) ++ "\n")
    } else {
        print("log_pipeline: FAIL checksum=" ++ string(checksum) ++ "\n")
    }
    print("__TIMING__:" ++ string((clock() - t0) * 1000.0) ++ "\n")
}
