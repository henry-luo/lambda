/* Native C2MIR port of text/log_pipeline.ls. */
extern int printf(const char *, ...);

#define LOG_ROUNDS 180
#define LOG_COUNT 12000
#define LOG_LINE_CAPACITY 192
#define LOG_MODULUS 1000000007

typedef struct {
    int is_error;
    int service;
    int status;
    int latency;
    int bytes;
} Record;

typedef struct {
    int count;
    int slow;
    int total_latency;
    int total_bytes;
} Group;

static char log_lines[LOG_COUNT][LOG_LINE_CAPACITY];
static int log_lengths[LOG_COUNT];

static void append_char(char *line, int *length, char value) {
    line[*length] = value;
    *length = *length + 1;
}

static void append_text(char *line, int *length, const char *text) {
    int index = 0;
    while (text[index] != 0) {
        append_char(line, length, text[index]);
        index++;
    }
}

static void append_decimal(char *line, int *length, int value) {
    int divisor = 1;
    while (value / divisor >= 10) divisor *= 10;
    while (divisor > 0) {
        append_char(line, length, '0' + value / divisor);
        value %= divisor;
        divisor /= 10;
    }
}

static void append_pad2(char *line, int *length, int value) {
    append_char(line, length, '0' + value / 10);
    append_char(line, length, '0' + value % 10);
}

static const char *log_level(int index) {
    if (index % 13 == 0) return "ERROR";
    if (index % 5 == 0) return "WARN";
    return "INFO";
}

static int log_status(int index) {
    if (index % 19 == 0) return 503;
    if (index % 7 == 0) return 404;
    return 200;
}

static const char *service_name(int index) {
    static const char *services[4] = {"api", "worker", "db", "cache"};
    return services[index % 4];
}

static const char *route_name(int index) {
    return index % 2 == 0 ? "/v1/items" : "/v1/search";
}

static const char *message_name(int index) {
    return index % 11 == 0 ? "retry-scheduled" : "request-complete";
}

static void build_logs(void) {
    int index;
    for (index = 0; index < LOG_COUNT; index++) {
        char *line = log_lines[index];
        int length = 0;
        int status = log_status(index);
        int latency = (index * 37) % 900 + 4;
        int bytes = (index * 113) % 50000 + 512;
        append_text(line, &length, "2026-09-07T");
        append_pad2(line, &length, index % 24);
        append_char(line, &length, ':');
        append_pad2(line, &length, index % 60);
        append_char(line, &length, ':');
        append_pad2(line, &length, (index * 7) % 60);
        append_char(line, &length, 'Z');
        if (index % 3 == 0) {
            append_text(line, &length, " level=");
            append_text(line, &length, log_level(index));
            append_text(line, &length, " service=");
            append_text(line, &length, service_name(index));
        } else {
            append_char(line, &length, ' ');
            append_text(line, &length, log_level(index));
            append_char(line, &length, ' ');
            append_text(line, &length, service_name(index));
        }
        append_text(line, &length, " status=");
        append_decimal(line, &length, status);
        append_text(line, &length, " latency=");
        append_decimal(line, &length, latency);
        append_text(line, &length, " region=us-east route=");
        append_text(line, &length, route_name(index));
        append_text(line, &length, " bytes=");
        append_decimal(line, &length, bytes);
        append_text(line, &length, " message=");
        append_text(line, &length, message_name(index));
        log_lengths[index] = length;
    }
}

static int text_equals(const char *line, int start, int length, const char *text) {
    int index = 0;
    while (text[index] != 0) {
        if (index >= length || line[start + index] != text[index]) return 0;
        index++;
    }
    return index == length;
}

static int parse_decimal(const char *line, int start, int length) {
    int value = 0;
    int index;
    for (index = 0; index < length; index++) value = value * 10 + line[start + index] - '0';
    return value;
}

static int service_id(const char *line, int start, int length) {
    if (text_equals(line, start, length, "api")) return 0;
    if (text_equals(line, start, length, "worker")) return 1;
    if (text_equals(line, start, length, "db")) return 2;
    return 3;
}

static int next_token(const char *line, int length, int *cursor, int *start) {
    int token_length = 0;
    while (*cursor < length && line[*cursor] == ' ') *cursor = *cursor + 1;
    *start = *cursor;
    while (*cursor < length && line[*cursor] != ' ') {
        *cursor = *cursor + 1;
        token_length++;
    }
    return token_length;
}

static void parse_log_line(const char *line, int length, Record *record) {
    int cursor = 0;
    int start;
    int token_length;
    int equal_at;
    record->is_error = 0;
    record->service = 0;
    record->status = 0;
    record->latency = 0;
    record->bytes = 0;

    next_token(line, length, &cursor, &start); /* skip the timestamp */
    token_length = next_token(line, length, &cursor, &start);
    equal_at = 0;
    while (equal_at < token_length && line[start + equal_at] != '=') equal_at++;
    if (equal_at == token_length) {
        record->is_error = text_equals(line, start, token_length, "ERROR");
        token_length = next_token(line, length, &cursor, &start);
        record->service = service_id(line, start, token_length);
    } else {
        record->is_error = text_equals(line, start + equal_at + 1,
                                       token_length - equal_at - 1, "ERROR");
    }
    while (cursor < length) {
        token_length = next_token(line, length, &cursor, &start);
        equal_at = 0;
        while (equal_at < token_length && line[start + equal_at] != '=') equal_at++;
        if (equal_at == token_length) continue;
        if (text_equals(line, start, equal_at, "level")) {
            record->is_error = text_equals(line, start + equal_at + 1,
                                           token_length - equal_at - 1, "ERROR");
        } else if (text_equals(line, start, equal_at, "service")) {
            record->service = service_id(line, start + equal_at + 1,
                                         token_length - equal_at - 1);
        } else if (text_equals(line, start, equal_at, "status")) {
            record->status = parse_decimal(line, start + equal_at + 1,
                                           token_length - equal_at - 1);
        } else if (text_equals(line, start, equal_at, "latency")) {
            record->latency = parse_decimal(line, start + equal_at + 1,
                                            token_length - equal_at - 1);
        } else if (text_equals(line, start, equal_at, "bytes")) {
            record->bytes = parse_decimal(line, start + equal_at + 1,
                                          token_length - equal_at - 1);
        }
    }
}

static void add_to_group(Group *group, Record *record) {
    group->count++;
    group->total_latency += record->latency;
    group->total_bytes += record->bytes;
    if (record->latency >= 500) group->slow++;
}

static int process_logs(void) {
    Group groups[4] = {{0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}};
    int accepted = 0;
    int rejected = 0;
    int index;
    for (index = 0; index < LOG_COUNT; index++) {
        Record record;
        parse_log_line(log_lines[index], log_lengths[index], &record);
        if (record.status >= 500 || record.is_error) {
            rejected++;
        } else {
            add_to_group(&groups[record.service], &record);
            accepted++;
        }
    }
    return accepted * 31 + rejected * 17 + groups[0].total_latency + groups[1].total_bytes;
}

int main(void) {
    int checksum = 0;
    int pass;
    build_logs();
    for (pass = 0; pass < LOG_ROUNDS; pass++) {
        checksum = (checksum + process_logs() + pass) % LOG_MODULUS;
    }
    if (checksum == 292634526) {
        printf("log_pipeline: CHECKSUM:%d\n", checksum);
        return 0;
    }
    printf("log_pipeline: FAIL checksum=%d\n", checksum);
    return 1;
}
