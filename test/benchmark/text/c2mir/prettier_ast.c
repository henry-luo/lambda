/* Native C2MIR port of text/prettier_ast.ls.
 *
 * The port intentionally decodes the checked-in Babel AST and rebuilds the
 * compact document IR on every measured iteration.  It does not cache the
 * formatted text or substitute the known checksum: the workload is the same
 * AST-printer/layout traversal that the Lambda and Node rows measure.
 */
extern int printf(const char *, ...);
extern void *malloc(unsigned long);
extern void *fopen(const char *, const char *);
extern int fseek(void *, long, int);
extern long ftell(void *);
extern unsigned long fread(void *, unsigned long, unsigned long, void *);
extern int fclose(void *);

#define PRETTIER_ITERATIONS 256
#define PRETTIER_PRINT_WIDTH 80
#define PRETTIER_LARGE_LENGTH 1000000000
#define PRETTIER_OUTPUT_CAPACITY 262144
#define PRETTIER_DOC_CAPACITY 50000
#define PRETTIER_LINK_CAPACITY 180000

typedef struct Node Node;

typedef struct {
    Node **items;
    int count;
    int capacity;
} NodeList;

struct Node {
    char *type;
    char *name;
    char *value_text;
    char *kind;
    char *operator_text;
    char *pattern;
    char *flags;
    int value_bool;
    int computed;
    int shorthand;
    int async;
    int generator;
    int is_static;
    Node *id;
    Node *init;
    Node *key;
    Node *value;
    Node *argument;
    Node *left;
    Node *right;
    Node *object;
    Node *property;
    Node *callee;
    Node *test;
    Node *consequent;
    Node *alternate;
    Node *declaration;
    Node *local;
    Node *exported;
    Node *body_node;
    NodeList body;
    NodeList declarations;
    NodeList properties;
    NodeList elements;
    NodeList params;
    NodeList arguments;
    NodeList specifiers;
};

typedef struct {
    const char *input;
    int index;
} AstReader;

static int text_length(const char *text) {
    int length = 0;
    while (text && text[length]) length++;
    return length;
}

static int text_equals(const char *left, const char *right) {
    int index = 0;
    if (!left || !right) return 0;
    while (left[index] && right[index]) {
        if (left[index] != right[index]) return 0;
        index++;
    }
    return left[index] == right[index];
}

static char *text_copy_range(const char *text, int start, int end) {
    int length = end - start;
    char *copy = (char *)malloc((unsigned long)(length + 1));
    int index;
    for (index = 0; index < length; index++) copy[index] = text[start + index];
    copy[length] = 0;
    return copy;
}

static void node_list_add(NodeList *list, Node *node) {
    if (list->count == list->capacity) {
        int new_capacity = list->capacity ? list->capacity * 2 : 8;
        Node **items = (Node **)malloc((unsigned long)new_capacity * sizeof(Node *));
        int index;
        for (index = 0; index < list->count; index++) items[index] = list->items[index];
        list->items = items;
        list->capacity = new_capacity;
    }
    list->items[list->count] = node;
    list->count++;
}

static void reader_skip_space(AstReader *reader) {
    char ch = reader->input[reader->index];
    while (ch == ' ' || ch == '\n' || ch == '\r' || ch == '\t') {
        reader->index++;
        ch = reader->input[reader->index];
    }
}

static int reader_consume(AstReader *reader, char expected) {
    reader_skip_space(reader);
    if (reader->input[reader->index] != expected) return 0;
    reader->index++;
    return 1;
}

static int reader_hex_value(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return 0;
}

static void reader_append_utf8(char *output, int *length, int codepoint) {
    if (codepoint < 0x80) {
        output[*length] = (char)codepoint;
        *length = *length + 1;
    } else if (codepoint < 0x800) {
        output[*length] = (char)(0xc0 | (codepoint >> 6));
        output[*length + 1] = (char)(0x80 | (codepoint & 0x3f));
        *length = *length + 2;
    } else {
        output[*length] = (char)(0xe0 | (codepoint >> 12));
        output[*length + 1] = (char)(0x80 | ((codepoint >> 6) & 0x3f));
        output[*length + 2] = (char)(0x80 | (codepoint & 0x3f));
        *length = *length + 3;
    }
}

static char *reader_string(AstReader *reader) {
    int capacity = 64;
    int length = 0;
    char *result;
    reader_skip_space(reader);
    if (reader->input[reader->index] != '"') return 0;
    reader->index++;
    result = (char *)malloc((unsigned long)capacity);
    while (reader->input[reader->index] && reader->input[reader->index] != '"') {
        char ch = reader->input[reader->index++];
        if (ch == '\\') {
            char escaped = reader->input[reader->index++];
            if (escaped == 'n') ch = '\n';
            else if (escaped == 'r') ch = '\r';
            else if (escaped == 't') ch = '\t';
            else if (escaped == 'b') ch = '\b';
            else if (escaped == 'f') ch = '\f';
            else if (escaped == 'u') {
                int codepoint = reader_hex_value(reader->input[reader->index]) * 4096 +
                    reader_hex_value(reader->input[reader->index + 1]) * 256 +
                    reader_hex_value(reader->input[reader->index + 2]) * 16 +
                    reader_hex_value(reader->input[reader->index + 3]);
                reader->index += 4;
                while (length + 4 >= capacity) {
                    char *grown = (char *)malloc((unsigned long)capacity * 2);
                    int copied;
                    for (copied = 0; copied < length; copied++) grown[copied] = result[copied];
                    result = grown;
                    capacity *= 2;
                }
                reader_append_utf8(result, &length, codepoint);
                continue;
            } else ch = escaped;
        }
        if (length + 2 >= capacity) {
            char *grown = (char *)malloc((unsigned long)capacity * 2);
            int copied;
            for (copied = 0; copied < length; copied++) grown[copied] = result[copied];
            result = grown;
            capacity *= 2;
        }
        result[length++] = ch;
    }
    if (reader->input[reader->index] == '"') reader->index++;
    result[length] = 0;
    return result;
}

static char *reader_number(AstReader *reader) {
    int start;
    reader_skip_space(reader);
    start = reader->index;
    while (reader->input[reader->index] == '-' || reader->input[reader->index] == '+' ||
            reader->input[reader->index] == '.' || reader->input[reader->index] == 'e' ||
            reader->input[reader->index] == 'E' ||
            (reader->input[reader->index] >= '0' && reader->input[reader->index] <= '9')) {
        reader->index++;
    }
    return text_copy_range(reader->input, start, reader->index);
}

static void reader_skip_value(AstReader *reader);
static Node *reader_node(AstReader *reader);

static void reader_node_list(AstReader *reader, NodeList *list) {
    reader_consume(reader, '[');
    reader_skip_space(reader);
    if (reader_consume(reader, ']')) return;
    while (1) {
        node_list_add(list, reader_node(reader));
        reader_skip_space(reader);
        if (reader_consume(reader, ']')) return;
        reader_consume(reader, ',');
    }
}

static void reader_skip_literal(AstReader *reader, const char *literal) {
    int index = 0;
    while (literal[index]) {
        if (reader->input[reader->index] == literal[index]) reader->index++;
        index++;
    }
}

static void reader_skip_value(AstReader *reader) {
    char ch;
    reader_skip_space(reader);
    ch = reader->input[reader->index];
    if (ch == '"') {
        char *ignored = reader_string(reader);
        (void)ignored;
    } else if (ch == '{') {
        reader->index++;
        reader_skip_space(reader);
        if (reader_consume(reader, '}')) return;
        while (1) {
            char *ignored_key = reader_string(reader);
            (void)ignored_key;
            reader_consume(reader, ':');
            reader_skip_value(reader);
            reader_skip_space(reader);
            if (reader_consume(reader, '}')) return;
            reader_consume(reader, ',');
        }
    } else if (ch == '[') {
        reader->index++;
        reader_skip_space(reader);
        if (reader_consume(reader, ']')) return;
        while (1) {
            reader_skip_value(reader);
            reader_skip_space(reader);
            if (reader_consume(reader, ']')) return;
            reader_consume(reader, ',');
        }
    } else if (ch == 't') reader_skip_literal(reader, "true");
    else if (ch == 'f') reader_skip_literal(reader, "false");
    else if (ch == 'n') reader_skip_literal(reader, "null");
    else {
        char *ignored_number = reader_number(reader);
        (void)ignored_number;
    }
}

static int reader_bool(AstReader *reader) {
    reader_skip_space(reader);
    if (reader->input[reader->index] == 't') {
        reader_skip_literal(reader, "true");
        return 1;
    }
    reader_skip_literal(reader, "false");
    return 0;
}

static char *reader_scalar_text(AstReader *reader, int *bool_value) {
    char ch;
    reader_skip_space(reader);
    ch = reader->input[reader->index];
    if (ch == '"') return reader_string(reader);
    if (ch == 't' || ch == 'f') {
        *bool_value = reader_bool(reader);
        return 0;
    }
    if (ch == 'n') {
        reader_skip_literal(reader, "null");
        return 0;
    }
    return reader_number(reader);
}

static void reader_assign_node_field(Node *node, const char *key, Node *value) {
    if (text_equals(key, "id")) node->id = value;
    else if (text_equals(key, "init")) node->init = value;
    else if (text_equals(key, "key")) node->key = value;
    else if (text_equals(key, "value")) node->value = value;
    else if (text_equals(key, "argument")) node->argument = value;
    else if (text_equals(key, "left")) node->left = value;
    else if (text_equals(key, "right")) node->right = value;
    else if (text_equals(key, "object")) node->object = value;
    else if (text_equals(key, "property")) node->property = value;
    else if (text_equals(key, "callee")) node->callee = value;
    else if (text_equals(key, "test")) node->test = value;
    else if (text_equals(key, "consequent")) node->consequent = value;
    else if (text_equals(key, "alternate")) node->alternate = value;
    else if (text_equals(key, "declaration")) node->declaration = value;
    else if (text_equals(key, "local")) node->local = value;
    else if (text_equals(key, "exported")) node->exported = value;
    else if (text_equals(key, "body")) node->body_node = value;
    else if (text_equals(key, "expression")) node->argument = value;
}

static Node *reader_node(AstReader *reader) {
    Node *node = (Node *)malloc(sizeof(Node));
    int byte;
    for (byte = 0; byte < (int)sizeof(Node); byte++) ((char *)node)[byte] = 0;
    reader_consume(reader, '{');
    reader_skip_space(reader);
    if (reader_consume(reader, '}')) return node;
    while (1) {
        char *key = reader_string(reader);
        char ch;
        reader_consume(reader, ':');
        reader_skip_space(reader);
        ch = reader->input[reader->index];
        if (text_equals(key, "type") || text_equals(key, "name") ||
                text_equals(key, "kind") || text_equals(key, "operator") ||
                text_equals(key, "pattern") || text_equals(key, "flags")) {
            char *text = reader_string(reader);
            if (text_equals(key, "type")) node->type = text;
            else if (text_equals(key, "name")) node->name = text;
            else if (text_equals(key, "kind")) node->kind = text;
            else if (text_equals(key, "operator")) node->operator_text = text;
            else if (text_equals(key, "pattern")) node->pattern = text;
            else node->flags = text;
        } else if (text_equals(key, "computed")) node->computed = reader_bool(reader);
        else if (text_equals(key, "shorthand")) node->shorthand = reader_bool(reader);
        else if (text_equals(key, "async")) node->async = reader_bool(reader);
        else if (text_equals(key, "generator")) node->generator = reader_bool(reader);
        else if (text_equals(key, "static")) node->is_static = reader_bool(reader);
        else if (text_equals(key, "body") && ch == '[') reader_node_list(reader, &node->body);
        else if (text_equals(key, "declarations")) reader_node_list(reader, &node->declarations);
        else if (text_equals(key, "properties")) reader_node_list(reader, &node->properties);
        else if (text_equals(key, "elements")) reader_node_list(reader, &node->elements);
        else if (text_equals(key, "params")) reader_node_list(reader, &node->params);
        else if (text_equals(key, "arguments")) reader_node_list(reader, &node->arguments);
        else if (text_equals(key, "specifiers")) reader_node_list(reader, &node->specifiers);
        else if (text_equals(key, "value") && ch != '{' && ch != '[') {
            node->value_text = reader_scalar_text(reader, &node->value_bool);
        } else if ((text_equals(key, "id") || text_equals(key, "init") ||
                text_equals(key, "key") || text_equals(key, "value") ||
                text_equals(key, "argument") || text_equals(key, "left") ||
                text_equals(key, "right") || text_equals(key, "object") ||
                text_equals(key, "property") || text_equals(key, "callee") ||
                text_equals(key, "test") || text_equals(key, "consequent") ||
                text_equals(key, "alternate") || text_equals(key, "declaration") ||
                text_equals(key, "local") || text_equals(key, "exported") ||
                text_equals(key, "expression") ||
                text_equals(key, "body")) && ch == '{') {
            reader_assign_node_field(node, key, reader_node(reader));
        } else if (ch == 'n') {
            reader_skip_literal(reader, "null");
        } else {
            reader_skip_value(reader);
        }
        reader_skip_space(reader);
        if (reader_consume(reader, '}')) return node;
        reader_consume(reader, ',');
    }
}

typedef struct Doc Doc;

enum {
    DOC_TEXT,
    DOC_ESCAPED_TEXT,
    DOC_CONCAT,
    DOC_INDENT,
    DOC_GROUP,
    DOC_IF_BREAK,
    DOC_LINE,
    DOC_SOFTLINE,
    DOC_HARDLINE
};

struct Doc {
    int kind;
    const char *text;
    Doc **parts;
    int count;
    Doc *first;
    Doc *second;
    int threshold;
};

static Doc doc_arena[PRETTIER_DOC_CAPACITY];
static Doc *doc_links[PRETTIER_LINK_CAPACITY];
static int doc_count;
static int doc_link_count;

static Doc *doc_new(int kind) {
    Doc *doc = &doc_arena[doc_count++];
    doc->kind = kind;
    doc->text = 0;
    doc->parts = 0;
    doc->count = 0;
    doc->first = 0;
    doc->second = 0;
    doc->threshold = PRETTIER_LARGE_LENGTH;
    return doc;
}

static Doc **doc_part_list(int count) {
    Doc **parts = &doc_links[doc_link_count];
    doc_link_count += count;
    return parts;
}

static Doc *doc_text(const char *text) {
    Doc *doc = doc_new(DOC_TEXT);
    doc->text = text ? text : "";
    return doc;
}

static Doc *doc_escaped_text(const char *text) {
    Doc *doc = doc_new(DOC_ESCAPED_TEXT);
    doc->text = text ? text : "";
    return doc;
}

static Doc *doc_concat(Doc **parts, int count) {
    Doc *doc = doc_new(DOC_CONCAT);
    doc->parts = parts;
    doc->count = count;
    return doc;
}

static Doc *doc_unary(int kind, Doc *contents) {
    Doc *doc = doc_new(kind);
    doc->first = contents;
    return doc;
}

static Doc *doc_if_break(Doc *broken, Doc *flat) {
    Doc *doc = doc_new(DOC_IF_BREAK);
    doc->first = broken;
    doc->second = flat;
    return doc;
}

static Doc *doc_group(Doc *contents, int threshold) {
    Doc *doc = doc_unary(DOC_GROUP, contents);
    doc->threshold = threshold;
    return doc;
}

static int escaped_json_length(const char *text) {
    int length = 0;
    int index = 0;
    while (text[index]) {
        char ch = text[index++];
        if (ch == '"' || ch == '\\' || ch == '\n' || ch == '\r' || ch == '\t') length += 2;
        else if ((unsigned char)ch < 0x20) length += 6;
        else length++;
    }
    return length;
}

static int doc_flat_length(Doc *doc) {
    int index;
    int length = 0;
    if (!doc) return 0;
    if (doc->kind == DOC_TEXT) return text_length(doc->text);
    if (doc->kind == DOC_ESCAPED_TEXT) return escaped_json_length(doc->text);
    if (doc->kind == DOC_CONCAT) {
        for (index = 0; index < doc->count; index++) {
            int child_length = doc_flat_length(doc->parts[index]);
            if (child_length >= PRETTIER_LARGE_LENGTH - length) return PRETTIER_LARGE_LENGTH;
            length += child_length;
        }
        return length;
    }
    if (doc->kind == DOC_INDENT || doc->kind == DOC_GROUP) return doc_flat_length(doc->first);
    if (doc->kind == DOC_IF_BREAK) return doc_flat_length(doc->second);
    if (doc->kind == DOC_LINE) return 1;
    if (doc->kind == DOC_SOFTLINE) return 0;
    return PRETTIER_LARGE_LENGTH;
}

typedef struct {
    char text[PRETTIER_OUTPUT_CAPACITY];
    int length;
    int column;
    int indent;
} RenderState;

static void output_char(RenderState *state, char ch) {
    if (state->length + 1 < PRETTIER_OUTPUT_CAPACITY) state->text[state->length] = ch;
    state->length++;
}

static void output_text(RenderState *state, const char *text) {
    int index = 0;
    while (text && text[index]) output_char(state, text[index++]);
}

static void output_escaped_json(RenderState *state, const char *text) {
    int index = 0;
    while (text[index]) {
        char ch = text[index++];
        if (ch == '"') output_text(state, "\\\"");
        else if (ch == '\\') output_text(state, "\\\\");
        else if (ch == '\n') output_text(state, "\\n");
        else if (ch == '\r') output_text(state, "\\r");
        else if (ch == '\t') output_text(state, "\\t");
        else if ((unsigned char)ch < 0x20) {
            const char hex[] = "0123456789abcdef";
            output_text(state, "\\u00");
            output_char(state, hex[((unsigned char)ch >> 4) & 15]);
            output_char(state, hex[(unsigned char)ch & 15]);
        } else output_char(state, ch);
    }
}

static void render_doc(Doc *doc, RenderState *state, int flat) {
    int index;
    if (!doc) return;
    if (doc->kind == DOC_TEXT) {
        output_text(state, doc->text);
        state->column += text_length(doc->text);
    } else if (doc->kind == DOC_ESCAPED_TEXT) {
        output_escaped_json(state, doc->text);
        state->column += escaped_json_length(doc->text);
    } else if (doc->kind == DOC_CONCAT) {
        for (index = 0; index < doc->count; index++) render_doc(doc->parts[index], state, flat);
    } else if (doc->kind == DOC_INDENT) {
        state->indent++;
        render_doc(doc->first, state, flat);
        state->indent--;
    } else if (doc->kind == DOC_GROUP) {
        int use_flat = flat || (doc->threshold >= doc_flat_length(doc->first) &&
            doc_flat_length(doc->first) <= PRETTIER_PRINT_WIDTH - state->column);
        render_doc(doc->first, state, use_flat);
    } else if (doc->kind == DOC_IF_BREAK) {
        render_doc(flat ? doc->second : doc->first, state, flat);
    } else if (doc->kind == DOC_LINE && flat) {
        output_char(state, ' ');
        state->column++;
    } else if (doc->kind == DOC_SOFTLINE && flat) {
        /* flat softlines are empty */
    } else {
        int spaces = state->indent * 2;
        output_char(state, '\n');
        for (index = 0; index < spaces; index++) output_char(state, ' ');
        state->column = spaces;
    }
}

static Doc *print_node(Node *node);
static Doc *print_statement(Node *node);
static Doc *print_expression(Node *node, int parent_precedence);

static Doc *doc_join(Doc *separator, NodeList *nodes, int statement) {
    int count = nodes->count ? nodes->count * 2 - 1 : 0;
    Doc **parts = doc_part_list(count);
    int node_index;
    int part_index = 0;
    for (node_index = 0; node_index < nodes->count; node_index++) {
        if (node_index > 0) parts[part_index++] = separator;
        parts[part_index++] = statement ? print_statement(nodes->items[node_index]) :
            print_node(nodes->items[node_index]);
    }
    return doc_concat(parts, count);
}

static Doc *doc_line(void) { return doc_new(DOC_LINE); }
static Doc *doc_softline(void) { return doc_new(DOC_SOFTLINE); }
static Doc *doc_hardline(void) { return doc_new(DOC_HARDLINE); }

static Doc *doc_pair(Doc *first, Doc *second) {
    Doc **parts = doc_part_list(2);
    parts[0] = first;
    parts[1] = second;
    return doc_concat(parts, 2);
}

static Doc *doc_many(Doc **parts, int count) { return doc_concat(parts, count); }

static int node_is(Node *node, const char *type) {
    return node && text_equals(node->type, type);
}

static Doc *literal_doc(Node *node) {
    if (node_is(node, "StringLiteral")) {
        Doc **parts = doc_part_list(3);
        parts[0] = doc_text("\"");
        parts[1] = doc_escaped_text(node->value_text);
        parts[2] = doc_text("\"");
        return doc_many(parts, 3);
    }
    if (node_is(node, "NumericLiteral")) return doc_text(node->value_text);
    if (node_is(node, "BooleanLiteral")) return doc_text(node->value_bool ? "true" : "false");
    if (node_is(node, "NullLiteral")) return doc_text("null");
    {
        Doc **parts = doc_part_list(3);
        parts[0] = doc_pair(doc_text("/"), doc_text(node->pattern));
        parts[1] = doc_text("/");
        parts[2] = doc_text(node->flags);
        return doc_many(parts, 3);
    }
}

static Doc *concat_docs(Doc **parts, int count) { return doc_many(parts, count); }

static Doc *node_key(Node *node) {
    if (node->computed) {
        Doc **parts = doc_part_list(3);
        parts[0] = doc_text("[");
        parts[1] = print_node(node->key);
        parts[2] = doc_text("]");
        return concat_docs(parts, 3);
    }
    return print_node(node->key);
}

static Doc *parameter_list(NodeList *params) {
    Doc **comma_parts = doc_part_list(2);
    Doc **parts = doc_part_list(5);
    comma_parts[0] = doc_text(",");
    comma_parts[1] = doc_line();
    parts[0] = doc_text("(");
    parts[1] = doc_unary(DOC_INDENT, doc_pair(doc_softline(),
        doc_join(doc_concat(comma_parts, 2), params, 0)));
    parts[2] = doc_if_break(doc_text(","), doc_text(""));
    parts[3] = doc_softline();
    parts[4] = doc_text(")");
    return doc_group(doc_many(parts, 5), PRETTIER_LARGE_LENGTH);
}

static int has_object_argument(NodeList *arguments) {
    int index;
    for (index = 0; index < arguments->count; index++) {
        if (node_is(arguments->items[index], "ObjectExpression")) return 1;
    }
    return 0;
}

static Doc *argument_list(NodeList *arguments) {
    Doc **parts;
    if (arguments->count == 0) return doc_text("()");
    if (has_object_argument(arguments)) {
        parts = doc_part_list(3);
        parts[0] = doc_text("(");
        parts[1] = doc_join(doc_text(", "), arguments, 0);
        parts[2] = doc_text(")");
        return doc_many(parts, 3);
    }
    {
        Doc **comma_parts = doc_part_list(2);
        parts = doc_part_list(5);
        comma_parts[0] = doc_text(",");
        comma_parts[1] = doc_line();
        parts[0] = doc_text("(");
        parts[1] = doc_unary(DOC_INDENT, doc_pair(doc_softline(),
            doc_join(doc_concat(comma_parts, 2), arguments, 0)));
        parts[2] = doc_if_break(doc_text(","), doc_text(""));
        parts[3] = doc_softline();
        parts[4] = doc_text(")");
        return doc_group(doc_many(parts, 5), PRETTIER_LARGE_LENGTH);
    }
}

static Doc *array_doc(NodeList *elements) {
    Doc **parts;
    if (elements->count == 0) return doc_text("[]");
    {
        Doc **comma_parts = doc_part_list(2);
        parts = doc_part_list(5);
        comma_parts[0] = doc_text(",");
        comma_parts[1] = doc_line();
        parts[0] = doc_text("[");
        parts[1] = doc_unary(DOC_INDENT, doc_pair(doc_softline(),
            doc_join(doc_concat(comma_parts, 2), elements, 0)));
        parts[2] = doc_if_break(doc_text(","), doc_text(""));
        parts[3] = doc_softline();
        parts[4] = doc_text("]");
        return doc_group(doc_many(parts, 5), PRETTIER_LARGE_LENGTH);
    }
}

static Doc *object_doc(NodeList *properties) {
    Doc **parts;
    if (properties->count == 0) return doc_text("{}");
    {
        Doc **comma_parts = doc_part_list(2);
        Doc **inner_parts = doc_part_list(3);
        parts = doc_part_list(5);
        comma_parts[0] = doc_text(",");
        comma_parts[1] = doc_line();
        parts[0] = doc_text("{");
        inner_parts[0] = doc_line();
        inner_parts[1] = doc_join(doc_concat(comma_parts, 2), properties, 0);
        inner_parts[2] = doc_if_break(doc_text(","), doc_text(""));
        parts[1] = doc_unary(DOC_INDENT, doc_many(inner_parts, 3));
        parts[2] = doc_line();
        parts[3] = doc_text("}");
        return doc_group(doc_many(parts, 4), PRETTIER_LARGE_LENGTH);
    }
}

static Doc *block_doc(NodeList *body) {
    Doc **parts;
    if (body->count == 0) return doc_text("{}");
    parts = doc_part_list(4);
    parts[0] = doc_text("{");
    parts[1] = doc_unary(DOC_INDENT, doc_pair(doc_hardline(),
        doc_join(doc_hardline(), body, 1)));
    parts[2] = doc_hardline();
    parts[3] = doc_text("}");
    return doc_many(parts, 4);
}

static Doc *variable_doc(Node *node, int terminator) {
    Doc **comma_parts = doc_part_list(2);
    Doc **declaration_parts = doc_part_list(2);
    Doc *declaration;
    comma_parts[0] = doc_text(",");
    comma_parts[1] = doc_line();
    declaration_parts[0] = doc_pair(doc_text(node->kind), doc_text(" "));
    declaration_parts[1] = doc_join(doc_concat(comma_parts, 2), &node->declarations, 0);
    declaration = doc_many(declaration_parts, 2);
    return terminator ? doc_pair(declaration, doc_text(";")) : declaration;
}

static Doc *print_property(Node *node) {
    if (node_is(node, "SpreadElement")) return doc_pair(doc_text("..."), print_node(node->argument));
    if (node->shorthand) return node_key(node);
    {
        Doc **parts = doc_part_list(3);
        parts[0] = node_key(node);
        parts[1] = doc_text(": ");
        parts[2] = print_expression(node->value, 0);
        return doc_many(parts, 3);
    }
}

static int expression_precedence(Node *node) {
    const char *op;
    if (!node) return 100;
    if (node_is(node, "AssignmentExpression") || node_is(node, "ArrowFunctionExpression")) return 1;
    if (node_is(node, "LogicalExpression")) return text_equals(node->operator_text, "&&") ? 3 : 2;
    if (!node_is(node, "BinaryExpression")) return 20;
    op = node->operator_text;
    if (text_equals(op, "*") || text_equals(op, "/") || text_equals(op, "%")) return 12;
    if (text_equals(op, "+") || text_equals(op, "-")) return 11;
    if (text_equals(op, "<") || text_equals(op, "<=") || text_equals(op, ">") ||
            text_equals(op, ">=") || text_equals(op, "in") || text_equals(op, "instanceof")) return 9;
    if (text_equals(op, "==") || text_equals(op, "!=") || text_equals(op, "===") ||
            text_equals(op, "!==")) return 8;
    return 7;
}

static Doc *print_expression(Node *node, int parent_precedence) {
    Doc *doc = print_node(node);
    if (expression_precedence(node) < parent_precedence) {
        Doc **parts = doc_part_list(3);
        parts[0] = doc_text("(");
        parts[1] = doc;
        parts[2] = doc_text(")");
        return doc_many(parts, 3);
    }
    return doc;
}

static void collect_additive_operands(Node *node, NodeList *operands) {
    if (node_is(node, "BinaryExpression") && text_equals(node->operator_text, "+")) {
        collect_additive_operands(node->left, operands);
        node_list_add(operands, node->right);
    } else node_list_add(operands, node);
}

static Doc *additive_chain_doc(Node *node) {
    NodeList operands = {0};
    Doc **tail;
    Doc **parts;
    int index;
    collect_additive_operands(node, &operands);
    tail = doc_part_list((operands.count - 1) * 3);
    for (index = 1; index < operands.count; index++) {
        tail[(index - 1) * 3] = doc_text(" +");
        tail[(index - 1) * 3 + 1] = doc_line();
        tail[(index - 1) * 3 + 2] = print_expression(operands.items[index], 12);
    }
    parts = doc_part_list(2);
    parts[0] = print_expression(operands.items[0], 11);
    parts[1] = doc_unary(DOC_INDENT, doc_many(tail, (operands.count - 1) * 3));
    return doc_group(doc_many(parts, 2), 60);
}

static Doc *print_node(Node *node) {
    Doc **parts;
    int precedence;
    if (!node) return doc_text("");
    if (node_is(node, "Identifier")) return doc_text(node->name);
    if (node_is(node, "ThisExpression")) return doc_text("this");
    if (node_is(node, "StringLiteral") || node_is(node, "NumericLiteral") ||
            node_is(node, "BooleanLiteral") || node_is(node, "NullLiteral") ||
            node_is(node, "RegExpLiteral")) return literal_doc(node);
    if (node_is(node, "ArrayExpression") || node_is(node, "ArrayPattern")) return array_doc(&node->elements);
    if (node_is(node, "ObjectExpression")) return object_doc(&node->properties);
    if (node_is(node, "ObjectProperty")) return print_property(node);
    if (node_is(node, "VariableDeclarator")) {
        if (!node->init) return print_node(node->id);
        parts = doc_part_list(3);
        parts[0] = print_node(node->id);
        parts[1] = doc_text(" = ");
        parts[2] = print_expression(node->init, 0);
        return doc_many(parts, 3);
    }
    if (node_is(node, "SpreadElement")) return doc_pair(doc_text("..."), print_node(node->argument));
    if (node_is(node, "AssignmentPattern")) {
        parts = doc_part_list(3);
        parts[0] = print_node(node->left);
        parts[1] = doc_text(" = ");
        parts[2] = print_node(node->right);
        return doc_many(parts, 3);
    }
    if (node_is(node, "MemberExpression")) {
        parts = doc_part_list(4);
        parts[0] = print_expression(node->object, 20);
        parts[1] = doc_text(node->computed ? "[" : ".");
        parts[2] = node->computed ? print_expression(node->property, 0) : print_node(node->property);
        parts[3] = doc_text(node->computed ? "]" : "");
        return doc_many(parts, 4);
    }
    if (node_is(node, "CallExpression")) {
        if (node->arguments.count == 1 && node_is(node->arguments.items[0], "ArrowFunctionExpression")) {
            Node *arrow = node->arguments.items[0];
            parts = doc_part_list(8);
            parts[0] = print_expression(node->callee, 20);
            parts[1] = doc_text("(");
            parts[2] = parameter_list(&arrow->params);
            parts[3] = doc_text(" =>");
            parts[4] = doc_unary(DOC_INDENT, doc_pair(doc_line(), print_expression(arrow->body_node, 0)));
            parts[5] = doc_if_break(doc_text(","), doc_text(""));
            parts[6] = doc_softline();
            parts[7] = doc_text(")");
            return doc_group(doc_many(parts, 8), PRETTIER_LARGE_LENGTH);
        }
        return doc_pair(print_expression(node->callee, 20), argument_list(&node->arguments));
    }
    if (node_is(node, "NewExpression")) {
        parts = doc_part_list(3);
        parts[0] = doc_text("new ");
        parts[1] = print_expression(node->callee, 20);
        parts[2] = argument_list(&node->arguments);
        return doc_many(parts, 3);
    }
    if (node_is(node, "BinaryExpression") || node_is(node, "LogicalExpression")) {
        precedence = expression_precedence(node);
        if (node_is(node, "BinaryExpression") && text_equals(node->operator_text, "+") &&
                node_is(node->left, "BinaryExpression") && text_equals(node->left->operator_text, "+")) {
            return additive_chain_doc(node);
        }
        parts = doc_part_list(3);
        parts[0] = print_expression(node->left, precedence);
        parts[1] = doc_pair(doc_text(" "), doc_text(node->operator_text));
        parts[2] = doc_unary(DOC_INDENT, doc_pair(doc_line(), print_expression(node->right,
            precedence + (text_equals(node->operator_text, "&&") || text_equals(node->operator_text, "||") ? 1 : 0))));
        return doc_group(doc_many(parts, 3), PRETTIER_LARGE_LENGTH);
    }
    if (node_is(node, "UnaryExpression")) {
        if (text_equals(node->operator_text, "!")) {
            return doc_pair(doc_text("!"), print_expression(node->argument, 20));
        }
        parts = doc_part_list(2);
        parts[0] = doc_pair(doc_text(node->operator_text), doc_text(" "));
        parts[1] = print_expression(node->argument, 20);
        return doc_many(parts, 2);
    }
    if (node_is(node, "AssignmentExpression")) {
        Doc **operator_parts = doc_part_list(3);
        parts = doc_part_list(3);
        parts[0] = print_expression(node->left, 2);
        operator_parts[0] = doc_text(" ");
        operator_parts[1] = doc_text(node->operator_text);
        operator_parts[2] = doc_text(" ");
        parts[1] = doc_many(operator_parts, 3);
        parts[2] = print_expression(node->right, 1);
        return doc_many(parts, 3);
    }
    if (node_is(node, "ArrowFunctionExpression")) {
        parts = doc_part_list(3);
        parts[0] = parameter_list(&node->params);
        parts[1] = doc_text(" => ");
        parts[2] = print_expression(node->body_node, 1);
        return doc_many(parts, 3);
    }
    if (node_is(node, "FunctionDeclaration")) {
        parts = doc_part_list(6);
        parts[0] = doc_text(node->async ? "async function " : "function ");
        parts[1] = doc_text(node->generator ? "*" : "");
        parts[2] = print_node(node->id);
        parts[3] = parameter_list(&node->params);
        parts[4] = doc_text(" ");
        parts[5] = block_doc(&node->body_node->body);
        return doc_many(parts, 6);
    }
    if (node_is(node, "ClassDeclaration")) {
        parts = doc_part_list(4);
        parts[0] = doc_text("class ");
        parts[1] = print_node(node->id);
        parts[2] = doc_text(" ");
        parts[3] = print_node(node->body_node);
        return doc_many(parts, 4);
    }
    if (node_is(node, "ClassBody")) {
        if (node->body.count == 0) return doc_text("{}");
        parts = doc_part_list(4);
        parts[0] = doc_text("{");
        parts[1] = doc_unary(DOC_INDENT, doc_pair(doc_hardline(), doc_join(doc_hardline(), &node->body, 0)));
        parts[2] = doc_hardline();
        parts[3] = doc_text("}");
        return doc_many(parts, 4);
    }
    if (node_is(node, "ClassMethod")) {
        parts = doc_part_list(7);
        parts[0] = doc_text(node->is_static ? "static " : "");
        parts[1] = doc_text(node->async ? "async " : "");
        parts[2] = doc_text(node->generator ? "*" : "");
        parts[3] = node_key(node);
        parts[4] = parameter_list(&node->params);
        parts[5] = doc_text(" ");
        parts[6] = block_doc(&node->body_node->body);
        return doc_many(parts, 7);
    }
    return print_statement(node);
}

static Doc *print_statement(Node *node) {
    Doc **parts;
    if (!node) return doc_text("");
    if (node_is(node, "VariableDeclaration")) return variable_doc(node, 1);
    if (node_is(node, "ReturnStatement")) {
        parts = doc_part_list(3);
        parts[0] = doc_text("return");
        parts[1] = node->argument ? doc_pair(doc_text(" "), print_node(node->argument)) : doc_text("");
        parts[2] = doc_text(";");
        return doc_many(parts, 3);
    }
    if (node_is(node, "ExpressionStatement")) return doc_pair(print_node(node->argument), doc_text(";"));
    if (node_is(node, "BlockStatement")) return block_doc(&node->body);
    if (node_is(node, "IfStatement")) {
        if (node_is(node->consequent, "BlockStatement")) {
            parts = doc_part_list(5);
            parts[0] = doc_text("if (");
            parts[1] = print_expression(node->test, 0);
            parts[2] = doc_text(") ");
            parts[3] = print_statement(node->consequent);
            parts[4] = node->alternate ? doc_pair(doc_text(" else "), print_statement(node->alternate)) : doc_text("");
            return doc_many(parts, 5);
        }
        {
        Doc **alternate_parts = doc_part_list(4);
        parts = doc_part_list(5);
        parts[0] = doc_text("if (");
        parts[1] = print_expression(node->test, 0);
        parts[2] = doc_text(")");
        parts[3] = doc_unary(DOC_INDENT, doc_pair(doc_line(), print_statement(node->consequent)));
        alternate_parts[0] = doc_line();
        alternate_parts[1] = doc_text("else");
        alternate_parts[2] = doc_line();
        alternate_parts[3] = print_statement(node->alternate);
        parts[4] = node->alternate ? doc_unary(DOC_INDENT,
            doc_many(alternate_parts, 4)) : doc_text("");
        return doc_group(doc_many(parts, 5), PRETTIER_LARGE_LENGTH);
        }
    }
    if (node_is(node, "ForOfStatement")) {
        parts = doc_part_list(6);
        parts[0] = doc_text("for (");
        parts[1] = variable_doc(node->left, 0);
        parts[2] = doc_text(" of ");
        parts[3] = print_node(node->right);
        parts[4] = doc_text(") ");
        parts[5] = print_statement(node->body_node);
        return doc_many(parts, 6);
    }
    if (node_is(node, "FunctionDeclaration") || node_is(node, "ClassDeclaration")) return print_node(node);
    if (node_is(node, "ExportNamedDeclaration")) {
        if (node->declaration) return doc_pair(doc_text("export "), print_statement(node->declaration));
        parts = doc_part_list(3);
        parts[0] = doc_text("export { ");
        parts[1] = doc_join(doc_text(", "), &node->specifiers, 0);
        parts[2] = doc_text(" };");
        return doc_many(parts, 3);
    }
    if (node_is(node, "ExportSpecifier")) {
        if (node->local && node->exported && text_equals(node->local->name, node->exported->name)) {
            return print_node(node->local);
        }
        parts = doc_part_list(3);
        parts[0] = print_node(node->local);
        parts[1] = doc_text(" as ");
        parts[2] = print_node(node->exported);
        return doc_many(parts, 3);
    }
    return doc_text("");
}

static Doc *print_program(Node *program) {
    Doc **parts = doc_part_list(2);
    parts[0] = doc_join(doc_hardline(), &program->body, 1);
    parts[1] = doc_hardline();
    return doc_many(parts, 2);
}

static int checksum_text(RenderState *state) {
    long checksum = 0;
    int index;
    for (index = 0; index < state->length; index++) {
        checksum = (checksum * 31 + (unsigned char)state->text[index]) % 1000000007;
    }
    return (int)checksum;
}

static char *read_ast_file(void) {
    void *file = fopen("test/benchmark/text/prettier_ast.json", "r");
    long length;
    char *input;
    if (!file) return 0;
    fseek(file, 0, 2);
    length = ftell(file);
    fseek(file, 0, 0);
    input = (char *)malloc((unsigned long)length + 1);
    fread(input, 1, (unsigned long)length, file);
    fclose(file);
    input[length] = 0;
    return input;
}

int main(void) {
    AstReader reader;
    Node *program;
    RenderState output = {{0}, 0, 0, 0};
    int iteration;
    int checksum;
    reader.input = read_ast_file();
    reader.index = 0;
    if (!reader.input) {
        printf("prettier_ast: FAIL missing input\n");
        return 1;
    }
    program = reader_node(&reader);
    for (iteration = 0; iteration < PRETTIER_ITERATIONS; iteration++) {
        Doc *doc;
        doc_count = 0;
        doc_link_count = 0;
        output.length = 0;
        output.column = 0;
        output.indent = 0;
        doc = print_program(program);
        render_doc(doc, &output, 0);
        output.text[output.length] = 0;
    }
    checksum = checksum_text(&output);
    if (checksum == 56483873) {
        printf("prettier_ast: CHECKSUM:%d\n", checksum);
        return 0;
    }
    printf("prettier_ast: FAIL checksum=%d\n", checksum);
    return 1;
}
