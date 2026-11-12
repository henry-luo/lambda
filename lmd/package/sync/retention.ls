// calendar policy never orders edits; causal heads and host pins override thinning.
import calendar: lambda.chart.calendar
import zones: lambda.chart.timezone
import v: .schema
import codec: .codec

import defaults: .retention_policy
pub let permanent = defaults.permanent
pub let daily = defaults.daily
pub fn validate(policy) any^ {
    v.fields(policy,["version","mode","timezone","keep_all_ms","rules","arrival_grace_ms","delete_grace_ms","clock_skew_ms"])^
    if (policy.version != 1 or not contains(['forever','buckets'],policy.mode)^ or
        not zones.valid_zone(policy.timezone)^ or not (policy.rules is array) or len(policy.rules)^ > 16 or
        not all([for (n in [policy.keep_all_ms,policy.arrival_grace_ms,policy.delete_grace_ms,policy.clock_skew_ms])
            n is number and floor(n) == n and n >= 0])) raise error("sync retention: invalid policy")
    for (rule in policy.rules) {
        v.fields(rule,["unit","count"])^
        if (not contains(['day','week','month'],rule.unit)^ or
            (rule.count != null and (not v.natural(rule.count)^ or rule.count < 1)))
            raise error("sync retention: invalid bucket rule")
    }
    return true
}
pub fn stamp(header, policy) any^ => if (header.saved_at == null or header.saved_at <= 0 or
    header.saved_at > header.arrived_at+policy.clock_skew_ms) header.arrived_at else header.saved_at
pub fn bucket(milliseconds, unit, zone) any^ {
    let wall = calendar.wall_time(milliseconds,zone)^
    let day = int(floor(zones.wall(milliseconds,zone)^/86400000.0))
    return if (unit == 'day') day else if (unit == 'week') int(floor((day+3)/7.0))
        else wall.year*12+wall.month-1
}
pub fn ancestor(headers, parent, child) any^ {
    var pending = [child]
    var seen = []
    while (len(pending)^ > 0) {
        let id = pending[0]
        pending = slice(pending,1)^
        if (id == parent) { return true }
        if (not contains(seen,id)^) {
            seen = [*seen,id]
            let found = v.lookup(headers,id)^
            if (found != null) { pending = [*pending,*found.parents] }
        }
    }
    return false
}
pub fn extremes(headers, subset, latest=true) any^ => [for (header in subset where
    not any([for (other in subset) other.id != header.id and
        (if (latest) ancestor(headers,header.id,other.id)^ else ancestor(headers,other.id,header.id)^)])) header]
pub fn heads(headers) any^ => [for (header in headers where
    not any([for (other in headers) other.group == header.group and contains(other.parents,header.id)^])) header.id]

pub fn select(headers, policy, milliseconds, pins=[]) any^ {
    validate(policy)^
    if (not (milliseconds is number) or milliseconds < 0 or not (headers is array) or
        len(unique([for (header in headers) header.id])) != len(headers)^)
        raise error("sync retention: invalid history/time")
    for (header in headers) {
        if (not (header.arrived_at is number) or header.arrived_at < 0 or not (header.parents is array))
            raise error("sync retention: invalid history header")
    }
    let protected = unique([*pins,*heads(headers)^])
    var keep = [for (header in headers where policy.mode == 'forever' or contains(protected,header.id)^ or
        milliseconds-header.arrived_at < policy.arrival_grace_ms or
        milliseconds-stamp(header,policy)^ < policy.keep_all_ms or
        bucket(stamp(header,policy)^,'day',policy.timezone)^ >= bucket(milliseconds,'day',policy.timezone)^) header.id]
    var rule_index = 0
    while (rule_index < len(policy.rules)^) {
        let rule = policy.rules[rule_index]
        let candidates = [for (header in headers where
            bucket(stamp(header,policy)^,rule.unit,policy.timezone)^ < bucket(milliseconds,rule.unit,policy.timezone)^ and
            (rule.count == null or bucket(milliseconds,rule.unit,policy.timezone)^-
                bucket(stamp(header,policy)^,rule.unit,policy.timezone)^ <= rule.count)) header]
        let groups = unique([for (header in candidates) [header.group,bucket(stamp(header,policy)^,rule.unit,policy.timezone)^]])
        var group_index = 0
        while (group_index < len(groups)^) {
            let group = groups[group_index]
            let members = [for (header in candidates where header.group == group[0] and
                bucket(stamp(header,policy)^,rule.unit,policy.timezone)^ == group[1]) header]
            let latest = extremes(headers,members)^
            let ordered = sort([for (header in latest) {*:header,rank:stamp(header,policy)^}],(header)=>[header.rank,header.id])^
            if (len(ordered)^ > 0) { keep = [*keep,ordered[len(ordered)^-1].id] }
            group_index = group_index+1
        }
        rule_index = rule_index+1
    }
    let retained = sort(unique(keep))^
    return {policy:codec.digest(policy)^,retained:retained,
        candidates:sort([for (header in headers where not contains(retained,header.id)^) header.id])^,
        protected:sort(protected)^,at:milliseconds}
}
