#include <stdio.h>
#include <string.h>
#include <tree_sitter/api.h>
#include "../lib/string_buffer/string_buffer.h"

int main(void) {
    StrBuf* myBuff = strbuf_new(100);
    strbuf_append_str(myBuff, "Hello, ");
    strbuf_sprintf(myBuff, "%s %i", "world", 42);
    printf("%s\n", myBuff->b);
    return 0;
}
