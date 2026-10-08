// Calendar arithmetic uses UTC instants plus an explicit fixed offset in minutes.
import util: .util

pub fn timestamp(value) float | error => if (value is number) float(value) else float(datetime(value).unix)

pub fn offset(options) => if (options.timezone != null) options.timezone else 0

pub fn valid_offset(value) => value is number and util.finite_number(value) and floor(value) == value and abs(value) <= 840

pub fn wall_time(value, zone = 0) {
    let stamp = timestamp(value);
    if (stamp is error) stamp else datetime(i64(stamp + float(zone) * 60000.0)).utc
}

fn month_start(index, zone) {
    let year = int(floor(float(index) / 12.0));
    float(date(year, index - year * 12 + 1, 1).unix) - float(zone) * 60000.0
}

fn interval(count, span) {
    if (count is map) {unit: count.interval, step: if (count.step != null) count.step else 1}
    else {
        let target = abs(span) / max([1.0, float(count)]);
        let choices = [
            for (step in [1, 5, 10, 50, 100, 250, 500]) {unit: "millisecond", step: step, span: step},
            for (step in [1, 5, 15, 30]) {unit: "second", step: step, span: step * 1000.0},
            for (step in [1, 5, 15, 30]) {unit: "minute", step: step, span: step * 60000.0},
            for (step in [1, 3, 6, 12]) {unit: "hour", step: step, span: step * 3600000.0},
            {unit: "day", step: 1, span: 86400000.0}, {unit: "week", step: 1, span: 604800000.0},
            {unit: "month", step: 1, span: 2629800000.0}, {unit: "month", step: 3, span: 7889400000.0},
            {unit: "year", step: 1, span: 31557600000.0}];
        if (target > choices[len(choices) - 1].span)
            {unit: "year", step: max([1, int(util.nice_num(target / 31557600000.0, true))])}
        else sort(choices, {by: (choice) => abs(math.log(max([1.0, target]) / choice.span))})[0]
    }
}

fn valid_interval(chosen) => contains(["millisecond", "second", "minute", "hour", "day", "week", "month", "year"], chosen.unit) and
    chosen.step is number and util.finite_number(chosen.step) and chosen.step >= 1 and floor(chosen.step) == chosen.step

fn unit_ms(unit) => if (unit == "second") 1000.0 else if (unit == "minute") 60000.0
    else if (unit == "hour") 3600000.0 else if (unit == "day") 86400000.0
    else if (unit == "week") 604800000.0 else 1.0

fn floor_interval(value, chosen, zone) {
    let dt = wall_time(value, zone);
    if (chosen.unit == "year") float(date(int(floor(float(dt.year) / float(chosen.step))) * chosen.step, 1, 1).unix) - zone * 60000.0
    else if (chosen.unit == "month") month_start(int(floor(float(dt.year * 12 + dt.month - 1) / float(chosen.step))) * chosen.step, zone)
    else {
        let shift = float(zone) * 60000.0;
        // 1970-01-04 is Sunday, the boundary for week intervals.
        let anchor = if (chosen.unit == "week") 259200000.0 else 0.0;
        let step = unit_ms(chosen.unit) * chosen.step;
        floor((value + shift - anchor) / step) * step + anchor - shift
    }
}

fn advance(value, chosen, zone, count = 1) {
    let dt = wall_time(value, zone);
    if (chosen.unit == "year") float(date(dt.year + chosen.step * count, 1, 1).unix) - zone * 60000.0
    else if (chosen.unit == "month") month_start(dt.year * 12 + dt.month - 1 + chosen.step * count, zone)
    else value + unit_ms(chosen.unit) * chosen.step * count
}

pub fn ticks(lo, hi, count = 8, zone = 0) {
    if (lo > hi) reverse(ticks(hi, lo, count, zone))
    else if (count is number and count <= 0) []
    else if (lo == hi) [lo]
    else {
        let chosen = interval(count, hi - lo);
        if (not valid_interval(chosen) or not valid_offset(zone)) error("chart: invalid temporal tick interval or timezone")
        else {
            let boundary = floor_interval(lo, chosen, zone);
            let start = if (boundary < lo) advance(boundary, chosen, zone) else boundary;
            let first = wall_time(start, zone);
            let final_time = wall_time(hi, zone);
            let length = if (chosen.unit == "year") int(floor(float(final_time.year - first.year) / float(chosen.step)))
                else if (chosen.unit == "month") int(floor(float((final_time.year - first.year) * 12 + final_time.month - first.month) / float(chosen.step)))
                else int(floor((hi - start) / (unit_ms(chosen.unit) * chosen.step)));
            [for (index in 0 to length, let value = advance(start, chosen, zone, index) where value <= hi) value]
        }
    }
}

pub fn nice_extent(lo, hi, zone = 0) {
    if (lo > hi) reverse(nice_extent(hi, lo, zone))
    else {
        let chosen = interval(8, hi - lo);
        let start = floor_interval(lo, chosen, zone);
        let end = floor_interval(hi, chosen, zone);
        [start, if (end < hi or lo == hi) advance(end, chosen, zone) else end]
    }
}

// Cyclic units use 2012 (a leap year starting on Sunday); chronological units retain the year.
pub fn time_unit(value, specification, zone = 0) {
    let raw = if (specification is map) specification.unit else specification;
    let unit = if (starts_with(raw, "utc")) slice(raw, 3) else raw;
    let actual_zone = unit_zone(specification, zone);
    if (not valid_offset(actual_zone)) error("chart: invalid time-unit timezone")
    else if (value == null) null
    else {
        let dt = wall_time(value, actual_zone);
        let year = if (starts_with(unit, "year")) dt.year else 2012;
        let month = if (contains(unit, "month")) dt.month else if (contains(unit, "quarter")) (dt.quarter - 1) * 3 + 1 else 1;
        let day = if (unit == "day" or unit == "weekday") dt.weekday + 1
            else if (contains(unit, "date")) dt.day else 1;
        let hour = if (contains(unit, "hours") or unit == "hour") dt.hour else 0;
        let minute = if (contains(unit, "minutes") or unit == "minute") dt.minute else 0;
        let second = if (contains(unit, "seconds") or unit == "second") dt.second else 0;
        let supported = contains(["year", "yearquarter", "yearmonth", "yearmonthdate", "yearmonthdatehours",
            "yearmonthdatehoursminutes", "yearmonthdatehoursminutesseconds", "quarter", "month", "monthdate",
            "date", "day", "weekday", "hour", "hours", "hoursminutes", "hoursminutesseconds",
            "minute", "minutes", "minutesseconds", "second", "seconds"], unit);
        if (dt is error) dt else if (not supported) error("chart: unsupported time_unit " ++ string(raw))
        else float(date(year, month, day).unix) + ((hour * 60.0 + minute) * 60.0 + second) * 1000.0 - actual_zone * 60000.0
    }
}

pub fn unit_zone(specification, zone = 0) {
    let raw = if (specification is map) specification.unit else specification;
    if (starts_with(raw, "utc") or specification.utc) 0
    else if (specification.timezone != null) specification.timezone else zone
}

pub fn unit_format(specification) {
    let raw = if (specification is map) specification.unit else specification;
    let unit = if (starts_with(raw, "utc")) slice(raw, 3) else raw;
    if (unit == "year") "YYYY"
    else if (unit == "yearmonth" or unit == "yearquarter") "MMM YYYY"
    else if (unit == "month" or unit == "quarter") "MMM"
    else if (unit == "day" or unit == "weekday") "ddd"
    else if (contains(unit, "seconds") or unit == "second") "hh:mm:ss"
    else if (contains(unit, "hours") or contains(unit, "minutes") or unit == "hour" or unit == "minute") "hh:mm"
    else "MMM DD"
}
