// Text benchmark: parse, filter, group, and aggregate mixed log records.

const LOG_ROUNDS = 180;
const LOG_COUNT = 12000;
const MODULUS = 1000000007;

function pad2(value) {
  return value < 10 ? "0" + value : String(value);
}

function makeLogLine(index) {
  const hour = pad2(index % 24);
  const minute = pad2(index % 60);
  const second = pad2((index * 7) % 60);
  const timestamp = "2026-09-07T" + hour + ":" + minute + ":" + second + "Z";
  const level = index % 13 === 0 ? "ERROR" : index % 5 === 0 ? "WARN" : "INFO";
  const services = ["api", "worker", "db", "cache"];
  const regions = ["us-east", "eu-west", "ap-south"];
  const service = services[index % services.length];
  const region = regions[(index * 3) % regions.length];
  const status = index % 19 === 0 ? 503 : index % 7 === 0 ? 404 : 200;
  const latency = (index * 37) % 900 + 4;
  const bytes = (index * 113) % 50000 + 512;
  const route = index % 2 === 0 ? "/v1/items" : "/v1/search";
  const message = index % 11 === 0 ? "retry-scheduled" : "request-complete";
  if (index % 3 === 0) {
    return timestamp +
      " level=" + level +
      " service=" + service +
      " status=" + status +
      " latency=" + latency +
      " region=" + region +
      " route=" + route +
      " bytes=" + bytes +
      " message=" + message;
  }
  return timestamp + " " + level + " " + service +
    " status=" + status +
    " latency=" + latency +
    " region=" + region +
    " route=" + route +
    " bytes=" + bytes +
    " message=" + message;
}

function buildLogs() {
  const lines = [];
  for (let index = 0; index < LOG_COUNT; index += 1) {
    lines.push(makeLogLine(index));
  }
  return lines;
}

const logs = buildLogs();

function setField(record, key, value) {
  if (key === "level") record.level = value;
  else if (key === "service") record.service = value;
  else if (key === "status") record.status = Number(value);
  else if (key === "latency") record.latency = Number(value);
  else if (key === "region") record.region = value;
  else if (key === "route") record.route = value;
  else if (key === "bytes") record.bytes = Number(value);
  else if (key === "message") record.message = value;
}

function parseLogLine(line) {
  const fields = line.split(" ");
  const record = {
    timestamp: fields[0],
    level: "",
    service: "",
    status: 0,
    latency: 0,
    region: "",
    route: "",
    bytes: 0,
    message: "",
  };
  let start = 1;
  if (fields[1].indexOf("=") < 0) {
    record.level = fields[1];
    record.service = fields[2];
    start = 3;
  }
  for (let index = start; index < fields.length; index += 1) {
    const token = fields[index];
    const separator = token.indexOf("=");
    if (separator >= 0) setField(record, token.slice(0, separator), token.slice(separator + 1));
  }
  return record;
}

function emptyGroup() {
  return { count: 0, errors: 0, slow: 0, totalLatency: 0, totalBytes: 0 };
}

function processLogs(lines) {
  const groups = { api: emptyGroup(), worker: emptyGroup(), db: emptyGroup(), cache: emptyGroup() };
  let accepted = 0;
  let rejected = 0;
  for (const line of lines) {
    const record = parseLogLine(line);
    if (record.status >= 500 || record.level === "ERROR") {
      rejected += 1;
      continue;
    }
    const group = groups[record.service];
    group.count += 1;
    group.totalLatency += record.latency;
    group.totalBytes += record.bytes;
    if (record.latency >= 500) group.slow += 1;
    accepted += 1;
  }
  return { groups, accepted, rejected };
}

let checksum = 0;
const t0 = process.hrtime.bigint();
for (let round = 0; round < LOG_ROUNDS; round += 1) {
  const result = processLogs(logs);
  checksum =
    (checksum + result.accepted * 31 + result.rejected * 17 +
      result.groups.api.totalLatency + result.groups.worker.totalBytes + round) % MODULUS;
}
const t1 = process.hrtime.bigint();

if (checksum !== 292634526) throw new Error("unexpected log_pipeline checksum");
process.stdout.write("log_pipeline: CHECKSUM:" + checksum + "\n");
process.stdout.write("__TIMING__:" + Number(t1 - t0) / 1e6 + "\n");
