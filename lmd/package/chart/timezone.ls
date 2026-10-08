// IANA transitions and POSIX recurrences keep zone arithmetic independent of host settings.
import numbers: .numbers

let database = input(sys.lambda.home# ++ "/package/chart/timezone_data.json", 'json') ^ { ~ }

pub fn valid_zone(zone) => if (zone is number) numbers.finite_number(zone) and floor(zone) == zone and abs(zone) <= 840
    else zone is string and not (database is error) and database.names[zone] != null

fn record(zone) {
    if (database is error) database
    else if (not (zone is string) or database.names[zone] == null) error("chart: unknown IANA time zone " ++ string(zone))
    else database.records[database.names[zone]]
}

// Find the last transition at or before the instant without scanning its history.
fn search(times, stamp, lo, hi) {
    if (lo >= hi) lo - 1
    else (let middle = int(floor(float(lo + hi) / 2.0)),
        if (times[middle] <= stamp) search(times, stamp, middle + 1, hi) else search(times, stamp, lo, middle))
}

fn rule_time(year, rule, future, before) {
    let parts = rule.values;
    let first = if (rule.kind == "M") date(year, parts[0], 1) else date(year, 1, 1);
    let next_month = if (rule.kind != "M") null else if (parts[0] == 12) date(year + 1, 1, 1) else date(year, parts[0] + 1, 1);
    let days = if (rule.kind == "M") int((next_month.unix - first.unix) / 86400000) else 0;
    let nth = if (rule.kind == "M") (parts[2] - first.weekday + 7) % 7 + (parts[1] - 1) * 7 else 0;
    let leap = year % 4 == 0 and (year % 100 != 0 or year % 400 == 0);
    let day = if (rule.kind == "M") (if (nth >= days) nth - 7 else nth)
        else if (rule.kind == "J") parts[0] - 1 + (if (leap and parts[0] >= 60) 1 else 0) else parts[0];
    let basis = if (rule.basis == "w") before.offset else if (rule.basis == "s") future.standard.offset else 0;
    if (first is error) first else float(first.unix) + day * 86400000.0 + (rule.seconds - basis) * 1000.0
}

fn future_events(future, year) => if (future.daylight == null) [] else [
    {at: rule_time(year, future.start, future, future.standard), before: future.standard, after: future.daylight},
    {at: rule_time(year, future.end, future, future.daylight), before: future.daylight, after: future.standard}]

fn future_info(stamp, future) {
    if (future.daylight == null) future.standard
    else {
        let local = datetime(i64(stamp + future.standard.offset * 1000.0)).utc;
        if (local is error) local else (
            let year = local.year,
            let events = sort([for (y in (year - 1) to (year + 1)) for (event in future_events(future, y) where event.at <= stamp) event], {by: (event) => event.at}),
            if (len(events) == 0) future.standard else events[len(events) - 1].after)
    }
}

pub fn info(stamp, zone = 0) {
    if (not numbers.finite_number(stamp)) error("chart: temporal instant must be finite")
    else if (zone is number) (if (valid_zone(zone)) {offset: zone * 60, name: "", dst: false} else error("chart: invalid UTC offset minutes"))
    else {
        let data = record(zone);
        if (data is error) data
        else if (data.future != null and (len(data.times) == 0 or stamp / 1000.0 > data.times[len(data.times) - 1])) future_info(stamp, data.future)
        else (let index = search(data.times, stamp / 1000.0, 0, len(data.times)),
            data.types[if (index < 0) 0 else data.indices[index]])
    }
}

pub fn offset_at(stamp, zone = 0) {
    let details = info(stamp, zone);
    if (details is error) details else float(details.offset) / 60.0
}

pub fn wall(stamp, zone = 0) {
    let details = info(stamp, zone);
    if (details is error) details else stamp + details.offset * 1000.0
}

fn two_digits(value) => (if (value < 10) "0" else "") ++ string(int(value))

pub fn format_timestamp(stamp, pattern, zone = 0) {
    let details = info(stamp, zone);
    if (details is error) details else {
        let seconds = abs(details.offset);
        let parts = [two_digits(floor(seconds / 3600.0)), two_digits(floor(seconds / 60.0) % 60),
            for (value in [seconds % 60] where value != 0) two_digits(value)];
        let prefix = if (details.offset < 0) "-" else "+";
        // Format the wall clock with the instant's real offset, including historical seconds.
        let adjusted = replace(replace(pattern, "ZZ", prefix ++ join(parts, "")), "Z", prefix ++ join(parts, ":"));
        datetime(i64(stamp + details.offset * 1000.0)).format(adjusted)
    }
}

pub fn transitions_between(lo, hi, zone) {
    let data = record(zone);
    if (data is error) data
    else {
        let last_transition = if (len(data.times) > 0) data.times[len(data.times) - 1] * 1000.0 else -inf;
        let historical = [for (index, at in data.times where at * 1000.0 >= lo and at * 1000.0 <= hi)
            {at: at * 1000.0, before: data.types[if (index == 0) 0 else data.indices[index - 1]], after: data.types[data.indices[index]]}];
        let future = if (data.future == null or hi <= last_transition) [] else [
            for (year in (datetime(i64(lo)).utc.year - 1) to (datetime(i64(hi)).utc.year + 1))
                for (event in future_events(data.future, year) where event.at > last_transition and event.at >= lo and event.at <= hi) event];
        sort([*historical, *future], {by: (event) => event.at})
    }
}

pub fn candidates(wall_stamp, zone) {
    if (zone is number) (if (valid_zone(zone)) [wall_stamp - zone * 60000.0] else error("chart: invalid UTC offset minutes"))
    else {
        let data = record(zone);
        if (data is error) data else sort([for (offset in data.offsets,
            let stamp = wall_stamp - offset * 1000.0 where info(stamp, zone).offset == offset) stamp])
    }
}

// Folds choose the earlier occurrence by default; gaps shift by the transition's actual jump.
pub fn from_wall(wall_stamp, zone, preferred = null, gap = "forward") {
    let choices = candidates(wall_stamp, zone);
    if (choices is error) choices
    else if (len(choices) > 0) (
        let earlier = if (preferred == null) [] else [for (stamp in choices where stamp <= preferred) stamp],
        if (len(earlier) > 0) earlier[len(earlier) - 1] else choices[0])
    else if (gap == "reject") null
    else {
        let data = record(zone);
        let transitions = transitions_between(wall_stamp - max(data.offsets) * 1000.0 - 1.0,
            wall_stamp - min(data.offsets) * 1000.0 + 1.0, zone);
        let gaps = [for (event in transitions where event.after.offset > event.before.offset and
            wall_stamp >= event.at + event.before.offset * 1000.0 and wall_stamp < event.at + event.after.offset * 1000.0) event];
        if (len(gaps) == 0) error("chart: local time cannot be resolved in " ++ string(zone))
        else wall_stamp - gaps[0].before.offset * 1000.0
    }
}

pub fn segments(lo, hi, zone) {
    let events = transitions_between(lo, hi, zone);
    if (events is error) events else (
        let boundaries = [lo, for (event in events where event.at > lo and event.at < hi) event.at, hi],
        [for (index in 0 to (len(boundaries) - 2))
            {lo: boundaries[index], hi: boundaries[index + 1], offset: offset_at(boundaries[index], zone)}])
}
