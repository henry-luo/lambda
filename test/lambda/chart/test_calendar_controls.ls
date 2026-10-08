import calendar: lambda.chart.calendar
import scale: lambda.chart.scale
import axis: lambda.chart.axis
import chart: lambda.chart.chart
import transform: lambda.chart.transform
import vega: lambda.chart.vega

let start = calendar.timestamp("2024-01-15")
let finish = calendar.timestamp("2024-05-20")
let months = calendar.ticks(start, finish, {interval: "month"})
let days = calendar.ticks(calendar.timestamp("2024-02-28T12:00Z"), calendar.timestamp("2024-03-02T12:00Z"), {interval: "day"})
let zoned = calendar.ticks(calendar.timestamp("2024-01-15"), calendar.timestamp("2024-03-15"), {interval: "month"}, 330)
let weeks = calendar.ticks(calendar.timestamp("2023-12-27"), calendar.timestamp("2024-01-10"), {interval: "week"})
let years = calendar.ticks(calendar.timestamp("2019-01-02"), calendar.timestamp("2031-01-02"), {interval: "year", step: 5})
let mapping = scale.configured_scale(["2024-01-15", "2024-05-20"], 0, 100, "temporal", {nice: false})
let zoned_scale = scale.configured_scale(["2024-01-15", "2024-03-15"], 0, 100, "temporal", {nice: false, timezone: 330})
let zoned_axis = axis.x_axis(zoned_scale, 100, 100,
    {tick_count: {interval: "month"}, format: "YYYY-MM-DD hh:mm", title_enabled: false}, null)
let records = [{date: "2024-01-01T01:00Z", value: 1}, {date: "2024-01-31T23:00Z", value: 2},
    {date: "2024-02-10T10:00Z", value: 3}]
let prepared = transform.prepare_encoding(records, {
    x: {field: "date", dtype: "temporal", time_unit: "yearmonth"},
    y: {field: "value", dtype: "quantitative", aggregate: "sum"}})
let grouped = prepared.data
let stepped = transform.apply_transforms(records, [{type: "timeunit", field: "date", unit: "yearmonth", as: "month"}])
let converted = vega.convert({data: {values: records}, mark: "bar", encoding: {
    x: {field: "date", type: "ordinal", timeUnit: "utcmonth", axis: {title: null}},
    y: {field: "value", type: "quantitative", aggregate: "sum", axis: null}}})
let image = chart.render_spec(converted)
let utc_step = vega.convert({data: {values: records}, mark: "point",
    transform: [{timeUnit: "utcyearmonth", field: "date", as: "month"}], encoding: {x: {field: "month", type: "temporal"}}})
let unit_zone = calendar.time_unit("2024-01-31T23:00Z", {unit: "yearmonth", timezone: 120})
let utc_unit = calendar.time_unit("2024-01-31T23:00Z", {unit: "utcyearmonth", timezone: 120})
let nice = calendar.nice_extent(calendar.timestamp("2024-01-15"), calendar.timestamp("2024-09-15"))
let checks = {
    months: [for (value in months) datetime(i64(value)).format("YYYY-MM-DD")] == ["2024-02-01", "2024-03-01", "2024-04-01", "2024-05-01"],
    automatic_months: scale.scale_ticks(mapping, 4) == months,
    leap_day: [for (value in days) datetime(i64(value)).format("MM-DD")] == ["02-29", "03-01", "03-02"],
    month_lengths: months[1] - months[0] == 29 * 86400000 and months[2] - months[1] == 31 * 86400000,
    week_alignment: len(weeks) == 2 and calendar.wall_time(weeks[0]).weekday == 0,
    multi_year: [for (value in years) calendar.wall_time(value).year] == [2020, 2025, 2030],
    reversed: calendar.ticks(finish, start, {interval: "month"}) == reverse(months),
    no_ticks: calendar.ticks(start, finish, 0) == [],
    same_instant: calendar.timestamp("2024-01-01T08:00+08:00") == calendar.timestamp("2024-01-01T00:00Z"),
    fixed_offset: calendar.wall_time(zoned[0], 330).format("YYYY-MM-DD hh:mm") == "2024-02-01 00:00",
    offset_label: zoned_axis[1][1][0] == "2024-02-01 00:00",
    year_grouping: len(grouped) == 2 and grouped[0].value_sum == 3 and grouped[1].value_sum == 3,
    group_instants: grouped[0].date_time_x == calendar.timestamp("2024-01-01"),
    original_field: stepped[1].date == records[1].date and stepped[1].month == calendar.timestamp("2024-01-01"),
    cyclic_month: calendar.wall_time(calendar.time_unit("2024-02-29", "month")).format("YYYY-MM-DD") == "2012-02-01",
    cyclic_day: calendar.wall_time(calendar.time_unit("2024-01-07", "day")).format("YYYY-MM-DD") == "2012-01-01",
    cyclic_hour: calendar.wall_time(calendar.time_unit("2024-01-07T15:45Z", "hours")).format("YYYY-MM-DD hh:mm") == "2012-01-01 15:00",
    offset_group: calendar.wall_time(unit_zone, 120).format("YYYY-MM-DD") == "2024-02-01",
    utc_group: utc_unit == calendar.timestamp("2024-01-01"),
    nice_boundaries: calendar.wall_time(nice[0]).day == 1 and calendar.wall_time(nice[1]).day == 1,
    vega_unit: converted.encoding.x.time_unit == "utcmonth" and image[1][1][1][1][0] == "Jan",
    vega_transform: transform.apply_transforms(records, utc_step.transform)[1].month == calendar.timestamp("2024-01-01"),
    null_unit: calendar.time_unit(null, "year") == null,
    bad_unit: transform.apply_transforms(records, [{type: "timeunit", field: "date", unit: "bad", as: "unit"}]) is error,
    bad_interval: calendar.ticks(start, finish, {interval: "month", step: 0}) is error,
    bad_timezone: scale.configured_scale([start, finish], 0, 100, "temporal", {timezone: "Asia/Singapore"}) is error
};
[for (label, passed in checks where passed != true) string(label)]
