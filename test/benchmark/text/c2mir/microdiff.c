/* Native C2MIR port of text/microdiff.js. */
extern int printf(const char *, ...);

#define MICRODIFF_PATH_CAPACITY 16
#define MICRODIFF_CHANGE_CAPACITY 32

enum NodeKind {
    NODE_BOOL,
    NODE_INT,
    NODE_STRING,
    NODE_ARRAY,
    NODE_OBJECT,
    NODE_DATE,
    NODE_REGEX
};

enum ChangeKind { CHANGE_REMOVE, CHANGE_CREATE, CHANGE_CHANGE };

typedef struct Node Node;
typedef struct Field Field;

struct Node {
    int kind;
    const char *text;
    int integer;
    const Node *const *items;
    int item_count;
    const Field *fields;
    int field_count;
};

struct Field {
    const char *key;
    const Node *value;
};

typedef struct PathPart {
    const char *key;
    int index;
    int is_index;
} PathPart;

typedef struct Change {
    int kind;
    PathPart path[MICRODIFF_PATH_CAPACITY];
    int path_count;
    const Node *value;
    const Node *old_value;
} Change;

typedef struct DiffResult {
    Change changes[MICRODIFF_CHANGE_CAPACITY];
    int count;
} DiffResult;

#define STRING_NODE(value) {NODE_STRING, value, 0, 0, 0, 0, 0}
#define INT_NODE(value) {NODE_INT, 0, value, 0, 0, 0, 0}
#define BOOL_NODE(value) {NODE_BOOL, 0, value, 0, 0, 0, 0}
#define RICH_NODE(kind_value, value) {kind_value, value, 0, 0, 0, 0, 0}
#define ARRAY_NODE(values) {NODE_ARRAY, 0, 0, values, (int)(sizeof(values) / sizeof(values[0])), 0, 0}
#define OBJECT_NODE(values) {NODE_OBJECT, 0, 0, 0, 0, values, (int)(sizeof(values) / sizeof(values[0]))}

static const Node text_paragraph_type = STRING_NODE("paragraph");
static const Node text_code_type = STRING_NODE("code");
static const Node text_heading_type = STRING_NODE("heading");
static const Node text_list_type = STRING_NODE("list");
static const Node text_js = STRING_NODE("js");
static const Node text_intro = STRING_NODE("intro");
static const Node text_body = STRING_NODE("body");
static const Node text_paragraph = STRING_NODE("A short paragraph of source text.");
static const Node text_algorithms = STRING_NODE("Algorithms");
static const Node text_diff = STRING_NODE("diff");
static const Node text_snapshot = STRING_NODE("snapshot");
static const Node text_hyphen = STRING_NODE("hyphen");
static const Node text_title_old = STRING_NODE("Text benchmark");
static const Node text_title_new = STRING_NODE("Text benchmark — revised");
static const Node text_light = STRING_NODE("light");
static const Node text_dark = STRING_NODE("dark");
static const Node text_benchmark = STRING_NODE("benchmark");
static const Node text_updated = STRING_NODE("updated");
static const Node int_lines_old = INT_NODE(12);
static const Node int_lines_new = INT_NODE(18);
static const Node int_level_old = INT_NODE(1);
static const Node int_level_new = INT_NODE(2);
static const Node int_value_old = INT_NODE(41);
static const Node int_value_new = INT_NODE(42);
static const Node bool_false = BOOL_NODE(0);
static const Node bool_true = BOOL_NODE(1);
static const Node date_old = RICH_NODE(NODE_DATE, "1700000000000");
static const Node date_new = RICH_NODE(NODE_DATE, "1700000001000");
static const Node regex_old = RICH_NODE(NODE_REGEX, "source|text/g");
static const Node regex_new = RICH_NODE(NODE_REGEX, "source|text|diff/gi");

static const Field paragraph_fields[] = {
    {"type", &text_paragraph_type}, {"text", &text_paragraph}
};
static const Node paragraph_block = OBJECT_NODE(paragraph_fields);

static const Field code_old_fields[] = {
    {"type", &text_code_type}, {"language", &text_js}, {"lines", &int_lines_old}
};
static const Node code_old_block = OBJECT_NODE(code_old_fields);
static const Field code_new_fields[] = {
    {"type", &text_code_type}, {"language", &text_js}, {"lines", &int_lines_new}
};
static const Node code_new_block = OBJECT_NODE(code_new_fields);

static const Node *const intro_old_blocks_items[] = {&paragraph_block, &code_old_block};
static const Node intro_old_blocks = ARRAY_NODE(intro_old_blocks_items);
static const Node *const intro_new_blocks_items[] = {&paragraph_block, &code_new_block};
static const Node intro_new_blocks = ARRAY_NODE(intro_new_blocks_items);
static const Field intro_old_fields[] = {
    {"id", &text_intro}, {"blocks", &intro_old_blocks}
};
static const Node intro_old_section = OBJECT_NODE(intro_old_fields);
static const Field intro_new_fields[] = {
    {"id", &text_intro}, {"blocks", &intro_new_blocks}
};
static const Node intro_new_section = OBJECT_NODE(intro_new_fields);

static const Field heading_old_fields[] = {
    {"type", &text_heading_type}, {"level", &int_level_old}, {"text", &text_algorithms}
};
static const Node heading_old_block = OBJECT_NODE(heading_old_fields);
static const Field heading_new_fields[] = {
    {"type", &text_heading_type}, {"level", &int_level_new}, {"text", &text_algorithms}
};
static const Node heading_new_block = OBJECT_NODE(heading_new_fields);

static const Node *const list_old_items_values[] = {&text_diff, &text_snapshot};
static const Node list_old_items = ARRAY_NODE(list_old_items_values);
static const Node *const list_new_items_values[] = {&text_diff, &text_snapshot, &text_hyphen};
static const Node list_new_items = ARRAY_NODE(list_new_items_values);
static const Field list_old_fields[] = {
    {"type", &text_list_type}, {"items", &list_old_items}
};
static const Node list_old_block = OBJECT_NODE(list_old_fields);
static const Field list_new_fields[] = {
    {"type", &text_list_type}, {"items", &list_new_items}
};
static const Node list_new_block = OBJECT_NODE(list_new_fields);

static const Node *const body_old_blocks_items[] = {&heading_old_block, &list_old_block};
static const Node body_old_blocks = ARRAY_NODE(body_old_blocks_items);
static const Node *const body_new_blocks_items[] = {&heading_new_block, &list_new_block};
static const Node body_new_blocks = ARRAY_NODE(body_new_blocks_items);
static const Field body_old_fields[] = {
    {"id", &text_body}, {"blocks", &body_old_blocks}
};
static const Node body_old_section = OBJECT_NODE(body_old_fields);
static const Field body_new_fields[] = {
    {"id", &text_body}, {"blocks", &body_new_blocks}
};
static const Node body_new_section = OBJECT_NODE(body_new_fields);

static const Node *const document_old_sections_items[] = {&intro_old_section, &body_old_section};
static const Node document_old_sections = ARRAY_NODE(document_old_sections_items);
static const Node *const document_new_sections_items[] = {&intro_new_section, &body_new_section};
static const Node document_new_sections = ARRAY_NODE(document_new_sections_items);
static const Field document_old_fields[] = {
    {"title", &text_title_old}, {"sections", &document_old_sections}
};
static const Node document_old = OBJECT_NODE(document_old_fields);
static const Field document_new_fields[] = {
    {"title", &text_title_new}, {"sections", &document_new_sections}
};
static const Node document_new = OBJECT_NODE(document_new_fields);

static const Field flags_old_fields[] = {
    {"trackChanges", &bool_false}, {"preserveWhitespace", &bool_true}
};
static const Node flags_old = OBJECT_NODE(flags_old_fields);
static const Field flags_new_fields[] = {
    {"trackChanges", &bool_true}, {"preserveWhitespace", &bool_true}
};
static const Node flags_new = OBJECT_NODE(flags_new_fields);
static const Field options_old_fields[] = {
    {"theme", &text_light}, {"flags", &flags_old}
};
static const Node options_old = OBJECT_NODE(options_old_fields);
static const Field options_new_fields[] = {
    {"theme", &text_dark}, {"flags", &flags_new}
};
static const Node options_new = OBJECT_NODE(options_new_fields);

static const Node *const tags_old_items[] = {&text_diff, &text_benchmark};
static const Node tags_old = ARRAY_NODE(tags_old_items);
static const Node *const tags_new_items[] = {&text_diff, &text_benchmark, &text_updated};
static const Node tags_new = ARRAY_NODE(tags_new_items);

static const Field snapshot_old_fields[] = {
    {"document", &document_old}, {"options", &options_old}, {"tags", &tags_old},
    {"updated", &date_old}, {"pattern", &regex_old}, {"value", &int_value_old}
};
static const Node snapshot_old = OBJECT_NODE(snapshot_old_fields);
static const Field snapshot_new_fields[] = {
    {"document", &document_new}, {"options", &options_new}, {"tags", &tags_new},
    {"updated", &date_new}, {"pattern", &regex_new}, {"value", &int_value_new}
};
static const Node snapshot_new = OBJECT_NODE(snapshot_new_fields);

static int text_equal(const char *left, const char *right) {
    int index = 0;
    while (left[index] != 0 && right[index] != 0) {
        if (left[index] != right[index]) return 0;
        index++;
    }
    return left[index] == right[index];
}

static int node_equal(const Node *old_value, const Node *fresh_value) {
    if (old_value->kind != fresh_value->kind) return 0;
    if (old_value->kind == NODE_BOOL || old_value->kind == NODE_INT) {
        return old_value->integer == fresh_value->integer;
    }
    if (old_value->kind == NODE_STRING || old_value->kind == NODE_DATE ||
            old_value->kind == NODE_REGEX) {
        return text_equal(old_value->text, fresh_value->text);
    }
    return old_value == fresh_value;
}

static const Field *find_field(const Node *object, const char *key) {
    int index;
    for (index = 0; index < object->field_count; index++) {
        if (text_equal(object->fields[index].key, key)) return &object->fields[index];
    }
    return 0;
}

static void add_change(DiffResult *result, int kind, const PathPart *path, int path_count,
                       const Node *value, const Node *old_value) {
    Change *change;
    int index;
    if (result->count >= MICRODIFF_CHANGE_CAPACITY || path_count > MICRODIFF_PATH_CAPACITY) return;
    change = &result->changes[result->count];
    change->kind = kind;
    change->path_count = path_count;
    change->value = value;
    change->old_value = old_value;
    for (index = 0; index < path_count; index++) change->path[index] = path[index];
    result->count++;
}

static void diff_node(const Node *old_value, const Node *fresh_value, DiffResult *result,
                      const PathPart *path, int path_count);

static void diff_array(const Node *old_value, const Node *fresh_value, DiffResult *result,
                       const PathPart *path, int path_count) {
    int index;
    for (index = 0; index < old_value->item_count; index++) {
        PathPart child[MICRODIFF_PATH_CAPACITY];
        int copy_index;
        for (copy_index = 0; copy_index < path_count; copy_index++) child[copy_index] = path[copy_index];
        child[path_count].key = 0;
        child[path_count].index = index;
        child[path_count].is_index = 1;
        if (index >= fresh_value->item_count) {
            add_change(result, CHANGE_REMOVE, child, path_count + 1, 0, old_value->items[index]);
        } else {
            diff_node(old_value->items[index], fresh_value->items[index], result, child, path_count + 1);
        }
    }
    for (index = old_value->item_count; index < fresh_value->item_count; index++) {
        PathPart child[MICRODIFF_PATH_CAPACITY];
        int copy_index;
        for (copy_index = 0; copy_index < path_count; copy_index++) child[copy_index] = path[copy_index];
        child[path_count].key = 0;
        child[path_count].index = index;
        child[path_count].is_index = 1;
        add_change(result, CHANGE_CREATE, child, path_count + 1, fresh_value->items[index], 0);
    }
}

static void diff_object(const Node *old_value, const Node *fresh_value, DiffResult *result,
                        const PathPart *path, int path_count) {
    int index;
    for (index = 0; index < old_value->field_count; index++) {
        const Field *old_field = &old_value->fields[index];
        const Field *fresh_field = find_field(fresh_value, old_field->key);
        PathPart child[MICRODIFF_PATH_CAPACITY];
        int copy_index;
        for (copy_index = 0; copy_index < path_count; copy_index++) child[copy_index] = path[copy_index];
        child[path_count].key = old_field->key;
        child[path_count].index = 0;
        child[path_count].is_index = 0;
        if (fresh_field) {
            diff_node(old_field->value, fresh_field->value, result, child, path_count + 1);
        } else {
            add_change(result, CHANGE_REMOVE, child, path_count + 1, 0, old_field->value);
        }
    }
    for (index = 0; index < fresh_value->field_count; index++) {
        const Field *fresh_field = &fresh_value->fields[index];
        if (!find_field(old_value, fresh_field->key)) {
            PathPart child[MICRODIFF_PATH_CAPACITY];
            int copy_index;
            for (copy_index = 0; copy_index < path_count; copy_index++) child[copy_index] = path[copy_index];
            child[path_count].key = fresh_field->key;
            child[path_count].index = 0;
            child[path_count].is_index = 0;
            add_change(result, CHANGE_CREATE, child, path_count + 1, fresh_field->value, 0);
        }
    }
}

static void diff_node(const Node *old_value, const Node *fresh_value, DiffResult *result,
                      const PathPart *path, int path_count) {
    if (old_value->kind == NODE_OBJECT && fresh_value->kind == NODE_OBJECT) {
        diff_object(old_value, fresh_value, result, path, path_count);
    } else if (old_value->kind == NODE_ARRAY && fresh_value->kind == NODE_ARRAY) {
        diff_array(old_value, fresh_value, result, path, path_count);
    } else if (!node_equal(old_value, fresh_value)) {
        add_change(result, CHANGE_CHANGE, path, path_count, fresh_value, old_value);
    }
}

static DiffResult microdiff(const Node *old_value, const Node *fresh_value) {
    DiffResult result;
    result.count = 0;
    diff_node(old_value, fresh_value, &result, 0, 0);
    return result;
}

static int change_type_length(const Change *change) {
    if (change->kind == CHANGE_REMOVE) return 6;
    if (change->kind == CHANGE_CREATE) return 6;
    return 6;
}

static int valid_fixture(void) {
    DiffResult removed = microdiff(&snapshot_new, &snapshot_old);
    DiffResult created = microdiff(&snapshot_old, &snapshot_new);
    Change *remove_change;
    Change *create_change;
    if (removed.count != 10 || created.count != 10) return 0;
    remove_change = &removed.changes[3];
    create_change = &created.changes[3];
    if (remove_change->kind != CHANGE_REMOVE || create_change->kind != CHANGE_CREATE) return 0;
    if (remove_change->path_count != 7 || create_change->path_count != 7) return 0;
    return remove_change->path[6].is_index && remove_change->path[6].index == 2 &&
        create_change->path[6].is_index && create_change->path[6].index == 2;
}

int main(void) {
    const Node *old_values[4] = {&snapshot_new, &snapshot_old, &snapshot_new, &snapshot_old};
    const Node *fresh_values[4] = {&snapshot_old, &snapshot_new, &snapshot_old, &snapshot_new};
    int checksum = 0;
    int round;
    int index;
    if (!valid_fixture()) {
        printf("microdiff: FAIL fixture verification\n");
        return 1;
    }
    for (round = 0; round < 512; round++) {
        for (index = 0; index < 4; index++) {
            DiffResult result = microdiff(old_values[index], fresh_values[index]);
            int change_index;
            checksum = (checksum + result.count * 19) % 1000000007;
            for (change_index = 0; change_index < result.count; change_index++) {
                checksum = (checksum + change_type_length(&result.changes[change_index]) * 23 +
                            result.changes[change_index].path_count) % 1000000007;
            }
        }
    }
    if (checksum != 3278848) {
        printf("microdiff: FAIL checksum=%d\n", checksum);
        return 1;
    }
    printf("microdiff: CHECKSUM:%d\n", checksum);
    return 0;
}
