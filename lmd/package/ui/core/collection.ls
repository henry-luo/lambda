import c: .component

pub fn enabled(items: array) => [for (item in items where item.disabled != true) item]
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
