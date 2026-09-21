// Typed variant: scan log fields as spans and retain only the scalar state
// consumed by the aggregate, avoiding a heap record for every parsed line.

let log_rounds = 180
let log_count = 12000
let modulus = 1000000007

fn pad2(value: int) string {
    if (value < 10) { "0" ++ string(value) }
    else { string(value) }
}

fn log_level(index: int) string {
    if (index % 13 == 0) { "ERROR" }
    else if (index % 5 == 0) { "WARN" }
    else { "INFO" }
}

fn log_status(index: int) int {
    if (index % 19 == 0) { 503 }
    else if (index % 7 == 0) { 404 }
    else { 200 }
}

fn log_route(index: int) string {
    if (index % 2 == 0) { "/v1/items" } else { "/v1/search" }
}

fn log_message(index: int) string {
    if (index % 11 == 0) { "retry-scheduled" } else { "request-complete" }
}

fn make_log_line(index: int) string {
    let hour = pad2(index % 24)
    let minute = pad2(index % 60)
    let second = pad2((index * 7) % 60)
    let timestamp = "2026-09-07T" ++ hour ++ ":" ++ minute ++ ":" ++ second ++ "Z"
    let level = log_level(index)
    let services: string[] = ["api", "worker", "db", "cache"]
    let regions: string[] = ["us-east", "eu-west", "ap-south"]
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

pn build_logs() string[] {
    var lines: string[] = []
    var index: int = 0
    while (index < log_count) {
        lines.push(make_log_line(index))
        index = index + 1
    }
    lines
}

pn decimal_span(line: string, start: int, end: int) int {
    var value: int = 0
    var index: int = start
    while (index < end) {
        value = value * 10 + ord(line[index]) - 48
        index = index + 1
    }
    value
}

// The layout matches C2MIR's four scalar groups: count, errors, slow,
// total latency and total bytes. The benchmark's output reads api latency and
// worker bytes, so all groups remain live through the timed parse.
pn process_logs(lines: string[]) int {
    var totals: int[] = fill(22, 0)
    var row: int = 0
    while (row < len(lines)) {
        let line: string = lines[row]
        let line_length: int = len(line)
        var cursor: int = 0
        var token_number: int = 0
        var is_error: int = 0
        var service: int = 0
        var status: int = 0
        var latency: int = 0
        var region: int = 0
        var route: int = 0
        var bytes: int = 0
        var message: int = 0

        while (cursor < line_length) {
            while (cursor < line_length and line[cursor] == " ") {
                cursor = cursor + 1
            }
            let token_start: int = cursor
            while (cursor < line_length and line[cursor] != " ") {
                cursor = cursor + 1
            }
            let token_end: int = cursor
            var equal_at: int = token_start
            while (equal_at < token_end and line[equal_at] != "=") {
                equal_at = equal_at + 1
            }

            if (token_number == 1 and equal_at == token_end) {
                is_error = if (line[token_start] == "E") 1 else 0
            } else if (token_number == 2 and equal_at == token_end) {
                let first: string = line[token_start]
                service = if (first == "a") 0 else if (first == "w") 1 else
                    if (first == "d") 2 else 3
            } else if (equal_at < token_end) {
                let key_length: int = equal_at - token_start
                let value_start: int = equal_at + 1
                let first: string = line[token_start]
                if (first == "l" and key_length == 5) {
                    is_error = if (line[value_start] == "E") 1 else 0
                } else if (first == "l" and key_length == 7) {
                    latency = decimal_span(line, value_start, token_end)
                } else if (first == "s" and key_length == 7) {
                    let service_first: string = line[value_start]
                    service = if (service_first == "a") 0 else
                        if (service_first == "w") 1 else
                        if (service_first == "d") 2 else 3
                } else if (first == "s" and key_length == 6) {
                    status = decimal_span(line, value_start, token_end)
                } else if (first == "r" and key_length == 6) {
                    region = if (line[value_start] == "u") 0 else
                        if (line[value_start] == "e") 1 else 2
                } else if (first == "r" and key_length == 5) {
                    route = if (line[value_start + 4] == "i") 0 else 1
                } else if (first == "b") {
                    bytes = decimal_span(line, value_start, token_end)
                } else if (first == "m") {
                    message = if (line[value_start] == "r") 0 else 1
                }
            }
            token_number = token_number + 1
        }

        // Retain the parse of fields that do not contribute to the aggregate.
        if (region < 0 or route < 0 or message < 0) { return -1 }
        if (status >= 500 or is_error == 1) {
            totals[1] = totals[1] + 1
        } else {
            let group: int = 2 + service * 5
            totals[0] = totals[0] + 1
            totals[group] = totals[group] + 1
            totals[group + 3] = totals[group + 3] + latency
            totals[group + 4] = totals[group + 4] + bytes
            if (latency >= 500) { totals[group + 2] = totals[group + 2] + 1 }
        }
        row = row + 1
    }
    totals[0] * 31 + totals[1] * 17 + totals[5] + totals[11]
}

pn main() {
    let logs = build_logs()
    var checksum: int = 0
    let t0 = clock()
    var pass: int = 0
    while (pass < log_rounds) {
        checksum = (checksum + process_logs(logs) + pass) % modulus
        pass = pass + 1
    }
    if (checksum == 292634526) {
        print("log_pipeline: CHECKSUM:" ++ string(checksum) ++ "\n")
    } else {
        print("log_pipeline: FAIL checksum=" ++ string(checksum) ++ "\n")
    }
    print("__TIMING__:" ++ string((clock() - t0) * 1000.0) ++ "\n")
}
