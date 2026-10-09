import c: .component

pub fn enabled(items: array) => [for (item in items where item.disabled != true) item]
pub fn require_id(props, owner) bool^ => if (props.id is string and props.id != "") true else raise c.fail(owner,"a nonempty id is required")
pub fn validate_keys(items, keys, owner) bool^ {
    if (not (keys is array)) raise c.fail(owner,"keys must be an array")
    else if (len(unique(keys)) != len(keys)) raise c.fail(owner,"keys must be unique")
    else if (not all([for (key in keys) any([for (item in items) item.key == key])])) raise c.fail(owner,"unknown key")
    else true
}
pub fn key_props(items, props, name, default_name, owner) bool^ {
    let current = validate_keys(items,c.option(props,name,[]),owner)^
    let initial = validate_keys(items,c.option(props,default_name,[]),owner)^;
    if (c.has(props,name) and c.has(props,default_name)) raise c.fail(owner,name ++ " and " ++ default_name ++ " are mutually exclusive") else true
}
pub fn toggle(keys, key, multiple = true) => if (contains(keys,key)) [for (old in keys where old != key) old] else if (multiple) [*keys,key] else [key]
pub fn set_keys(keys, candidates, checked) => if (checked) unique([*keys,*candidates]) else [for (key in keys where not contains(candidates,key)) key]
pub fn validate(items, owner) bool^ {
    if (not (items is array)) raise c.fail(owner, "items must be an array")
    else (
        let invalid = [for (item in items where not (item is map) or item.key == null or
            not (item.key is string or item.key is symbol or item.key is int) or
            (item.disabled != null and not (item.disabled is bool))) item],
        let keys = [for (item in items) c.text(item.key)],
        if (len(invalid) > 0) raise c.fail(owner, "items need scalar keys and boolean disabled flags")
        else if (len(unique(keys)) != len(keys)) raise c.fail(owner, "keys must have distinct string representations")
        else true
    )
}
pub fn initial(items, props) => c.option(props, "value", c.option(props, "default_value", enabled(items)[0].key))
pub fn move(items: array, key, direction) {
    let choices = enabled(items)
    let found = [for (index, item in choices where item.key == key) index]
    let here = if (len(found) == 0) 0 else found[0];
    if (len(choices) == 0) null
    else if (direction == "Home") choices[0].key
    else if (direction == "End") choices[len(choices) - 1].key
    else choices[(here + (if (direction == "ArrowLeft" or direction == "ArrowUp") len(choices) - 1 else 1)) % len(choices)].key
}
pub fn page_count(total, page_size) => max(1, int(ceil(total / page_size)))
pub fn page_rows(rows: array, current: int, page_size: int) array^ => slice(rows, (current - 1) * page_size, current * page_size)^
pub fn order_rows(rows: array, key, direction) => if (key == null) rows else sort(rows, {dir:direction, by:(row) => row[key]})
