#ifndef LAMBDA_ARCHIVE_CODEC_H
#define LAMBDA_ARCHIVE_CODEC_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <zlib.h>
#include <lzma.h>
#include <bzlib.h>
#include <zstd.h>
#include <iconv.h>

#ifdef __cplusplus
extern "C" {
#endif

// native reader and codec allocations share explicit per-archive quota ownership.
typedef struct ArchiveCodecBudget {
    void* context;
    void* allocations;
    void* resources;
    const char* failure_message;
    bool (*charge_expansion)(void* context, uint64_t bytes);
    uint64_t* index_bytes;
    uint64_t index_limit;
    uint64_t decoder_bytes;
    uint64_t decoder_limit;
    uint64_t codec_output;
    uint64_t delivered_output;
    uint64_t charged_output;
    bool format_output;
    bool metadata;
    bool limited;
} ArchiveCodecBudget;

#ifdef __cplusplus
[[noreturn]] void lambda_archive_fatal(int status, const char* message);
#else
_Noreturn void lambda_archive_fatal(int status, const char* message);
#endif
typedef bool (*ArchiveCodecAction)(void* context);
bool archive_codec_run(ArchiveCodecBudget* budget, ArchiveCodecAction action, void* context);
void archive_codec_discard(ArchiveCodecBudget* budget);
iconv_t lambda_archive_iconv_open(const char* to, const char* from);
int lambda_archive_iconv_close(iconv_t descriptor);
ArchiveCodecBudget* archive_codec_scope(ArchiveCodecBudget* budget);
void archive_codec_phase(ArchiveCodecBudget* budget, bool metadata);
bool lambda_archive_format_output(size_t bytes);
bool lambda_archive_7zip_codec(uint64_t codec);
bool archive_codec_delivered(ArchiveCodecBudget* budget, size_t bytes);
void* lambda_archive_malloc(size_t bytes);
void* lambda_archive_calloc(size_t count, size_t bytes);
void* lambda_archive_realloc(void* pointer, size_t bytes);
void lambda_archive_free(void* pointer);
char* lambda_archive_strdup(const char* text);
int lambda_archive_inflate_init(z_streamp stream, int window_bits, const char* version, int size);
int lambda_archive_inflate(z_streamp stream, int flush);
lzma_ret lambda_archive_lzma_properties(lzma_filter* filter, const lzma_allocator* allocator,
    const uint8_t* properties, size_t size);
lzma_ret lambda_archive_lzma_raw(lzma_stream* stream, const lzma_filter* filters);
lzma_ret lambda_archive_lzma_stream(lzma_stream* stream, uint64_t limit, uint32_t flags);
lzma_ret lambda_archive_lzma_alone(lzma_stream* stream, uint64_t limit);
lzma_ret lambda_archive_lzma_code(lzma_stream* stream, lzma_action action);
int lambda_archive_bzip_init(bz_stream* stream, int verbosity, int small);
int lambda_archive_bzip_decode(bz_stream* stream);
ZSTD_DStream* lambda_archive_zstd_create(void);
size_t lambda_archive_zstd_decode(ZSTD_DStream* stream, ZSTD_outBuffer* output, ZSTD_inBuffer* input);

#ifdef __cplusplus
}
#endif
#endif
