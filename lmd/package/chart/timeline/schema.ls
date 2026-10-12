// canonical elements and maps share one validation path (S2.4.3v3, S7.4.1).
import c: .common
import dates: .date
import util: lambda.chart.util
import parse: lambda.chart.parse
import transform: lambda.chart.transform
import sizing: lambda.chart.sizing
import media: .media
import time_map: .time_map
import annotation: .annotation
import zones: lambda.chart.timezone
import locale: .locale
import scales_engine: lambda.chart.scale

let children = ["title", "description", "event", "era", "group", "data", "encoding", "transform", "source", "scale", "axis",
    "window", "coordinate", "navigator", "cards", "story", "markers", "interaction", "edit", "cluster", "search", "slideshow",
    "media_options", "toolbar", "pending", "empty", "loading", "error", "annotation", "series", "messages", "export"]
let repeated = {event:"events", era:"eras", group:"groups", scale:"scales", axis:"axes", annotation:"annotations", series:"series"}
let slots = ["title", "description", "pending", "empty", "loading", "error"]
let root_keys = ["id", "version", "view", "spacing", "orientation", "placement", "order", "width", "height",
    "min_width", "max_width", "min_height", "max_height", "padding", "gap", "time_scale", "value", "default_value",
    "theme", "parts", "locale", "direction", "motion", "label", "description", "title", "events", "eras", "groups",
    "data", "encoding", "transform", "source", "scales", "axes", "window", "coordinate", "navigator", "cards", "story",
    "markers", "interaction", "edit", "cluster", "search", "slideshow", "media_options", "toolbar", "pending", "empty",
    "loading", "error", "annotations", "series", "messages", "export"]
let event_keys = ["id", "kind", "at", "from", "to", "open_start", "open_end", "title", "subtitle", "display_date", "content",
    "group", "subgroup", "tags", "placement", "align", "priority", "color", "marker", "connector", "appearance", "link", "target",
    "visible", "selectable", "editable", "uncertainty", "metadata", "media", "tooltip", "children", "loading", "unstable_id", "duration", "size", "opacity"]
let definitions = {
    scale:["id", "kind", "domain", "unit", "origin", "timezone", "calendar", "week_start", "numbering", "era_labels", "locale", "ambiguous_time", "nonexistent_time", "hidden_intervals", "interval_limit", "format", "formatter",
        "range", "domain_min", "domain_max", "base", "exponent", "nice", "zero", "clamp", "round", "reverse", "padding", "padding_inner", "padding_outer"],
    axis:["id", "scale", "position", "major", "minor", "grid", "week_numbers", "tick_unit", "tick_step", "tick_count", "format", "formatter", "rotation", "title", "label_bounds", "collision"],
    window:["from", "to", "min", "max", "min_span", "max_span", "fit", "anchor", "follow"],
    coordinate:["kind", "orientation", "reverse", "lane_direction", "levels", "turn_radius", "start_location", "end_location", "turns", "start_angle", "end_angle", "inner_radius", "outer_radius", "lane_width", "points", "path", "smoothing", "precision", "point", "lane_offsets", "label_angle", "self_intersection"],
    navigator:["enabled", "visible", "position", "height", "axis", "labels", "thumbnails", "groups", "handles", "appearance"],
    cards:["visibility", "width", "height", "min_width", "max_width", "min_height", "max_height", "date_align", "title_align", "body_align", "borderless", "text_overlay", "heading_level", "variant", "read_more", "markers", "scrollbars", "breakpoints", "overflow", "stack", "gap", "reverse"],
    story:["title_slide", "active_id", "start_at_end", "panel_order", "order", "bookmarks", "media_behavior", "metadata"],
    markers:["visible", "shape", "size", "color", "connector", "label_placement", "renderer"],
    interaction:["enabled", "click_to_use", "pan", "zoom", "wheel_modifier", "wheel", "pinch", "horizontal_scroll", "vertical_scroll", "keyboard", "activate", "hover", "focus_on_load", "auto_scroll", "selectable", "multiselect", "contiguous", "per_group", "linked"],
    edit:["enabled", "add", "update", "remove", "move_time", "move_group", "resize", "subgroup", "group", "drag_drop", "override_items", "snap_unit", "snap_step", "snap", "validate", "confirm"],
    cluster:["enabled", "min_count", "distance", "by", "title", "renderer", "badge", "max_expansion", "expand_on"],
    search:["enabled", "fields", "query", "case_sensitive", "diacritics", "mode", "highlight", "count", "no_results", "tags", "groups", "from", "to", "predicate", "controls"],
    slideshow:["enabled", "autoplay", "duration", "progress", "overall_progress", "transition", "transition_duration", "delay", "easing", "repeat", "direction", "media_end", "media_timeout"],
    media_options:["loading", "prefetch", "concurrency", "timeout", "retry", "active_playback", "pause_on_deactivate", "aspect_ratio", "fullscreen", "lightbox", "providers", "fallback", "error"],
    toolbar:["enabled", "visible", "buttons", "position", "sticky", "labels", "icons", "slots", "search_width"],
    source:["format", "url", "name", "mapping", "refresh", "revision"],
    export:["snapshot", "resources", "expanded", "pagination", "unsupported", "scope", "format", "filename", "background", "scale",
        "fields", "headings", "date_formatter", "sheet_name", "caption", "time_ms", "table", "clusters", "quality", "device_scale", "base_url", "media_frames"],
    media:["id", "kind", "src", "mime", "provider", "poster", "thumbnail", "alt", "caption", "credit", "link", "target", "width", "height", "fit", "align", "controls", "muted", "autoplay", "loop", "start", "end", "content", "fallback"],
    group:["id", "title", "parent", "order", "visible", "collapsed", "editable", "height", "min_height", "max_height", "height_mode", "appearance", "subgroups", "groups"],
    subgroup:["id", "title", "order", "visible", "stack", "height", "min_height", "max_height", "height_mode", "appearance"],
    era:["id", "from", "to", "title", "group", "color", "placement", "appearance", "open_start", "open_end"],
    annotation:["id", "kind", "at", "from", "to", "title", "group", "color", "appearance", "editable", "content"],
    series:["id", "time_scale", "value_scale", "visible", "legend", "data", "mark", "encoding", "config", "transform"]
}
let bool_keys = ["visible", "enabled", "open_start", "open_end", "loading", "selectable", "unstable_id", "reverse", "major", "minor", "grid", "week_numbers",
    "borderless", "text_overlay", "read_more", "markers", "scrollbars", "stack", "start_at_end", "bookmarks", "click_to_use", "pan", "zoom", "wheel", "pinch",
    "horizontal_scroll", "vertical_scroll", "keyboard", "activate", "hover", "focus_on_load", "auto_scroll", "multiselect", "contiguous", "per_group", "add", "update",
    "remove", "move_time", "move_group", "resize", "subgroup", "group", "drag_drop", "override_items", "confirm", "badge", "case_sensitive", "highlight", "count",
    "autoplay", "progress", "overall_progress", "media_end", "active_playback", "pause_on_deactivate", "fullscreen", "lightbox", "sticky", "controls", "muted", "loop", "collapsed"]

let choices={axis:{position:["start","end","both","none"],collision:["omit","rotate","wrap","none"]},
    navigator:{position:["top","bottom"]},
    cards:{visibility:["all","active","viewport"],date_align:["start","center","end"],title_align:["start","center","end"],body_align:["start","center","end"],
        variant:["compact","detailed"],overflow:["stack","cluster","scroll","expand","visible"]},
    coordinate:{lane_direction:["start","end","both"],label_angle:["tangent","upright"],self_intersection:["allow","error"]},
    search:{mode:["filter","highlight"],diacritics:["sensitive","ignore"]},
    interaction:{wheel_modifier:["none","ctrl","shift","alt","meta"]},
    cluster:{expand_on:["click","dblclick"]},
    slideshow:{transition:["none","fade","slide","reveal"],direction:["normal","reverse","alternate","reverse_alternate"]},
    media_options:{loading:["lazy","eager","active"],fallback:["error","link","poster","omit"]},
    toolbar:{position:["top","bottom"]},group:{height_mode:["auto","fixed","fit_items"]},subgroup:{height_mode:["auto","fixed","fit_items"]}}

fn configuration_rules(raw, kind) {
    let positive_keys=if (kind=="cards") ["width","height","min_width","max_width","min_height","max_height"]
        else if (kind=="axis") ["tick_count","tick_step"] else if (kind=="media_options") ["timeout","concurrency","aspect_ratio"]
        else if (kind=="cluster") ["min_count","distance","max_expansion"] else if (kind=="navigator") ["height"]
        else if (kind=="source") ["refresh"] else if (kind=="markers") ["size"]
        else if (contains(["group","subgroup"],kind)) ["height","min_height","max_height"] else [];
    util.first_error([
        for (key,values in c.attrs(choices[kind]) where raw[key]!=null)
            if (not (kind=="coordinate" and string(key)=="label_angle" and util.finite_number(raw[key])) and not c.enum(raw[key],values)) c.fail(kind ++ "." ++ string(key),"invalid value"),
        for (key in positive_keys where raw[key]!=null) if (not c.positive(raw[key])) c.fail(kind ++ "." ++ key,"expected positive finite pixels/count"),
        if (kind=="cards" and raw.heading_level!=null and (not c.integral(raw.heading_level) or raw.heading_level<1 or raw.heading_level>6)) c.fail("cards.heading_level","expected 1 to 6"),
        if (kind=="cards" and raw.breakpoints!=null and not (raw.breakpoints is array)) c.fail("cards.breakpoints","expected breakpoint rules"),
        if (kind=="story" and raw.panel_order!=null and (not (raw.panel_order is array) or
            len(unique(raw.panel_order |> c.text(~)))!=len(raw.panel_order) or
            not all([for (part in c.items(raw.panel_order)) c.enum(part,["text","media","children"])])))
            c.fail("story.panel_order","expected unique text, media, or children sections"),
        for (rule in (if (kind=="cards" and raw.breakpoints is array) raw.breakpoints else []))
            util.first_error([c.properties(rule,["min_width","max_width","orientation","placement","cards"],"cards.breakpoints"),
                if (rule.orientation!=null and not c.enum(rule.orientation,["horizontal","vertical"])) c.fail("cards.breakpoints.orientation","invalid orientation"),
                if (rule.placement!=null and not c.enum(rule.placement,["start","end","alternate","auto"])) c.fail("cards.breakpoints.placement","invalid placement"),
                for (key in ["min_width","max_width"] where rule[key]!=null) if (not c.positive(rule[key])) c.fail("cards.breakpoints." ++ key,"expected positive width"),
                if (rule.min_width!=null and rule.max_width!=null and rule.min_width>rule.max_width) c.fail("cards.breakpoints","contradictory bounds"),
                if (rule.cards!=null) config(rule.cards,"cards")]),
        if (kind=="media_options" and raw.retry!=null and (not c.integral(raw.retry) or raw.retry<0)) c.fail("media_options.retry","expected a nonnegative retry count"),
        if (kind=="media_options" and raw.prefetch!=null and (not c.integral(raw.prefetch) or raw.prefetch<0)) c.fail("media_options.prefetch","expected a nonnegative event count"),
        for (key in (if (kind=="cluster") ["min_count","max_expansion"] else []) where raw[key]!=null)
            if (not c.integral(raw[key])) c.fail("cluster." ++ key,"expected an integral event count"),
        if (kind=="slideshow" and raw.delay!=null and (not util.finite_number(raw.delay) or raw.delay<0)) c.fail("slideshow.delay","expected nonnegative milliseconds"),
        if (kind=="slideshow" and raw.transition_duration!=null and (not util.finite_number(raw.transition_duration) or raw.transition_duration<0)) c.fail("slideshow.transition_duration","expected nonnegative milliseconds"),
        if (kind=="slideshow" and raw.media_timeout!=null and not c.positive(raw.media_timeout)) c.fail("slideshow.media_timeout","expected positive milliseconds"),
        if (kind=="scale" and raw.interval_limit!=null and (not c.integral(raw.interval_limit) or raw.interval_limit<1)) c.fail("scale.interval_limit","expected a positive integral occurrence limit"),
        if (kind=="scale" and raw.week_start!=null and (not c.integral(raw.week_start) or raw.week_start<0 or raw.week_start>6)) c.fail("scale.week_start","expected Sunday=0 through Saturday=6"),
        if (kind=="axis" and raw.rotation!=null and not util.finite_number(raw.rotation)) c.fail("axis.rotation","expected finite degrees"),
        if (kind=="axis" and raw.tick_step!=null and not c.integral(raw.tick_step)) c.fail("axis.tick_step","expected an integral tick step"),
        if (kind=="axis" and raw.tick_unit!=null and not c.enum(raw.tick_unit,["millisecond","second","minute","hour","day","week","month","year"])) c.fail("axis.tick_unit","unknown calendar tick unit"),
        for (dimension in (if (kind=="cards") ["width","height"] else []))
            if (raw["min_" ++ dimension]!=null and raw["max_" ++ dimension]!=null and raw["min_" ++ dimension]>raw["max_" ++ dimension])
                c.fail("cards." ++ dimension,"contradictory size constraints"),
        if (kind=="search" and raw.fields!=null and (not (raw.fields is array) or not all([for (field in c.items(raw.fields)) field is string or field is symbol])))
            c.fail("search.fields","expected field names"),
        if (kind=="search" and raw.predicate!=null and not (raw.predicate is fn)) c.fail("search.predicate","expected a pure function"),
        if (kind=="search" and raw.controls!=null and not (raw.controls is bool or raw.controls is array and
            all([for (field in raw.controls) c.enum(field,["groups","tags","from","to"])])))
            c.fail("search.controls","expected a boolean or an array of groups, tags, from, to"),
        if (kind=="search" and raw.controls is array and len(unique(raw.controls |> c.text(~)))!=len(raw.controls))
            c.fail("search.controls","duplicate filter field"),
        for (key in (if (kind=="edit") ["snap","validate"] else []) where raw[key]!=null)
            if (not (raw[key] is fn)) c.fail("edit." ++ key,"expected a pure function"),
        if (kind=="edit" and raw.snap_step!=null and not c.positive(raw.snap_step)) c.fail("edit.snap_step","expected a positive finite step"),
        if (kind=="edit" and raw.snap_unit!=null and not c.enum(raw.snap_unit,["millisecond","second","minute","hour","day","week","month","year","unit"])) c.fail("edit.snap_unit","unknown snap unit"),
        if (kind=="edit" and raw.snap_unit!=null and raw.snap_unit!="unit" and raw.snap_step!=null and not c.integral(raw.snap_step)) c.fail("edit.snap_step","calendar steps must be integral"),
        if (kind=="scale" and raw.formatter!=null and not (raw.formatter is fn)) c.fail("scale.formatter","expected a pure formatter"),
        if (kind=="axis" and raw.formatter!=null and not (raw.formatter is fn)) c.fail("axis.formatter","expected a pure formatter"),
        if (kind=="axis" and raw.format!=null and not (raw.format is string)) c.fail("axis.format","expected a formatting pattern"),
        if (kind=="export") export_rules(raw)])
}

fn export_rules(raw) => util.first_error([
    for (key,values in {scope:["full","current","window","selected"],resources:["recorded","local","external"],unsupported:["error","text","omit"],
        format:["html","svg","png","jpeg","jpg","pdf","json","csv","xlsx"]} where raw[string(key)]!=null)
        if (not c.enum(raw[string(key)],values)) c.fail("export." ++ string(key),"invalid value"),
    for (key in ["scale","device_scale"] where raw[key]!=null) if (not c.positive(raw[key])) c.fail("export." ++ key,"expected a positive finite scale"),
    for (key in ["table","clusters","expanded"] where raw[key]!=null) if (not (raw[key] is bool)) c.fail("export." ++ key,"expected a boolean"),
    for (key in ["snapshot","headings","media_frames"] where raw[key]!=null) if (not (raw[key] is map)) c.fail("export." ++ key,"expected a map"),
    for (key in ["filename","base_url","sheet_name"] where raw[key]!=null) if (not (raw[key] is string) or raw[key]=="") c.fail("export." ++ key,"expected nonempty text"),
    if (raw.fields!=null and (not (raw.fields is array) or not all([for (field in c.items(raw.fields)) field is string or field is symbol]))) c.fail("export.fields","expected an ordered field array"),
    if (raw.date_formatter!=null and not (raw.date_formatter is fn)) c.fail("export.date_formatter","expected a pure formatter"),
    if (raw.time_ms!=null and (not util.finite_number(raw.time_ms) or raw.time_ms<0)) c.fail("export.time_ms","expected nonnegative finite milliseconds"),
    if (raw.quality!=null and (not c.integral(raw.quality) or raw.quality<1 or raw.quality>100)) c.fail("export.quality","expected 1 to 100"),
    if (raw.pagination!=null and not (raw.pagination is bool or raw.pagination is map)) c.fail("export.pagination","expected a boolean or pagination map")])

fn booleans(value, path) => util.first_error([for (key in bool_keys where c.has(value, key) and not (value[key] is bool) and
    not (key == "loading" and path == "media_options") and not (key == "controls" and path == "search") and
    not (contains(["group", "subgroup"], key) and path != "edit")) c.fail(path ++ "." ++ key, "expected a boolean")])

pub fn config(value, kind) {
    if (value == null) {} else if (value is bool and contains(["navigator", "toolbar", "cluster", "slideshow", "interaction", "edit"], kind)) {enabled:value, visible:value}
    else if (not (value is map or value is element)) c.fail(kind, "expected a configuration map")
    else {
        let parsed=if (kind=="series" and value is element) series_node(value) else c.attrs(value);
        let raw=c.attrs(parsed);
        let failure = util.first_error([parsed,c.properties(raw, definitions[kind], kind, true), booleans(raw, kind),configuration_rules(raw,kind)]);
        if (failure is error) failure else raw
    }
}

fn series_node(value) {
    let nodes=content(value);
    let allowed=["data","mark","encoding","config","transform"];
    let failure=util.first_error([for (node in nodes) if (not (node is element) or not contains(allowed,c.tag(node)))
        c.fail("series.children","expected native data, mark, encoding, config, or transform"),
        for (kind in allowed,let matching=nodes |: c.tag(~)==kind)
            if (len(matching)>1 or len(matching)>0 and c.has(value,kind)) c.fail("series." ++ kind,"duplicate singleton")]);
    if (failure is error) failure else {*:c.attrs(value),*:map([for (kind in allowed,
        let matching=nodes |: c.tag(~)==kind where len(matching)>0)
        (kind,if (kind=="mark") parse.parse_mark(matching[0]) else if (kind=="config") c.attrs(matching[0]) else node_value(matching[0],kind))])}
}

fn node_value(node, kind) {
    if (contains(slots, kind)) {content:content(node), *:c.attrs(node)}
    else if (kind == "data") if (node.values != null) node.values else content(node)
    else if (kind == "encoding") {*:c.attrs(node), *:map([for (child in content(node)) (string(name(child)), c.attrs(child))])}
    else if (kind == "transform") node
    else node
}

fn root(raw) {
    if (raw is map) raw
    else if (not (raw is element) or name(raw) != 'timeline.chart') c.fail("chart", "expected a map or timeline.chart element")
    else {
        let nodes = content(raw);
        let failure = util.first_error([for (node in nodes)
            if (not (node is element) or not contains(children, c.tag(node))) c.fail("chart.children", "unknown timeline child"),
            for (kind in children where repeated[kind] == null,
                let matching = nodes |: c.tag(~) == kind)
                if (len(matching) > 1 or len(matching) > 0 and c.has(raw, kind)) c.fail(kind, "duplicate singleton")]);
        if (failure is error) failure else {*:c.attrs(raw), *:map([for (kind in children,
            let matches = nodes |: c.tag(~) == kind where len(matches) > 0)
            (if (repeated[kind] != null) repeated[kind] else kind,
                if (repeated[kind] != null) [*c.items(raw[repeated[kind]]), *matches] else node_value(matches[0], kind))])}
    }
}

pub fn authored(raw) => root(raw)

// strip derived layout fields before an immutable source edit is validated again.
pub fn source_record(spec) => {*:map([for (key,value in c.attrs(spec) where contains(root_keys,string(key))) (key,value)]),
    events:[for (item in spec.events) event_record(item)],groups:[for (item in spec.groups) group_record(item)],
    eras:[for (item in spec.eras) record_fields(item,"era")],annotations:[for (item in spec.annotations) record_fields(item,"annotation")]}
fn record_fields(item, kind) => {*:map([for (key,value in c.attrs(item) where contains(definitions[kind],string(key))) (key,value)]),*:c.attributes(item)}
pub fn event_record(item) => {*:map([for (key,value in c.attrs(item) where contains(event_keys,string(key)) and
    (string(key)!="display_date" or item.display_date_authored!=false)) (key,value)]),*:c.attributes(item)}
pub fn group_record(item) => map([for (key,value in c.attrs(item) where contains(definitions.group,string(key)) and string(key)!="groups") (key,value)])

pub fn permissions(value, path) => if (value==null or value is bool) null
    else if (not (value is map)) c.fail(path,"expected a boolean or permission map")
    else util.first_error([c.properties(value,["add","update","remove","move_time","move_group","resize","group","subgroup"],path),
        for (key,permission in value) if (not (permission is bool)) c.fail(path ++ "." ++ string(key),"expected a boolean")])

fn event_raw(value) {
    if (not (value is element)) value else {
        let nodes = content(value);
        let allowed = ["content", "media", "marker", "tooltip", "children"];
        let failure = util.first_error([for (node in nodes)
            if (not (node is element) or not contains(allowed, c.tag(node))) c.fail("event.children", "unknown event slot"),
            for (kind in allowed where kind != "media", let matches = nodes |: c.tag(~) == kind)
                if (len(matches) > 1 or len(matches) > 0 and c.has(value, kind)) c.fail("event." ++ kind, "duplicate slot")]);
        if (failure is error) failure else {*:c.attrs(value), *:map([for (kind in allowed,
            let matches = nodes |: c.tag(~) == kind where len(matches) > 0)
            (kind, if (kind == "media") [for (node in matches) {*:c.attrs(node), content:content(node)}]
                else content(matches[0]))])}
    }
}

fn event(value, index, spec) {
    let raw = event_raw(value);
    let key = if (raw.id != null) c.id(raw.id) else "event-" ++ string(index);
    let first = if (raw.at != null) raw.at else raw.from;
    let start = if (first != null) dates.normalize(first, spec.scale) else null;
    let end = if (raw.to != null) dates.normalize(raw.to, spec.scale) else start;
    let kind = c.text(c.option(raw, "kind", if (raw.from != null or raw.to != null) "range" else "point"));
    let placement = c.text(c.option(raw, "placement", spec.placement));
    let media_records = [for (record in c.items(raw.media), let parsed=config(record, "media"))
        if (parsed is error) parsed else media.validate(parsed)];
    let uncertainty=annotation.uncertainty(raw.uncertainty,start,end,kind,spec.scale);
    let failure = util.first_error([raw, key, start, end, uncertainty,*media_records, c.properties(raw, event_keys, "event", true), booleans(raw, "event"),permissions(raw.editable,"event.editable"),
        if (not (raw is map or raw is element)) c.fail("events", "events must be maps or timeline.event elements"),
        if (raw.at != null and (raw.from != null or raw.to != null)) c.fail("event.at", "cannot combine at with from/to"),
        if (not contains(["point", "range", "background"], kind)) c.fail("event.kind", "invalid event kind"),
        if (spec.spacing == "time" and (start == null and raw.open_start != true or end == null and raw.open_end != true)) c.fail("event.date", "time spacing requires dates or explicit open ends"),
        if (kind != "point" and (raw.from == null and raw.open_start != true or raw.to == null and raw.open_end != true)) c.fail("event.range", "range endpoints require from/to or open flags"),
        if (start != null and end != null and end.value < start.value) c.fail("event.to", "end precedes start"),
        if (raw.placement != null and not c.enum(raw.placement, ["start", "end", "alternate", "auto"])) c.fail("event.placement", "invalid placement"),
        if (raw.tags != null and not (raw.tags is array)) c.fail("event.tags", "expected an array"),
        if (len(unique([for (record in media_records where record.id!=null) c.text(record.id)]))!=len([for (record in media_records where record.id!=null) record.id]))
            c.fail("event.media.id","duplicate media ID within an event"),
        if (raw.size!=null and not c.positive(raw.size)) c.fail("event.size","expected positive marker area"),
        if (raw.opacity!=null and (not util.finite_number(raw.opacity) or raw.opacity<0 or raw.opacity>1)) c.fail("event.opacity","expected a ratio from zero to one")]);
    if (failure is error) failure else {*:raw, id:key, kind:kind, start:start, end:end, index:index, media:media_records,uncertainty:uncertainty,
        group:if (raw.group != null) c.id(raw.group) else null,
        subgroup:if (raw.subgroup != null) c.id(raw.subgroup) else null,
        unstable_id:raw.id == null or raw.unstable_id==true, visible:c.option(raw, "visible", true), selectable:c.option(raw, "selectable", true),
        placement:if (placement == "alternate") (if (index % 2 == 0) "start" else "end") else placement,
        display_date_authored:raw.display_date!=null,
        display_date:if (raw.display_date != null) raw.display_date else if (start != null) start.label else null}
}

// Narrative introductions have card data but no position on the chronology axis.
fn introduction(value, spec) {
    if (value==null or value==false) null else if (not (value==true or value is map or value is element))
        c.fail("story.title_slide","expected true, false, or an event record")
    else {
        let raw=if (value==true) {title:if (spec.title is map) spec.title.content else spec.title,
            content:if (spec.description is map) spec.description.content else spec.description} else event_raw(value);
        if (raw is error) raw else event({id:"__title__",*:raw,selectable:false,editable:false},-1,{*:spec,spacing:"sequence"})
    }
}

fn subgroup(value) {
    let raw=config(value,"subgroup");
    let key=if (raw is error) raw else c.id(raw.id);
    if (key is error) key else {*:raw,id:key}
}
fn flatten_groups(values, parent = null, depth = 0) {
    if (depth > 64) [c.fail("groups", "nested groups exceed maximum depth")] else [for (value in values,
        let raw = config(value, "group"), let key = c.id(raw.id),
        let subs=[for (item in c.items(raw.subgroups)) subgroup(item)],
        let failure=util.first_error([raw,key,*subs,unique_ids(subs,"subgroups"),permissions(raw.editable,"group.editable")]))
        for (record in [if (failure is error) failure else {*:raw, id:key,
            parent:if (raw.parent != null) c.id(raw.parent) else parent,subgroups:subs},
            *flatten_groups(if (value is element) content(value) else c.items(raw.groups), key, depth + 1)]) record]
}
fn ancestry(group, groups, seen = []) {
    if (group.parent == null) null
    else if (contains(seen, group.parent) or group.id == group.parent) c.fail("groups.parent", "cyclic group hierarchy")
    else {
        let parents = groups |: ~.id == group.parent;
        if (len(parents) != 1) c.fail("groups.parent", "unknown parent") else ancestry(parents[0], groups, [*seen, group.id])
    }
}
fn unique_ids(values, path) {
    let ids = values |> ~.id;
    if (len(unique(ids)) != len(ids)) c.fail(path ++ ".id", "duplicate normalized ID") else null
}
fn encoded(records, encoding) => if (encoding == null) records else [for (row in records)
    map([for (channel, definition in encoding) (channel, if (definition is fn) definition(row)
        else parse.channel_value(definition, row))])]

fn chronology(kind) => contains(["calendar","numeric","cosmological","ordinal"],c.text(kind)) or false
fn scale_record(value, index, language) {
    let raw=config(value,"scale");
    let key=if (raw is error) raw else c.id(c.option(raw,"id",if (index==0) "time" else null));
    let kind=c.text(c.option(raw,"kind","calendar"));
    let clock=chronology(kind);
    let record={timezone:"UTC",locale:language,*:raw,id:key,kind:kind};
    let domain=if (clock and raw.domain is array) [for (at in raw.domain) dates.normalize(at,record)] else [];
    let failure=util.first_error([raw,key,*domain,
        if (not clock and not contains(["linear","log","sqrt","pow","band","point","identity","temporal"],kind)) c.fail("scale.kind","unknown chronology or quantitative scale"),
        if (clock and raw.domain!=null and (not (raw.domain is array) or len(raw.domain)!=2)) c.fail("scale.domain","expected two chronology bounds"),
        if (len(domain)==2 and domain[0].value>=domain[1].value) c.fail("scale.domain","expected increasing bounds"),
        if (clock and kind!="ordinal" and kind!="calendar" and raw.unit==null) c.fail("scale.unit","numeric clock requires an explicit unit"),
        if (kind=="calendar" and not zones.valid_zone(record.timezone)) c.fail("scale.timezone","unknown timezone or invalid offset"),
        if (raw.calendar!=null and not c.enum(raw.calendar,["gregorian","proleptic_gregorian"])) c.fail("scale.calendar","alternate calendars require an explicit adapter"),
        if (raw.numbering!=null and not c.enum(raw.numbering,["astronomical","human"])) c.fail("scale.numbering","expected astronomical or human numbering"),
        if (raw.locale!=null and (not (raw.locale is string) or raw.locale=="")) c.fail("scale.locale","expected a locale identifier"),
        if (raw.format!=null and not (raw.format is string)) c.fail("scale.format","expected a formatting pattern"),
        if (raw.era_labels!=null and not (raw.era_labels is map)) c.fail("scale.era_labels","expected era labels"),
        for (key,value in c.attrs(raw.era_labels)) if (not contains(["bce","ce"],string(key)) or not (value is string))
            c.fail("scale.era_labels","expected bce/ce strings"),
        if (raw.ambiguous_time!=null and not c.enum(raw.ambiguous_time,["reject","earlier","later"])) c.fail("scale.ambiguous_time","invalid fold policy"),
        if (raw.nonexistent_time!=null and not c.enum(raw.nonexistent_time,["reject","forward","backward"])) c.fail("scale.nonexistent_time","invalid gap policy"),
        if (clock) time_map.validate(record) else scales_engine.configured_scale([],0,1,kind,{*:raw,type:kind})]);
    if (failure is error) failure else record
}

fn window_rules(value, scale) {
    let parsed=map([for (key in ["from","to","min","max"] where value[key]!=null) (key,dates.normalize(value[key],scale))]);
    util.first_error([for (key,record in parsed) record,
        for (pair in [["from","to"],["min","max"]] where parsed[pair[0]]!=null and parsed[pair[1]]!=null)
            if (parsed[pair[0]].value>=parsed[pair[1]].value) c.fail("window." ++ pair[1],"expected increasing bounds"),
        for (key in ["min_span","max_span"] where value[key]!=null) if (not c.positive(value[key])) c.fail("window." ++ key,"expected a positive span"),
        if (value.min_span!=null and value.max_span!=null and value.min_span>value.max_span) c.fail("window","contradictory span constraints")])
}

fn projection(value, path) {
    let fields=["active_id","selected_ids","window","collapsed_groups","expanded_events","query","filters","view","theme"];
    if (value==null) null else if (not (value is map)) c.fail(path,"expected an interaction-state map") else util.first_error([
        c.properties(value,fields,path),
        if (value.active_id!=null) c.id(value.active_id),
        for (field in ["selected_ids","collapsed_groups","expanded_events"] where c.has(value,field))
            if (not (value[field] is array)) c.fail(path ++ "." ++ field,"expected an ID array") else util.first_error([for (id in value[field]) c.id(id)]),
        if (c.has(value,"query") and not (value.query is string)) c.fail(path ++ ".query","expected search text"),
        if (value.filters!=null and not (value.filters is map)) c.fail(path ++ ".filters","expected filter fields"),
        if (value.view!=null and not c.enum(value.view,["cards","axis","story"])) c.fail(path ++ ".view","invalid view"),
        if (c.has(value,"window") and (not (value.window is map) or not util.finite_number(value.window.from) or
            not util.finite_number(value.window.to) or value.window.from>=value.window.to)) c.fail(path ++ ".window","expected increasing canonical chronology coordinates")])
}

pub fn validate(input) {
    let raw = root(input);
    if (raw is error) raw else {
        let presentation = c.text(c.option(raw, "view", "cards"));
        let spacing = c.text(c.option(raw, "spacing", if (presentation == "cards") "sequence" else "time"));
        let language=c.option(raw,"locale","en-US");
        let scales = [for (index,value in c.items(raw.scales)) scale_record(value,index,language)];
        let scale_id = c.text(c.option(raw, "time_scale", "time"));
        let primary = scales |: ~.id == scale_id;
        let scale = if (len(primary)>0) primary[0] else {id:scale_id,timezone:"UTC",kind:"calendar",locale:language};
        let base = {*:raw, version:c.option(raw, "version", 1), view:presentation, spacing:spacing,
            orientation:c.text(c.option(raw, "orientation", if (presentation == "cards") "vertical" else "horizontal")),
            placement:c.text(c.option(raw, "placement", if (presentation == "cards") "alternate" else "auto")),
            order:c.text(c.option(raw, "order", if (spacing == "sequence") "source" else "chronological")),
            locale:language, direction:c.text(c.option(raw, "direction", locale.direction(language))),
            motion:c.text(c.option(raw, "motion", "full")), scale:scale, scales:scales,time_scale:scale_id,
            width:c.option(raw, "width", "auto"), height:c.option(raw, "height", "auto"), gap:c.option(raw, "gap", 20),
            padding:sizing.normalize_padding(raw.padding)};
        let configurations = map([for (key in ["window", "coordinate", "navigator", "cards", "story", "markers", "interaction", "edit", "cluster", "search", "slideshow", "media_options", "toolbar", "source", "export"])
            (key, config(raw[key], key))]);
        let intro=introduction(configurations.story.title_slide,base);
        let records = if (raw.data is array) raw.data else if (raw.data.values is array) raw.data.values else [];
        let transformed = if (raw.transform != null) transform.apply_transforms(records, raw.transform) else records;
        let values = if (transformed is error) [] else [*c.items(raw.events), *encoded(transformed, raw.encoding)];
        let events = [for (index, value in values) event(value, index, base)];
        let groups = flatten_groups(c.items(raw.groups));
        let eras = [for (index, value in c.items(raw.eras), let era = config(value, "era"))
            if (era is error) era else event({*:era, kind:"background"}, index, base)];
        let axes = [for (value in c.items(raw.axes)) config(value, "axis")];
        let annotations = [for (index,value in c.items(raw.annotations),let parsed=config(value,"annotation"))
            if (parsed is error) parsed else annotation.normalize(parsed,scale,index)];
        let series = [for (value in c.items(raw.series)) config(value, "series")];
        let failure = util.first_error([c.properties(raw, root_keys, "chart", true), base.padding,
            if (not (language is string) or language=="") c.fail("locale","expected a locale identifier"),
            for (key, value in configurations) value, intro,*scales, *events, *groups, *eras, *axes, *annotations, *series, transformed,
            window_rules(configurations.window,scale),projection(raw.value,"value"),projection(raw.default_value,"default_value"),
            if (scale.kind=="calendar" and configurations.edit.snap_step!=null and configurations.edit.snap_unit!="unit" and
                not c.integral(configurations.edit.snap_step)) c.fail("edit.snap_step","calendar steps must be integral"),
            if (scale.kind!="calendar" and configurations.edit.snap_unit!=null and configurations.edit.snap_unit!="unit" and
                configurations.edit.snap_unit!=scale.unit) c.fail("edit.snap_unit","numeric snapping must use the chronology's declared unit"),
            if (not chronology(scale.kind)) c.fail("time_scale","primary scale must describe chronology"),
            if (c.has(raw,"time_scale") and len(primary)==0 and scale_id!="time") c.fail("time_scale","unknown primary scale ID"),
            unique_ids(scales,"scales"),
            if (base.version != 1) c.fail("version", "unsupported schema version"),
            if (configurations.story.bookmarks==true) c.id(base.id),
            if (configurations.story.bookmarks==true and any(events |> ~.unstable_id==true))
                c.fail("story.bookmarks","every addressed event requires an authored stable ID"),
            if (not contains(["cards", "axis", "story"], presentation)) c.fail("view", "invalid view"),
            if (not contains(["sequence", "time"], spacing) or presentation == "axis" and spacing != "time") c.fail("spacing", "invalid presentation/spacing combination"),
            if (not contains(["vertical", "horizontal"], base.orientation)) c.fail("orientation", "invalid orientation"),
            if (not contains(["start", "end", "alternate", "auto"], base.placement)) c.fail("placement", "invalid placement"),
            if (not contains(["source", "chronological", "reverse_chronological"], base.order)) c.fail("order", "invalid order"),
            if (not contains(["ltr", "rtl"], base.direction)) c.fail("direction", "invalid direction"),
            if (not contains(["full", "reduced", "none"], base.motion)) c.fail("motion", "invalid motion"),
            for (key in ["width", "height"]) if (base[key] != "auto" and not c.positive(base[key])) c.fail(key, "expected positive pixels or auto"),
            for (dimension in ["width","height"])
                util.first_error([for (key in ["min_" ++ dimension,"max_" ++ dimension] where raw[key]!=null)
                    if (not c.positive(raw[key])) c.fail(key,"expected positive finite pixels"),
                    if (raw["min_" ++ dimension]!=null and raw["max_" ++ dimension]!=null and raw["min_" ++ dimension]>raw["max_" ++ dimension])
                        c.fail(dimension,"contradictory size constraints")]),
            if (not util.finite_number(base.gap) or base.gap < 0) c.fail("gap", "expected nonnegative finite pixels"),
            if (raw.data != null and not (raw.data is array) and not (raw.data.values is array)) c.fail("data", "expected records or a values array"),
            if (raw.data != null and raw.source != null) c.fail("data", "cannot combine data and source"),
            if (c.has(raw, "value") and c.has(raw, "default_value")) c.fail("value", "cannot combine value and default_value"),
            unique_ids(events, "events"), unique_ids(groups, "groups"), unique_ids(eras, "eras"),unique_ids(annotations,"annotations"),
            if (intro!=null and contains(events |> ~.id,intro.id)) c.fail("story.title_slide.id","must differ from every event ID"),
            if (configurations.story.active_id!=null and not contains([intro.id,*[for (item in events) item.id]],c.text(configurations.story.active_id)))
                c.fail("story.active_id","unknown narrative event ID"),
            if (configurations.story.order!=null and (not (configurations.story.order is array) or
                len(unique(configurations.story.order |> c.text(~)))!=len(configurations.story.order) or
                not all([for (id in c.items(configurations.story.order)) contains(events |> ~.id,c.text(id))])))
                c.fail("story.order","expected unique known event IDs"),
            unique_ids([for (item in series where item.id!=null) {*:item,id:c.text(item.id)}],"series"),
            for (item in axes where item.scale!=null)
                if (c.text(item.scale)!=scale_id and not contains(scales |> ~.id,c.text(item.scale))) c.fail("axis.scale","unknown scale ID"),
            for (item in series)
                util.first_error([if (item.time_scale!=null and c.text(item.time_scale)!=scale_id) c.fail("series.time_scale","series must use the primary chronology"),
                    if (item.value_scale!=null and not contains(scales |> ~.id,c.text(item.value_scale))) c.fail("series.value_scale","unknown scale ID"),
                    if (item.value_scale!=null and chronology((scales |: ~.id==c.text(item.value_scale))[0].kind)) c.fail("series.value_scale","expected a quantitative scale")]),
            for (group in groups) ancestry(group, groups),
            for (item in [*events, *eras,*annotations] where item.group != null)
                if (not contains(groups |> ~.id, item.group)) c.fail("event.group", "unknown group"),
            for (item in events where item.subgroup!=null,
                let group=(groups |: ~.id==item.group)[0])
                if (group==null or not contains(c.items(group.subgroups) |> ~.id,item.subgroup)) c.fail("event.subgroup","unknown subgroup in the event's group"),
            for (group in groups) for (item in [group,*c.items(group.subgroups)])
                if (item.min_height!=null and item.max_height!=null and item.min_height>item.max_height) c.fail("group.height","contradictory height constraints"),
            if (configurations.coordinate.kind != null and not c.enum(configurations.coordinate.kind, ["straight", "serpentine", "spiral", "curve"])) c.fail("coordinate.kind", "invalid coordinate"),
            if (configurations.coordinate.kind != null and c.text(configurations.coordinate.kind) != "straight" and (spacing != "time" or presentation == "cards")) c.fail("coordinate", "curves require time spacing and axis/story presentation")]);
        if (failure is error) failure else {
            let sorted = if (base.order == "source") events else sort(events, {by:(item) => if (item.start != null) item.start.value else item.index,
                dir:if (base.order == "reverse_chronological") "desc" else "asc"});
            {*:base, *:configurations, events:sorted, intro:intro,groups:groups, eras:eras, axes:axes, annotations:annotations, series:series}
        }
    }
}
