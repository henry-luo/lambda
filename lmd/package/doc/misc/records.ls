// vCard/iCalendar share RFC content lines, folding, and ordered property traversal.
import c: .common
fn property_name(value,target) string^ => upper(c.token(value,"property name",if (target == "vcf") "." else "")^)
fn base_name(value) => upper(slice(string(value),c.get({i:last_index_of(string(value),".")},"i",-1)+1))
fn entries(record) array^ {
    if (record.entries != null and not (record.entries is array)) raise c.fail("entries must be an array")
    else if (record.entries != null) [for (entry in record.entries) {
        if (not (entry is map) or entry.name == null or entry.value == null) raise c.fail("entry requires name and value")
        else {*:entry,raw:c.get(entry,"raw",true)}
    }]
    else if (record.properties != null and not (record.properties is map)) raise c.fail("properties must be a map")
    else [for (name,value in c.get(record,"properties",{}),item in c.sequence(value)) {name:string(name),value:item,raw:true}]
}
fn param_value(value,raw,version) string^ {
    let text = string(value)
    let encoded = if (raw or version == "3.0") c.single_line(text,"parameter value")^ else
        replace(replace(replace(c.newline(text),"^","^^"),"\n","^n"),"\"","^'");
    if (version == "3.0" and (contains(text,"\"") or contains(text,"\n") or contains(text,"\r")))
        raise c.fail("vCard 3.0 parameter contains an unsupported quote or newline")
    else if (contains(encoded,":") or contains(encoded,";") or (not raw and contains(encoded,",")) or starts_with(encoded," ") or ends_with(encoded," ")) "\"" ++ encoded ++ "\""
    else encoded
}
fn parameters(values,raw,version) string^ {
    if (values != null and not (values is map)) raise c.fail("parameters must be a map")
    else join([for (key,value in c.get({v:values},"v",{}))
        ";" ++ upper(c.token(key,"parameter name")^) ++ "=" ++
        join([for (item in c.sequence(value)) param_value(item,raw,version)^],",")],"")
}
fn property(entry,target,version) string^ {
    let name = property_name(entry.name,target)^
    let bare = base_name(name)
    let raw = c.get(entry,"raw",false)
    let params = c.get(entry,"parameters",{})
    let value_kind = upper(c.get(params,"VALUE",c.get(params,"value","")))
    let text_fields = if (target == "vcf") ["FN","NICKNAME","TITLE","ROLE","NOTE","CATEGORIES","EMAIL"] else
        ["SUMMARY","DESCRIPTION","LOCATION","COMMENT","CONTACT","RESOURCES","CATEGORIES","NAME","UID","PRODID","TZID","TZNAME"]
    let wire = if (raw) c.single_line(entry.value,"property value")^ else if (value_kind == "TEXT" or (value_kind == "" and bare in text_fields))
        c.text_escape(entry.value) else c.single_line(entry.value,"property value")^
    let parameter_source = if (raw and entry.parameter_source != null) c.single_line(entry.parameter_source,"parameter source")^ else null;
    if (bare in ["BEGIN","END"]) raise c.fail("BEGIN/END are represented by record containers")
    else if (parameter_source != null and not starts_with(parameter_source,";")) raise c.fail("parameter_source must begin with a semicolon")
    else c.fold_line(name ++ (if (parameter_source != null) parameter_source else parameters(params,raw,version)^) ++ ":" ++ wire)
}
fn structured(value,keys) string^ {
    if (not (value is map)) raise c.fail("structured contact field must be a map")
    else join([for (key in keys) join([for (part in c.sequence(value[key])) c.text_escape(part)],",")],";")
}
fn fresh(name,value,parameters = null) => {name:name,value:value,parameters:parameters,raw:false}
fn contact_fields(value,version) array^ {
    let names = {full_name:"FN",email:"EMAIL",phone:"TEL",organization:"ORG",title:"TITLE",note:"NOTE",url:"URL",birthday:"BDAY"}
    let ordinary = [for (field,name in names,item in c.sequence(value[field]))
        if (item is map and item.value != null) fresh(name,item.value,c.get(item,"parameters",{}))
        else fresh(name,if (field == "organization") c.text_escape(item) else item,
            if (field == "phone" and version == "4.0" and not starts_with(string(item),"tel:")) {VALUE:"text"} else null)]
    let name = if (value.name == null) [] else [fresh("N",structured(value.name,["family","given","additional","prefix","suffix"])^)]
    let address = [for (item in c.sequence(value.address)) fresh("ADR",structured(item,["po_box","extended","street","city","state","postal_code","country"])^)];
    [*ordinary,*name,*address]
}
fn contains_property(values,name) => any([for (entry in values) base_name(entry.name) == name and entry.value != null and entry.value != ""])
fn contact(value) map^ {
    let valid_contact = if (not (value is map)) raise c.fail("vCard contact must be a map")
    let retained = entries(value)^
    let versions = [for (entry in retained where base_name(entry.name) == "VERSION") string(entry.value)]
    let version = if (len(versions) > 0) versions[0] else c.get(value,"version","4.0");
    let valid_version = if (not (version in ["3.0","4.0"]) or len(versions) > 1) raise c.fail("vCard requires one VERSION, 3.0 or 4.0")
    let fields = if (value.entries != null or value.properties != null) retained else contact_fields(value,version)^
    let body = join([for (entry in fields where base_name(entry.name) != "VERSION") property(entry,"vcf",version)^],"")
    let issues = (if (not contains_property(fields,"FN")) [c.issue("missing-property",value,"vCard requires FN")] else []) ++
        (if (version == "3.0" and not contains_property(fields,"N")) [c.issue("missing-property",value,"vCard 3.0 requires N")] else []) ++
        c.unknown(value,["entries","properties","version","full_name","name","email","phone","organization","title","note","url","birthday","address"],"contact field");
    c.result("BEGIN:VCARD\r\nVERSION:" ++ version ++ "\r\n" ++ body ++ "END:VCARD\r\n",issues)
}
fn digits(value,width,label) string^ {
    let text = string(value);
    if (len(text) > width or len(text) == 0 or any([for (ch in split(text,"")) not contains("0123456789",ch)])) raise c.fail("invalid " ++ label)
    else c.times("0",width-len(text)) ++ text
}
fn calendar_date(value) map^ {
    if (value is string) {value:c.single_line(value,"calendar date")^,parameters:null}
    else if (not (value is map)) raise c.fail("calendar date must be text or a date-part map")
    else {
        let date_text = digits(value.year,4,"year")^ ++ digits(value.month,2,"month")^ ++ digits(value.day,2,"day")^
        let has_time = value.hour != null or value.minute != null or value.second != null
        let timezone = c.get(value,"timezone","")
        let suffix = if (not has_time) "" else "T" ++ digits(value.hour,2,"hour")^ ++ digits(value.minute,2,"minute")^ ++ digits(value.second,2,"second")^
        let year = int(value.year)
        let month = int(value.month)
        let leap = year%4 == 0 and (year%100 != 0 or year%400 == 0)
        let days = [31,if (leap) 29 else 28,31,30,31,30,31,31,30,31,30,31];
        if (month < 1 or month > 12 or int(value.day) < 1 or int(value.day) > days[month-1] or
            (has_time and (int(value.hour) > 23 or int(value.minute) > 59 or int(value.second) > 60))) raise c.fail("calendar date is out of range")
        else {value:date_text ++ suffix ++ (if (has_time and timezone == "UTC") "Z" else ""),
            parameters:if (not has_time) {VALUE:"DATE"} else if (timezone != "" and timezone != "UTC") {TZID:timezone} else null}
    }
}
fn duration(value) string^ {
    if (value is string) c.single_line(value,"duration")^
    else if (not (value is map)) raise c.fail("duration must be text or a duration map")
    else {
        let keys = {weeks:"W",days:"D",hours:"H",minutes:"M",seconds:"S"}
        let invalid = [for (key,item in value where not (string(key) in ["weeks","days","hours","minutes","seconds","negative"])) key]
        let time_text = join([for (key in ["hours","minutes","seconds"] where value[key] != null) digits(value[key],len(string(value[key])),key)^ ++ keys[key]],"");
        if (len(invalid) > 0 or (value.weeks != null and any([for (key in ["days","hours","minutes","seconds"]) value[key] != null]))) raise c.fail("invalid duration units")
        else (if (value.negative == true) "-" else "") ++ "P" ++
            join([for (key in ["weeks","days"] where value[key] != null) digits(value[key],len(string(value[key])),key)^ ++ keys[key]],"") ++
            (if (time_text == "" and value.weeks == null and value.days == null) "T0S" else if (time_text == "") "" else "T" ++ time_text)
    }
}
fn component_fields(value) array^ {
    let names = {summary:"SUMMARY",description:"DESCRIPTION",location:"LOCATION",status:"STATUS",priority:"PRIORITY",organizer:"ORGANIZER",attendee:"ATTENDEE",uid:"UID",stamp:"DTSTAMP",created:"CREATED",modified:"LAST-MODIFIED",sequence:"SEQUENCE",due:"DUE",recurrence_rule:"RRULE",categories:"CATEGORIES",action:"ACTION",trigger:"TRIGGER"}
    let ordinary = [for (field,name in names,item in c.sequence(value[field]))
        if (item is map and item.value != null) fresh(name,item.value,c.get(item,"parameters",{})) else fresh(name,item)]
    let dates = [for (field,name in {start_time:"DTSTART",end_time:"DTEND"} where value[field] != null) {
        let date_parts = calendar_date(value[field])^;
        fresh(name,date_parts.value,date_parts.parameters)
    }]
    let durations = if (value.duration == null) [] else [fresh("DURATION",duration(value.duration)^)];
    [*ordinary,*dates,*durations]
}
fn component(value,depth,method) map^ {
    let valid_component = if (depth > 64) raise c.fail("calendar component nesting exceeds 64")
    else if (not (value is map)) raise c.fail("calendar component must be a map")
    let component_type = upper(c.token(value.type,"calendar component type")^)
    let values = if (value.entries != null or value.properties != null) entries(value)^ else component_fields(value)^
    let emitted = c.merge([for (entry in values) if (entry.value is map and entry.value.type != null)
        component(entry.value,depth+1,method)^ else c.result(property(entry,"ics","2.0")^)])^
    let nested = if (value.entries != null) c.result("") else c.merge([for (child in c.sequence(value.components)) component(child,depth+1,method)^])^
    let required = if (component_type == "VEVENT") ["UID","DTSTAMP",*if (method == null) ["DTSTART"] else []]
        else if (component_type in ["VTODO","VJOURNAL","VFREEBUSY"]) ["UID","DTSTAMP"] else if (component_type == "VALARM") ["ACTION","TRIGGER"] else []
    let issues = [for (name in required where not contains_property(values,name)) c.issue("missing-property",value,component_type ++ " requires " ++ name)] ++
        (if (component_type == "VCALENDAR") [c.issue("nested-calendar",value,"VCALENDAR cannot be nested")] else []) ++
        c.unknown(value,["type","entries","properties","components","summary","description","start_time","end_time","duration","location","status","priority","organizer","attendee","uid","stamp","created","modified","sequence","due","recurrence_rule","categories","action","trigger"],"component field");
    c.result("BEGIN:" ++ component_type ++ "\r\n" ++ emitted.text ++ nested.text ++ "END:" ++ component_type ++ "\r\n",emitted.diagnostics ++ nested.diagnostics ++ issues)
}
fn calendar(value) map^ {
    let valid_calendar = if (not (value is map)) raise c.fail("iCalendar requires a calendar map")
    let values = if (value.entries != null or value.properties != null) entries(value)^ else
        [for (field,name in {version:"VERSION",product_id:"PRODID",calendar_scale:"CALSCALE",method:"METHOD"} where value[field] != null) fresh(name,value[field])]
    let version_entries = [for (entry in values where base_name(entry.name) == "VERSION") entry]
    let version = if (len(version_entries) == 0) "2.0" else string(version_entries[0].value);
    let valid_version = if (version != "2.0" or len(version_entries) > 1) raise c.fail("iCalendar requires one VERSION:2.0")
    let method_entries = [for (entry in values where base_name(entry.name) == "METHOD") entry.value]
    let method = if (len(method_entries) > 0) method_entries[0] else value.method
    let emitted = c.merge([for (entry in values where base_name(entry.name) != "VERSION")
        if (entry.value is map and entry.value.type != null) component(entry.value,0,method)^ else c.result(property(entry,"ics","2.0")^)])^
    let children = if (value.entries != null) c.result("") else c.merge([for (child in c.sequence(value.components)) component(child,0,method)^])^
    let product = if (contains_property(values,"PRODID")) "" else "PRODID:-//Lambda//doc.misc//EN\r\n"
    let component_count = if (value.entries != null) len([for (entry in values where entry.value is map and entry.value.type != null) entry]) else len(c.sequence(value.components))
    let issues = c.unknown(value,["version","product_id","calendar_scale","method","components","properties","entries"],"calendar field") ++
        (if (component_count == 0) [c.issue("missing-component",value,"VCALENDAR requires a component")] else []);
    c.result("BEGIN:VCALENDAR\r\nVERSION:2.0\r\n" ++ product ++ emitted.text ++ children.text ++ "END:VCALENDAR\r\n",emitted.diagnostics ++ children.diagnostics ++ issues)
}
pub fn render(value,target) map^ {
    if (target == "ics") calendar(value)^
    else if (value is map and value.contacts != null) c.merge([for (item in c.sequence(value.contacts)) contact(item)^])^
    else if (value is array or value is list) c.merge([for (item in value) contact(item)^])^
    else contact(value)^
}
