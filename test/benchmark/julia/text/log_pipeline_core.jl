# native port of test/benchmark/text/python/log_pipeline.py; see ../LICENSE.md.
# Mixed log parsing and aggregation workload from log_pipeline.js.
const ROUNDS = 180
const COUNT = 12000
const MODULUS = 1000000007
const SERVICES = ("api", "worker", "db", "cache")
const REGIONS = ("us-east", "eu-west", "ap-south")
function make_log_line(index)
    local latency, level, message, prefix, region, route, service, size, status, timestamp
    timestamp = string("2026-09-07T", format0(mod(index, 24), "02d"), ":", format0(mod(index, 60), "02d"), ":", format0(mod(mul0(index, 7), 60), "02d"), "Z")
    level = (truth0(((mod(index, 13) == 0))) ? "ERROR" : (truth0(((mod(index, 5) == 0))) ? "WARN" : "INFO"))
    service = get0(SERVICES, mod(index, Base.length(SERVICES)))
    region = get0(REGIONS, mod(mul0(index, 3), Base.length(REGIONS)))
    status = (truth0(((mod(index, 19) == 0))) ? 503 : (truth0(((mod(index, 7) == 0))) ? 404 : 200))
    latency = add0(mod(mul0(index, 37), 900), 4)
    size = add0(mod(mul0(index, 113), 50000), 512)
    route = (truth0(((mod(index, 2) == 0))) ? "/v1/items" : "/v1/search")
    message = (truth0(((mod(index, 11) == 0))) ? "retry-scheduled" : "request-complete")
    if truth0(((mod(index, 3) == 0)))
        prefix = string(string(timestamp), " level=", string(level), " service=", string(service))
    else
        prefix = string(string(timestamp), " ", string(level), " ", string(service))
    end
    return string(string(prefix), " status=", string(status), " latency=", string(latency), " region=", string(region), " route=", string(route), " bytes=", string(size), " message=", string(message))
end

function parse_log_line(line)
    local fields, key, record, separator, start, token, value
    fields = m_split(line, " ")
    record = Dict{String,Any}("timestamp"=>get0(fields, 0), "level"=>"", "service"=>"", "status"=>0, "latency"=>0, "region"=>"", "route"=>"", "bytes"=>0, "message"=>"")
    start = 1
    if truth0((!in0("=", get0(fields, 1))))
        set0!(record, "level", get0(fields, 1))
        set0!(record, "service", get0(fields, 2))
        start = 3
    end
    for token in slice0(fields, start, nothing, nothing)
        separator = m_find(token, "=")
        if truth0(((separator >= 0)))
            (key, value) = (slice0(token, nothing, separator, nothing), slice0(token, add0(separator, 1), nothing, nothing))
            set0!(record, key, (truth0((in0(key, ("status", "latency", "bytes")))) ? int0(value) : value))
        end
    end
    return record
end

function empty_group()
    return Dict{String,Any}("count"=>0, "errors"=>0, "slow"=>0, "total_latency"=>0, "total_bytes"=>0)
end

function process_logs(lines)
    local accepted, group, groups, line, record, rejected, service
    groups = Dict{Any,Any}(service=>empty_group() for service in SERVICES)
    let _assigned = 0
        accepted = _assigned
        rejected = _assigned
    end
    for line in lines
        record = parse_log_line(line)
        if truth0((let _bool_value = ((get0(record, "status") >= 500)); truth0(_bool_value) ? _bool_value : ((get0(record, "level") == "ERROR")) end))
            rejected = add0(rejected, 1)
            continue
        end
        group = get0(groups, get0(record, "service"))
        set0!(group, "count", add0(get0(group, "count"), 1))
        set0!(group, "total_latency", add0(get0(group, "total_latency"), get0(record, "latency")))
        set0!(group, "total_bytes", add0(get0(group, "total_bytes"), get0(record, "bytes")))
        if truth0(((get0(record, "latency") >= 500)))
            set0!(group, "slow", add0(get0(group, "slow"), 1))
        end
        accepted = add0(accepted, 1)
    end
    return (groups, accepted, rejected)
end

