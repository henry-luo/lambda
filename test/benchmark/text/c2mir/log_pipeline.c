/* Native C2MIR port of text/log_pipeline.ls.
 *
 * Parses a fixed corpus of mixed-format log lines into records, then filters,
 * groups and aggregates them once per round.
 */
extern int printf(const char *, ...);

#define LOG_ROUNDS 180
#define LOG_COUNT 12000
#define MODULUS 1000000007L
#define MAX_LINE 192
#define MAX_FIELDS 16
#define MAX_TOKEN 64

static char log_lines[LOG_COUNT][MAX_LINE];

typedef struct {
    char timestamp[32];
    char level[16];
    char service[16];
    long status;
    long latency;
    char region[16];
    char route[16];
    long bytes;
    char message[32];
} LogRecord;

typedef struct {
    long count;
    long errors;
    long slow;
    long total_latency;
    long total_bytes;
} Group;

static const char *services[4] = {"api", "worker", "db", "cache"};
static const char *regions[3] = {"us-east", "eu-west", "ap-south"};

static int str_equal(const char *a, const char *b) {
    int i = 0;
    while (a[i] != 0 && a[i] == b[i]) i++;
    return a[i] == b[i];
}

static void str_copy(char *out, const char *text) {
    int i = 0;
    while (text[i] != 0) { out[i] = text[i]; i++; }
    out[i] = 0;
}

static int append_text(char *out, int at, const char *text) {
    int i = 0;
    while (text[i] != 0) out[at++] = text[i++];
    out[at] = 0;
    return at;
}

static int append_int(char *out, int at, long value) {
    char digits[24];
    int n = 0;
    if (value == 0) { out[at++] = '0'; out[at] = 0; return at; }
    while (value > 0) { digits[n++] = (char) ('0' + value % 10); value /= 10; }
    while (n > 0) out[at++] = digits[--n];
    out[at] = 0;
    return at;
}

static int append_pad2(char *out, int at, long value) {
    if (value < 10) at = append_text(out, at, "0");
    return append_int(out, at, value);
}

static long parse_int(const char *text) {
    long value = 0;
    int i = 0;
    int negative = 0;
    if (text[0] == '-') { negative = 1; i = 1; }
    while (text[i] >= '0' && text[i] <= '9') {
        value = value * 10 + (long) (text[i] - '0');
        i++;
    }
    return negative ? -value : value;
}

static void make_log_line(int index, char *line) {
    const char *level = index % 13 == 0 ? "ERROR" : (index % 5 == 0 ? "WARN" : "INFO");
    const char *service = services[index % 4];
    const char *region = regions[(index * 3) % 3];
    long status = index % 19 == 0 ? 503 : (index % 7 == 0 ? 404 : 200);
    long latency = (long) (index * 37) % 900 + 4;
    long bytes = (long) (index * 113) % 50000 + 512;
    const char *route = index % 2 == 0 ? "/v1/items" : "/v1/search";
    const char *message = index % 11 == 0 ? "retry-scheduled" : "request-complete";
    int at = append_text(line, 0, "2026-09-07T");
    at = append_pad2(line, at, index % 24);
    at = append_text(line, at, ":");
    at = append_pad2(line, at, index % 60);
    at = append_text(line, at, ":");
    at = append_pad2(line, at, (index * 7) % 60);
    at = append_text(line, at, "Z");
    if (index % 3 == 0) {
        at = append_text(line, at, " level=");
        at = append_text(line, at, level);
        at = append_text(line, at, " service=");
        at = append_text(line, at, service);
    } else {
        at = append_text(line, at, " ");
        at = append_text(line, at, level);
        at = append_text(line, at, " ");
        at = append_text(line, at, service);
    }
    at = append_text(line, at, " status=");
    at = append_int(line, at, status);
    at = append_text(line, at, " latency=");
    at = append_int(line, at, latency);
    at = append_text(line, at, " region=");
    at = append_text(line, at, region);
    at = append_text(line, at, " route=");
    at = append_text(line, at, route);
    at = append_text(line, at, " bytes=");
    at = append_int(line, at, bytes);
    at = append_text(line, at, " message=");
    append_text(line, at, message);
}

static void build_logs(void) {
    int index;
    for (index = 0; index < LOG_COUNT; index++) make_log_line(index, log_lines[index]);
}

static void set_field(LogRecord *record, const char *key, const char *value) {
    if (str_equal(key, "level")) str_copy(record->level, value);
    else if (str_equal(key, "service")) str_copy(record->service, value);
    else if (str_equal(key, "status")) record->status = parse_int(value);
    else if (str_equal(key, "latency")) record->latency = parse_int(value);
    else if (str_equal(key, "region")) str_copy(record->region, value);
    else if (str_equal(key, "route")) str_copy(record->route, value);
    else if (str_equal(key, "bytes")) record->bytes = parse_int(value);
    else if (str_equal(key, "message")) str_copy(record->message, value);
}

/* split on a single space, matching Lambda's split(line, " ") */
static int split_fields(const char *line, char fields[MAX_FIELDS][MAX_TOKEN]) {
    int count = 0;
    int at = 0;
    int i = 0;
    for (;;) {
        char c = line[i];
        if (c == 0 || c == ' ') {
            fields[count][at] = 0;
            count++;
            at = 0;
            if (c == 0) break;
        } else {
            fields[count][at++] = c;
        }
        i++;
    }
    return count;
}

static int index_of_equals(const char *token) {
    int i = 0;
    while (token[i] != 0) {
        if (token[i] == '=') return i;
        i++;
    }
    return -1;
}

static void parse_log_line(const char *line, LogRecord *record) {
    static char fields[MAX_FIELDS][MAX_TOKEN];
    char key[MAX_TOKEN];
    char value[MAX_TOKEN];
    int count = split_fields(line, fields);
    int field_start = 1;
    int index;
    str_copy(record->timestamp, fields[0]);
    record->level[0] = 0;
    record->service[0] = 0;
    record->status = 0;
    record->latency = 0;
    record->region[0] = 0;
    record->route[0] = 0;
    record->bytes = 0;
    record->message[0] = 0;
    if (index_of_equals(fields[1]) < 0) {
        str_copy(record->level, fields[1]);
        str_copy(record->service, fields[2]);
        field_start = 3;
    }
    for (index = field_start; index < count; index++) {
        const char *token = fields[index];
        int separator = index_of_equals(token);
        int i;
        if (separator < 0) continue;
        for (i = 0; i < separator; i++) key[i] = token[i];
        key[separator] = 0;
        i = separator + 1;
        {
            int at = 0;
            while (token[i] != 0) value[at++] = token[i++];
            value[at] = 0;
        }
        set_field(record, key, value);
    }
}

static void add_to_group(Group *group, const LogRecord *record) {
    group->count++;
    group->total_latency += record->latency;
    group->total_bytes += record->bytes;
    if (record->latency >= 500) group->slow++;
}

int main(void) {
    long checksum = 0;
    int pass;
    build_logs();
    for (pass = 0; pass < LOG_ROUNDS; pass++) {
        Group api = {0, 0, 0, 0, 0};
        Group worker = {0, 0, 0, 0, 0};
        Group db = {0, 0, 0, 0, 0};
        Group cache = {0, 0, 0, 0, 0};
        LogRecord record;
        long accepted = 0;
        long rejected = 0;
        int index;
        for (index = 0; index < LOG_COUNT; index++) {
            parse_log_line(log_lines[index], &record);
            if (record.status >= 500 || str_equal(record.level, "ERROR")) {
                rejected++;
            } else {
                if (str_equal(record.service, "api")) add_to_group(&api, &record);
                else if (str_equal(record.service, "worker")) add_to_group(&worker, &record);
                else if (str_equal(record.service, "db")) add_to_group(&db, &record);
                else add_to_group(&cache, &record);
                accepted++;
            }
        }
        checksum = (checksum + accepted * 31 + rejected * 17 +
                    api.total_latency + worker.total_bytes + (long) pass) % MODULUS;
    }
    printf("log_pipeline: CHECKSUM:%ld\n", checksum);
    return checksum != 292634526;
}
