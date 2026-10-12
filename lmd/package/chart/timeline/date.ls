// package dates use astronomical years and exact coordinates; no ambient clock.
import c: .common
import util: lambda.chart.util
import zones: lambda.chart.timezone
import locale: .locale

type iso_year = \(("+" | "-")? "\d"{4+})
type iso_month = \(("+" | "-")? "\d"{4+} "-" "\d"{2})
type iso_day = \(("+" | "-")? "\d"{4+} "-" "\d"{2} "-" "\d"{2})
type iso_full = \(("+" | "-")? "\d"{4+} "-" "\d"{2} "-" "\d"{2} ("T" | " ") "\d"{2} ":" "\d"{2}
    (":" "\d"{2} ("." "\d"{1,3})?)? ("Z" | "z" | ("+" | "-") "\d"{2} ":" "\d"{2})?)

pub fn floor_div(value, divisor) => value div divisor - (if (value < 0 and value % divisor != 0) 1 else 0)
pub fn leap(year) => year % 4 == 0 and (year % 100 != 0 or year % 400 == 0)
pub fn month_days(year, month) => if (month == 2) (if (leap(year)) 29 else 28)
    else if (contains([4, 6, 9, 11], month)) 30 else 31

// Gregorian civil arithmetic works for BCE and avoids the native datetime range.
pub fn days(year, month = 1, day = 1) {
    let y = year - (if (month <= 2) 1 else 0);
    let era = floor_div(y, 400);
    let within = y - era * 400;
    let shifted = month + (if (month > 2) -3 else 9);
    let doy = (153 * shifted + 2) div 5 + day - 1;
    era * 146097 + within * 365 + within div 4 - within div 100 + doy - 719468
}

pub fn civil(day_number) {
    let z = day_number + 719468;
    let era = floor_div(z, 146097);
    let doe = z - era * 146097;
    let yoe = (doe - doe div 1460 + doe div 36524 - doe div 146096) div 365;
    let y = yoe + era * 400;
    let doy = doe - (365 * yoe + yoe div 4 - yoe div 100);
    let mp = (5 * doy + 2) div 153;
    let month = mp + (if (mp < 10) 3 else -9);
    {year: y + (if (month <= 2) 1 else 0), month: month, day: doy - (153 * mp + 2) div 5 + 1}
}

fn two(value) => locale.pad_digits(value,2)
pub fn label(record, options = {}) {
    let year = record.year;
    let language=c.option(options,"locale","en-US");
    let code=locale.language(language);
    let bce=c.option(c.attrs(options.era_labels),"bce",c.option(options,"bce",locale.era(language,"bce")));
    let era = if (options.numbering=="astronomical") string(year)
        else if (year <= 0) string(1 - year) ++ " " ++ bce
        else string(year) ++ (if (options.era_labels.ce!=null) " " ++ c.text(options.era_labels.ce) else "");
    let precision=record.precision;
    let pattern=options.format;
    let offset=if (record.timezone is number) record.timezone else if (record.value!=null and record.timezone!=null)
        zones.offset_at(record.value,record.timezone) else 0;
    let formatted=if (pattern is string) locale.format_date({*:record,weekday:((days(year,record.month,record.day)+4)%7+7)%7},pattern,language,offset,c.attrs(options.era_labels))
        else if (code=="zh") (if (year<=0 and options.numbering!="astronomical") c.text(bce) ++ string(1-year) else era) ++ "年" ++
            (if (precision=="year") "" else string(record.month) ++ "月" ++ (if (precision=="month") "" else string(record.day) ++ "日"))
        else if (code=="fr" or code=="ar") (if (precision=="year") era else
            (if (precision=="month") "" else two(record.day) ++ "/") ++ two(record.month) ++ "/" ++ era)
        else if (precision=="year") era
        else (if (year<=0) era ++ " " else era ++ "-") ++ two(record.month) ++
            (if (precision=="month") "" else "-" ++ two(record.day));
    let clock=if (pattern is string or not contains(["hour","minute","second","millisecond"],precision)) ""
        else " " ++ two(record.hour) ++ ":" ++ two(record.minute) ++
            (if (contains(["second","millisecond"],precision)) ":" ++ two(record.second) else "") ++
            (if (precision=="millisecond") "." ++ locale.pad_digits(record.millisecond,3) else "");
    if (record.display_date != null) record.display_date
    else if (offset is error) offset else if (pattern is string) formatted else locale.localize_digits(formatted ++ clock,language)
}

fn formatted(record, options) {
    let value=if (record.kind=="calendar") label(record,options) else
        if (record.display_date!=null) c.text(record.display_date) else locale.format_number(record.value,c.option(options,"locale","en-US"),
            if (options.format is string) {pattern:options.format} else {});
    let labelled={*:record,label:value};
    let rendered=if (record.display_date!=null or not (options.formatter is fn)) value else
        options.formatter(labelled,{locale:c.option(options,"locale","en-US"),timezone:record.timezone,precision:record.precision});
    if (rendered is error) rendered else {*:labelled,label:rendered}
}

fn parse_iso(value) {
    let source = if (value is datetime) slice(string(value), 2, len(string(value)) - 1) else value;
    if (not (source is string) or not (source is iso_year or source is iso_month or source is iso_day or source is iso_full))
        c.fail("date", "expected an ISO year, date, or timestamp")
    else {
        let start = if (starts_with(source, "-") or starts_with(source, "+")) 1 else 0;
        let year_end = index_of(slice(source, start), "-");
        let yend = if (year_end == null or year_end < 0) len(source) else year_end + start;
        let clock = if (len(source) > yend + 6) slice(source, yend + 7) else "";
        let offset_index = if (contains(clock, "+")) index_of(clock, "+") else index_of(clock, "-");
        let offset_text = if (offset_index != null and offset_index >= 0) slice(clock, offset_index) else null;
        let fraction = if (len(clock) > 8 and slice(clock, 8, 9) == ".")
            slice(clock, 9, if (offset_text != null) offset_index else if (ends_with(lower(clock), "z")) len(clock) - 1 else len(clock)) else "";
        let offset = if (offset_text != null) (if (starts_with(offset_text, "-")) -1 else 1) *
            (int(slice(offset_text, 1, 3)) * 60 + int(slice(offset_text, 4, 6)))
            else if (ends_with(lower(clock), "z")) 0 else null;
        if (offset_text!=null and (int(slice(offset_text,1,3))>23 or int(slice(offset_text,4,6))>59))
            c.fail("date.timezone","invalid ISO offset") else {year: i64(slice(source, 0, yend)),
            month: if (len(source) > yend) int(slice(source, yend + 1, yend + 3)) else null,
            day: if (len(source) > yend + 3) int(slice(source, yend + 4, yend + 6)) else null,
            hour: if (len(clock) > 0) int(slice(clock, 0, 2)) else null,
            minute: if (len(clock) > 0) int(slice(clock, 3, 5)) else null,
            second: if (len(clock) >= 8 and slice(clock, 5, 6) == ":") int(slice(clock, 6, 8)) else null,
            millisecond: if (len(fraction) > 0) int(fraction ++ join([for (i in 1 to (3 - len(fraction))) "0"], "")) else null,
            timezone: offset}
    }
}

fn resolve_wall(wall, zone, options) {
    let choices = zones.candidates(wall, zone);
    if (choices is error) choices
    else if (len(choices) == 0) {
        let policy = c.text(c.option(options, "nonexistent_time", "reject"));
        if (policy == "reject") c.fail("date", "nonexistent local clock")
        else if (policy == "forward") zones.from_wall(wall, zone)
        else if (policy == "backward") {
            let forward = zones.from_wall(wall, zone);
            let jump = zones.wall(forward, zone) - wall;
            zones.from_wall(wall - jump, zone, null, "reject")
        } else c.fail("scale.nonexistent_time", "expected reject, forward, or backward")
    } else if (len(choices) > 1) {
        let policy = c.text(c.option(options, "ambiguous_time", "reject"));
        if (policy == "earlier") choices[0] else if (policy == "later") choices[len(choices) - 1]
        else c.fail("date", "ambiguous local clock requires an offset or earlier/later policy")
    } else choices[0]
}

fn calendar(raw, options) {
    let names = c.properties(raw, ["year", "month", "day", "hour", "minute", "second", "millisecond", "timezone", "precision", "display_date"], "date");
    let precision = c.text(c.option(raw, "precision", if (raw.millisecond != null) "millisecond" else if (raw.second != null) "second"
        else if (raw.minute != null) "minute" else if (raw.hour != null) "hour" else if (raw.day != null) "day" else if (raw.month != null) "month" else "year"));
    let record = {*:raw, month: if (raw.month != null) raw.month else 1, day: if (raw.day != null) raw.day else 1,
        hour: if (raw.hour != null) raw.hour else 0, minute: if (raw.minute != null) raw.minute else 0,
        second: if (raw.second != null) raw.second else 0, millisecond: if (raw.millisecond != null) raw.millisecond else 0, precision: precision};
    let zone = if (raw.timezone != null) raw.timezone else c.option(options, "timezone", "UTC");
    let invalid = util.first_error([names,
        if (not c.integral(raw.year) or abs(raw.year) > 200000000) c.fail("date.year", "expected an integral supported calendar year; use cosmological coordinates for deeper time"),
        if (not all([for (key in ["month", "day", "hour", "minute", "second", "millisecond"]) c.integral(record[key])])) c.fail("date", "components must be integers"),
        if (record.month < 1 or record.month > 12 or record.day < 1 or record.day > month_days(record.year, record.month)) c.fail("date", "invalid calendar date"),
        if (record.hour < 0 or record.hour > 23 or record.minute < 0 or record.minute > 59 or record.second < 0 or record.second > 59 or
            record.millisecond < 0 or record.millisecond > 999) c.fail("date", "invalid clock time"),
        if (not contains(["year", "month", "day", "hour", "minute", "second", "millisecond"], precision)) c.fail("date.precision", "invalid precision"),
        if (not zones.valid_zone(zone)) c.fail("date.timezone", "unknown timezone or invalid offset")]);
    if (invalid is error) invalid else {
        let wall = days(record.year, record.month, record.day) * i64(86400000) +
            ((record.hour * 60 + record.minute) * 60 + record.second) * 1000 + record.millisecond;
        let resolved = if (zone is number) wall - zone * 60000
            else if (zone == "UTC" or zone == "Etc/UTC") wall
            else if (record.year < 1 or record.year > 9999) c.fail("date.timezone", "named-zone dates outside native range require a fixed offset")
            else resolve_wall(wall, zone, options);
        if (resolved is error) resolved else formatted({*:record,value:resolved,kind:"calendar",timezone:zone},options)
    }
}

pub fn normalize(value, options = {}) {
    let kind = c.text(c.option(options, "kind", "calendar"));
    if (contains(["numeric", "cosmological", "ordinal"], kind)) {
        let numeric = if (value is map) value.value else value;
        if (not util.finite_number(numeric)) c.fail("date", "expected a finite numeric coordinate")
        else if (options.unit == null and kind != "ordinal") c.fail("scale.unit", "numeric clock requires an explicit unit")
        else formatted({value:numeric,kind:kind,precision:"unit",*:(if (value.display_date!=null) {display_date:value.display_date} else {})},options)
    } else if (kind != "calendar") c.fail("scale.kind", "invalid chronology kind")
    else {
        let record = if (value is map) value else parse_iso(value);
        if (record is error) record else calendar(record, options)
    }
}

pub fn from_coordinate(value, options = {}) {
    if (c.text(c.option(options, "kind", "calendar")) != "calendar") normalize(value, options)
    else {
        let zone=c.option(options,"timezone","UTC");
        let adjustment=if (zone is number) zone*60000 else if (zone=="UTC" or zone=="Etc/UTC") 0 else {
            let details=zones.info(value,zone);
            if (details is error) details else details.offset*1000
        };
        let wall=if (adjustment is error) adjustment else offset(value,adjustment);
        let day = floor_div(i64(wall), i64(86400000));
        let record = civil(day);
        let remainder = wall - day * i64(86400000);
        let result={*:record,hour:int(remainder div 3600000),minute:int(remainder div 60000 % 60),
            second:int(remainder div 1000 % 60),millisecond:int(remainder % 1000),precision:"millisecond",
            timezone:zone,value:value,kind:"calendar"};
        if (wall is error) wall else formatted(result,options)
    }
}

// recur in local civil time so a week crossing DST retains its authored clock.
pub fn shift_calendar(record, unit, count, options = {}) {
    let month_index=record.year*12+record.month-1+(if (unit=="month") count else if (unit=="year") count*12 else 0);
    let year=floor_div(month_index,12);
    let month=month_index-year*12+1;
    let shifted=if (contains(["month","year"],unit)) {year:year,month:month,day:min(record.day,month_days(year,month))}
        else civil(days(record.year,record.month,record.day)+count*(if (unit=="week") 7 else 1));
    normalize({*:shifted,hour:record.hour,minute:record.minute,second:record.second,millisecond:record.millisecond,
        precision:record.precision,timezone:record.timezone},options)
}

// subtract before float conversion to retain adjacent exact deep-clock coordinates.
pub fn fraction(value, from, to) => if (from == to) 0.5 else float(value - from) / float(to - from)
pub fn offset(value, delta) => if (value is float) value+delta else decimal(value)+decimal(delta)
pub fn interpolate(from, to, ratio) => offset(from,decimal(to-from)*decimal(ratio))
pub fn human_year(year) => if (year <= 0) year + 1 else year
