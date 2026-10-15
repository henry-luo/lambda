// Source markers may be reused only when they still describe the current task.
pub let states = ["todo","in-progress","done","waiting","blocked","cancelled","rejected","migrated","custom"]
fn attr(node,key,fallback = null) => if (node[key] == null) fallback else node[key]
fn decoded(marker,flavor) => if (marker == "[ ]") {state:"todo",lo:0,hi:0,label:null}
    else if ((flavor == "vimwiki" and marker == "[X]") or (flavor == "zimwiki" and marker == "[*]")) {state:"done",lo:100,hi:100,label:null}
    else if (flavor == "vimwiki" and marker in ["[.]","[o]","[O]"]) {
        state:"in-progress",lo:if (marker == "[.]") 1 else if (marker == "[o]") 34 else 67,
        hi:if (marker == "[.]") 33 else if (marker == "[o]") 66 else 99,label:null}
    else if (flavor == "vimwiki" and marker == "[-]") {state:"rejected",lo:null,hi:null,label:null}
    else if (flavor == "zimwiki" and marker == "[x]") {state:"custom",lo:null,hi:null,label:"not-completed"}
    else if (flavor == "zimwiki" and marker in ["[>]","[<]"]) {state:"migrated",lo:null,hi:null,label:if (marker == "[>]") "moved" else "moved-out"}
    else if (flavor in ["vimwiki","zimwiki"] and marker is string and len(marker) == 3 and starts_with(marker,"[") and ends_with(marker,"]"))
        {state:"custom",lo:null,hi:null,label:marker}
    else null
fn percentage(value) => value is string and value in [for (i in 0 to 100) string(i)]
pub fn model(node) {
    let status = attr(node,"data-task-state",if (node["data-checked"] == null) null else if (node["data-checked"] == "true") "done" else "todo")
    let lo = node["data-task-progress-min"]
    let hi = node["data-task-progress-max"]
    let status_label = node["data-task-label"]
    let marker = node["data-task-marker"]
    let flavor = node["data-task-flavor"]
    let checkboxes = if (node is element) [for (child in content(node) where child is element and string(name(child)) == "input" and child.type == "checkbox") child] else []
    let projection = all([for (child in checkboxes) (child.checked != null) == (status == "done") and child.disabled != null])
    let valid = projection and (status == null or status in states) and
        (status != "custom" or (status_label is string and status_label != "")) and
        ((lo == null and hi == null) or (percentage(lo) and percentage(hi) and int(lo) <= int(hi))) and
        ((marker == null and flavor == null) or (marker is string and flavor is string)) and
        (node["data-checked"] == null or node["data-checked"] == if (status == "done") "true" else "false");
    {state:status,label:status_label,lo:if (percentage(lo)) int(lo) else null,hi:if (percentage(hi)) int(hi) else null,marker:marker,flavor:flavor,valid:valid}
}
pub fn marker(node,target) {
    let task = model(node)
    let saved = decoded(task.marker,task.flavor)
    let reusable = task.flavor == target and saved != null and saved.state == task.state and
        saved.label == task.label and saved.lo == task.lo and saved.hi == task.hi
    let choices = if (target == "vimwiki") ["[ ]","[.]","[o]","[O]","[X]","[-]"]
        else if (target == "zimwiki") ["[ ]","[*]","[x]","[>]","[<]"] else []
    let matches = [for (marker in choices,let value = decoded(marker,target) where
        value.state == task.state and (task.label == null or value.label == task.label) and
        ((task.lo == null and task.hi == null and (value.lo == null or task.state in ["todo","done"])) or (value.lo == task.lo and value.hi == task.hi))) marker];
    if (task.state == null) {text:"",code:null}
    else if (not task.valid) {text:"",code:"invalid-task"}
    else if (reusable) {text:task.marker ++ " ",code:null}
    else if (len(matches) > 0) {text:matches[0] ++ " ",code:null}
    else {text:"",code:"unrepresentable-task"}
}
