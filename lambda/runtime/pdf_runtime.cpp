#include "../lambda-data.hpp"
#include "../io/pdf_content_stream.h"

extern __thread EvalContext* context;

extern "C" Item pdf_parse_content_stream(Item bytes_item) {
    return context && context->pool ? pdf_parse_content_stream_io(context->pool, bytes_item) : ItemNull;
}

extern "C" Item fn_pdf_parse_content_stream(Item bytes_item) {
    return pdf_parse_content_stream(bytes_item);
}
