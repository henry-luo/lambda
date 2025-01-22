#include "lib/string_buffer/string_buffer.h"

int main() {
    StrBuf* myBuff = strbuf_new(100);
    strbuf_append_str(myBuff, "Hello, ");
    strbuf_sprintf(myBuff, "%s %i", "world", 42);
    return 0;
}
