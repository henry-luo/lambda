import calendar: lambda.chart.calendar
import zones: lambda.chart.timezone
import scale: lambda.chart.scale
import axis: lambda.chart.axis
import util: lambda.chart.util
import chart: lambda.chart.chart
import vega: lambda.chart.vega
import transform: lambda.chart.transform

fn stamp(value) float | error => calendar.timestamp(value)
fn ticks(first, final_value, unit, zone, step = 1) => calendar.ticks(stamp(first), stamp(final_value), {interval: unit, step: step}, zone)
fn labels(values, zone, pattern = "YYYY-MM-DD hh:mm") => [for (value in values) calendar.wall_time(value, zone).format(pattern)]
let ny = "America/New_York";
let spring = ticks("2024-03-10T05:00Z", "2024-03-10T09:00Z", "hour", ny);
let fall = ticks("2024-11-03T04:00Z", "2024-11-03T09:00Z", "hour", ny);
let spring_days = ticks("2024-03-09T05:00Z", "2024-03-12T04:00Z", "day", ny);
let fall_days = ticks("2024-11-02T04:00Z", "2024-11-05T05:00Z", "day", ny);
let months = ticks("2024-02-15", "2024-05-15", "month", ny);
let lord_howe = ticks("2024-10-05T13:30Z", "2024-10-05T17:00Z", "hour", "Australia/Lord_Howe");
let apia = ticks("2011-12-29T10:00Z", "2012-01-01T10:00Z", "day", "Pacific/Apia");
let santiago = ticks("2024-09-07T04:00Z", "2024-09-09T03:00Z", "day", "America/Santiago");
let mapping = scale.configured_scale([stamp("2024-03-10T05:00Z"), stamp("2024-03-10T09:00Z")], 0, 400, "temporal", {nice: false, timezone: ny});
let guide = axis.x_axis(mapping, 400, 100, {tick_count: {interval: "hour"}, format: "hh:mm", title_enabled: false, label_overlap: false}, null);
let nice = calendar.nice_extent(stamp("2024-11-03T05:50Z"), stamp("2024-11-03T06:10Z"), ny);
let midnight_nice = calendar.nice_extent(stamp("2024-03-09T12:00Z"), stamp("2024-03-12T12:00Z"), ny);
let grouped = transform.apply_transforms([{date: "2024-03-01T04:30Z"}, {date: "2024-03-01T05:30Z"}],
    [{type: "timeunit", field: "date", unit: "yearmonth", timezone: ny, as: "month"}]);
let converted = vega.convert({data: {values: [{date: "2024-03-10T06:00Z", value: 1}, {date: "2024-03-10T08:00Z", value: 2}]},
    mark: "point", encoding: {x: {field: "date", type: "temporal", scale: {timezone: ny, nice: false}, axis: {format: "hh:mm"}},
        y: {field: "value", type: "quantitative"}}});
let checks = {
    known_zone: zones.valid_zone(ny) and zones.valid_zone("Asia/Singapore"),
    unknown_zone: not zones.valid_zone("Invalid/Zone"),
    fixed_valid: zones.valid_zone(330) and not zones.valid_zone(330.5),
    spring_before: zones.info(stamp("2024-03-10T06:59:59Z"), ny).offset == -18000,
    spring_after: zones.info(stamp("2024-03-10T07:00Z"), ny).offset == -14400,
    abbreviation: zones.info(stamp("2024-03-10T07:00Z"), ny).name == "EDT",
    spring_hours: labels(spring, ny, "hh:mm") == ["00:00", "01:00", "03:00", "04:00", "05:00"],
    fall_hours: labels(fall, ny, "hh:mm") == ["00:00", "01:00", "01:00", "02:00", "03:00", "04:00"],
    distinct_fold: fall[2] - fall[1] == 3600000,
    fold_candidates: zones.candidates(stamp("2024-11-03T01:30Z"), ny) == [stamp("2024-11-03T05:30Z"), stamp("2024-11-03T06:30Z")],
    fold_earlier: zones.from_wall(stamp("2024-11-03T01:30Z"), ny) == stamp("2024-11-03T05:30Z"),
    fold_preferred: zones.from_wall(stamp("2024-11-03T01:30Z"), ny, stamp("2024-11-03T06:45Z")) == stamp("2024-11-03T06:30Z"),
    gap_candidates: zones.candidates(stamp("2024-03-10T02:30Z"), ny) == [],
    gap_forward: zones.from_wall(stamp("2024-03-10T02:30Z"), ny) == stamp("2024-03-10T07:30Z"),
    gap_reject: zones.from_wall(stamp("2024-03-10T02:30Z"), ny, null, "reject") == null,
    spring_dates: labels(spring_days, ny, "MM-DD hh:mm") == ["03-09 00:00", "03-10 00:00", "03-11 00:00", "03-12 00:00"],
    short_day: spring_days[2] - spring_days[1] == 23 * 3600000,
    long_day: fall_days[2] - fall_days[1] == 25 * 3600000,
    month_alignment: labels(months, ny) == ["2024-03-01 00:00", "2024-04-01 00:00", "2024-05-01 00:00"],
    month_offset: months[1] - months[0] == 31 * 86400000 - 3600000,
    half_hour_dst: labels(lord_howe, "Australia/Lord_Howe", "hh:mm") == ["00:00", "01:00", "03:00", "04:00"],
    midnight_gap: labels(santiago, "America/Santiago", "MM-DD hh:mm") == ["09-07 00:00", "09-08 01:00", "09-09 00:00"],
    skipped_date: labels(apia, "Pacific/Apia", "MM-DD hh:mm") == ["12-29 00:00", "12-31 00:00", "01-01 00:00", "01-02 00:00"],
    quarter_hour: calendar.wall_time(stamp("2024-01-01"), "Asia/Kathmandu").format("hh:mm") == "05:45",
    historical_seconds: zones.info(stamp("1880-01-01"), ny).offset == -17762,
    alias: zones.info(stamp("2024-01-01"), "US/Eastern") == zones.info(stamp("2024-01-01"), ny),
    future_winter: zones.info(stamp("2100-01-01"), ny).offset == -18000,
    future_summer: zones.info(stamp("2100-07-01"), ny).offset == -14400,
    southern_future: zones.info(stamp("2100-01-01"), "Australia/Sydney").offset == 39600,
    negative_dst: zones.info(stamp("2100-01-01"), "Europe/Dublin").offset == 0 and zones.info(stamp("2100-07-01"), "Europe/Dublin").offset == 3600,
    latest_rules: zones.info(stamp("2100-01-01"), "America/Winnipeg").offset == -18000 and zones.info(stamp("2100-07-01"), "America/Winnipeg").offset == -18000,
    future_transition: labels(ticks("2100-03-14T05:00Z", "2100-03-14T09:00Z", "hour", ny), ny, "hh:mm") == ["00:00", "01:00", "03:00", "04:00", "05:00"],
    aligned_steps: labels(ticks("2024-11-03T04:00Z", "2024-11-03T13:00Z", "hour", ny, 3), ny, "hh:mm") == ["00:00", "03:00", "06:00"],
    scale_ticks: scale.scale_ticks(mapping, {interval: "hour"}) == spring,
    guide_labels: guide[1][1][0] == "00:00" and guide[3][1][0] == "03:00",
    format_label: util.format_value("2024-03-10T07:00Z", "YYYY-MM-DD hh:mm", "temporal", ny) == "2024-03-10 03:00",
    format_offset: util.format_value("2024-03-10T07:00Z", "hh:mm Z ZZ", "temporal", ny) == "03:00 -04:00 -0400",
    format_historical: util.format_value("1880-01-01", "Z", "temporal", ny) == "-04:56:02",
    nice_fold: nice[0] <= stamp("2024-11-03T05:50Z") and nice[1] >= stamp("2024-11-03T06:10Z") and nice[0] < nice[1],
    nice_day: midnight_nice[0] <= stamp("2024-03-09T12:00Z") and midnight_nice[1] >= stamp("2024-03-12T12:00Z"),
    group_before_midnight: grouped[0].month == stamp("2024-02-01T05:00Z"),
    group_after_midnight: grouped[1].month == stamp("2024-03-01T05:00Z"),
    utc_override: calendar.time_unit("2024-03-01T04:30Z", "utcyearmonth", ny) == stamp("2024-03-01"),
    vega_render: chart.render_spec(converted) is element,
    reversed: calendar.ticks(spring[4], spring[0], {interval: "hour"}, ny) == reverse(spring),
    bad_tick_zone: calendar.ticks(0, 0, 5, "Invalid/Zone") is error,
    bad_time_unit: calendar.time_unit("2024-01-01", "month", "Invalid/Zone") is error,
    bad_label_zone: util.format_value("2024-01-01", "YYYY", "temporal", "Invalid/Zone") is error
};
[for (label, passed in checks where passed != true) string(label)]
