// Text benchmark: microdiff's recursive object/array snapshot diff.
// Rich leaves model the Date and RegExp values in the JavaScript fixture.

fn make_remove(path, old_value) => {type: "REMOVE", path: path, oldValue: old_value}

fn make_create(path, value) => {type: "CREATE", path: path, value: value}

fn make_change(path, value, old_value) =>
    {type: "CHANGE", path: path, value: value, oldValue: old_value}

pn is_rich_leaf(value) bool {
    if (value is map) {
        return "__microdiff_rich_type" at value
    }
    return false
}

pn rich_leaf_equal(old_value, fresh_value) bool {
    if (not is_rich_leaf(old_value) or not is_rich_leaf(fresh_value)) {
        return false
    }
    return old_value.__microdiff_rich_type == fresh_value.__microdiff_rich_type and
        old_value.value == fresh_value.value
}

pn both_nan(left, right) bool {
    return left is nan and right is nan
}

pn diff_array(old, fresh, prefix) {
    var changes = []
    var index: int = 0
    while (index < len(old)) {
        let path = prefix ++ [index]
        if (index >= len(fresh)) {
            changes = changes ++ [make_remove(path, old[index])]
        } else {
            changes = changes ++ diff_value(old[index], fresh[index], path)
        }
        index = index + 1
    }
    while (index < len(fresh)) {
        changes = changes ++ [make_create(prefix ++ [index], fresh[index])]
        index = index + 1
    }
    return changes
}

pn diff_map(old, fresh, prefix) {
    var changes = []
    for (key, old_value at old) {
        let path = prefix ++ [string(key)]
        if (key at fresh) {
            changes = changes ++ diff_value(old_value, fresh[key], path)
        } else {
            changes = changes ++ [make_remove(path, old_value)]
        }
    }
    for (key, fresh_value at fresh) {
        if (not (key at old)) {
            changes = changes ++ [make_create(prefix ++ [string(key)], fresh_value)]
        }
    }
    return changes
}

pn diff_value(old_value, fresh_value, path) {
    if (old_value is map and fresh_value is map) {
        if (is_rich_leaf(old_value) or is_rich_leaf(fresh_value)) {
            if (rich_leaf_equal(old_value, fresh_value)) []
            else [make_change(path, fresh_value, old_value)]
        } else {
            diff_map(old_value, fresh_value, path)
        }
    } else if (old_value is array and fresh_value is array) {
        diff_array(old_value, fresh_value, path)
    } else if (old_value == fresh_value or both_nan(old_value, fresh_value)) {
        []
    } else {
        [make_change(path, fresh_value, old_value)]
    }
}

fn rich_leaf(kind, value) => {__microdiff_rich_type: kind, value: value}

fn make_microdiff_snapshot(revised) => {
    document: {
        title: if (revised) "Text benchmark — revised" else "Text benchmark",
        sections: [
            {
                id: "intro",
                blocks: [
                    {type: "paragraph", text: "A short paragraph of source text."},
                    {type: "code", language: "js", lines: if (revised) 18 else 12}
                ]
            },
            {
                id: "body",
                blocks: [
                    {type: "heading", level: if (revised) 2 else 1, text: "Algorithms"},
                    {type: "list", items: if (revised)
                        ["diff", "snapshot", "hyphen"] else ["diff", "snapshot"]}
                ]
            }
        ]
    },
    options: {
        theme: if (revised) "dark" else "light",
        flags: {trackChanges: revised, preserveWhitespace: true}
    },
    tags: if (revised) ["text", "benchmark", "updated"] else ["text", "benchmark"],
    updated: rich_leaf("Date", if (revised) 1700000001000 else 1700000000000),
    pattern: rich_leaf("RegExp", if (revised) "source|text|diff/gi" else "source|text/g"),
    value: if (revised) 42 else 41
}

pn valid_snapshot_diffs(pairs) bool {
    let removed = diff_map(pairs[0][0], pairs[0][1], [])
    let created = diff_map(pairs[1][0], pairs[1][1], [])
    return len(removed) == 10 and len(created) == 10 and
        removed[3].type == "REMOVE" and removed[3].path ==
            ["document", "sections", 1, "blocks", 1, "items", 2] and
        created[3].type == "CREATE" and created[3].path ==
            ["document", "sections", 1, "blocks", 1, "items", 2]
}

pn main() {
    let pairs = [
        [make_microdiff_snapshot(true), make_microdiff_snapshot(false)],
        [make_microdiff_snapshot(false), make_microdiff_snapshot(true)],
        [make_microdiff_snapshot(true), make_microdiff_snapshot(false)],
        [make_microdiff_snapshot(false), make_microdiff_snapshot(true)]
    ]
    if (not valid_snapshot_diffs(pairs)) {
        print("microdiff: FAIL fixture verification\n")
        return
    }
    var checksum: int = 0
    var round: int = 0
    let t0 = clock()
    while (round < 512) {
        var index: int = 0
        while (index < len(pairs)) {
            let result = diff_map(pairs[index][0], pairs[index][1], [])
            checksum = (checksum + len(result) * 19) % 1000000007
            var change: int = 0
            while (change < len(result)) {
                checksum = (checksum + len(result[change].type) * 23 +
                    len(result[change].path)) % 1000000007
                change = change + 1
            }
            index = index + 1
        }
        round = round + 1
    }
    if (checksum == 3278848) {
        print("microdiff: CHECKSUM:" ++ checksum ++ "\n")
    } else {
        print("microdiff: FAIL checksum=" ++ checksum ++ "\n")
    }
    print("__TIMING__:" ++ ((clock() - t0) * 1000.0) ++ "\n")
}
