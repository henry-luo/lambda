#define ZSTD_STATIC_LINKING_ONLY
#include "codec.h"
#include "../memtrack.h"
#include <stdlib.h>
#include <errno.h>
#include <setjmp.h>
#include <string.h>

static _Thread_local ArchiveCodecBudget* active_archive_budget;

typedef struct ArchiveCodecRecovery {
    jmp_buf target;
    struct ArchiveCodecRecovery* previous;
} ArchiveCodecRecovery;
static _Thread_local ArchiveCodecRecovery* active_archive_recovery;

_Noreturn void lambda_archive_fatal(int status, const char* message) {
    (void)status;
    // the C recovery frame never crosses a C++ object with a nontrivial destructor.
    if (!active_archive_recovery || !active_archive_budget) abort();
    active_archive_budget->failure_message = message;
    longjmp(active_archive_recovery->target, 1);
}

bool archive_codec_run(ArchiveCodecBudget* budget, ArchiveCodecAction action, void* context) {
    ArchiveCodecRecovery recovery;
    recovery.previous = active_archive_recovery;
    ArchiveCodecBudget* previous = archive_codec_scope(budget);
    budget->failure_message = NULL;
    active_archive_recovery = &recovery;
    bool ok = false;
    if (setjmp(recovery.target) == 0) ok = action(context);
    active_archive_recovery = recovery.previous;
    archive_codec_scope(previous);
    return ok;
}


typedef struct ArchiveCodecAllocation {
    max_align_t alignment;
    struct ArchiveCodecAllocation* next;
    struct ArchiveCodecAllocation* previous;
    ArchiveCodecBudget* owner;
    size_t bytes;
    bool decoder;
} ArchiveCodecAllocation;

ArchiveCodecBudget* archive_codec_scope(ArchiveCodecBudget* budget) {
    ArchiveCodecBudget* previous = active_archive_budget;
    active_archive_budget = budget;
    return previous;
}

void archive_codec_phase(ArchiveCodecBudget* budget, bool metadata) {
    budget->metadata = metadata;
    budget->codec_output = budget->delivered_output = budget->charged_output = 0;
}

static bool archive_codec_record(ArchiveCodecBudget* budget, size_t bytes, bool codec) {
    if (!budget || budget->limited) return false;
    uint64_t* count = codec ? &budget->codec_output : &budget->delivered_output;
    if (bytes > UINT64_MAX - *count) { budget->limited = true; return false; }
    *count += bytes;
    uint64_t total = budget->codec_output > budget->delivered_output
        ? budget->codec_output : budget->delivered_output;
    uint64_t charge = total - budget->charged_output;
    if (budget->metadata) {
        if (total > budget->index_limit) { budget->limited = true; return false; }
    } else if (charge && !budget->charge_expansion(budget->context, charge)) {
        budget->limited = true;
        return false;
    }
    budget->charged_output = total;
    return true;
}

bool lambda_archive_format_output(size_t bytes) {
    if (archive_codec_record(active_archive_budget, bytes, true)) return true;
    // some format callers discard callback status; unwind before decoding more blocks.
    lambda_archive_fatal(1, "archive expansion limit exceeded");
}

bool lambda_archive_7zip_codec(uint64_t codec) {
    // admit the documented Copy/LZMA/LZMA2 and BCJ/Delta profile at header time.
    switch (codec) {
        case 0: case 0x030101: case 0x21: case 0x03:
        case 0x03030103: case 0x03030205: case 0x03030401:
        case 0x03030501: case 0x03030701: case 0x03030805:
        case 0xa: case 0xb: return true;
        default: return false;
    }
}

bool archive_codec_delivered(ArchiveCodecBudget* budget, size_t bytes) {
    return archive_codec_record(budget, bytes, false);
}

static void* archive_codec_allocate(ArchiveCodecBudget* owner, size_t bytes, bool decoder) {
    if (!owner || bytes > SIZE_MAX - sizeof(ArchiveCodecAllocation)) return NULL;
    uint64_t* used = decoder ? &owner->decoder_bytes : owner->index_bytes;
    uint64_t limit = decoder ? owner->decoder_limit : owner->index_limit;
    size_t charge = sizeof(ArchiveCodecAllocation) + bytes;
    if (*used > limit || charge > limit - *used) { owner->limited = true; errno = ENOMEM; return NULL; }
    ArchiveCodecAllocation* allocation = mem_alloc(charge, MEM_CAT_INPUT_OTHER);
    if (!allocation) { errno = ENOMEM; return NULL; }
    allocation->next = owner->allocations;
    allocation->previous = NULL;
    if (allocation->next) allocation->next->previous = allocation;
    owner->allocations = allocation;
    allocation->owner = owner;
    allocation->bytes = charge;
    allocation->decoder = decoder;
    *used += charge;
    return allocation + 1;
}

void* lambda_archive_malloc(size_t bytes) {
    return archive_codec_allocate(active_archive_budget, bytes, false);
}

void* lambda_archive_calloc(size_t count, size_t bytes) {
    if (count && bytes > SIZE_MAX / count) return NULL;
    void* data = lambda_archive_malloc(count * bytes);
    if (data) memset(data, 0, count * bytes);
    return data;
}

void lambda_archive_free(void* pointer) {
    if (!pointer) return;
    ArchiveCodecAllocation* allocation = (ArchiveCodecAllocation*)pointer - 1;
    uint64_t* used = allocation->decoder ? &allocation->owner->decoder_bytes : allocation->owner->index_bytes;
    if (allocation->previous) allocation->previous->next = allocation->next;
    else allocation->owner->allocations = allocation->next;
    if (allocation->next) allocation->next->previous = allocation->previous;
    *used -= allocation->bytes;
    mem_free(allocation);
}

void* lambda_archive_realloc(void* pointer, size_t bytes) {
    if (!pointer) return lambda_archive_malloc(bytes);
    if (!bytes) { lambda_archive_free(pointer); return NULL; }
    ArchiveCodecAllocation* old = (ArchiveCodecAllocation*)pointer - 1;
    ArchiveCodecBudget* owner = old->owner;
    uint64_t* used = old->decoder ? &owner->decoder_bytes : owner->index_bytes;
    uint64_t limit = old->decoder ? owner->decoder_limit : owner->index_limit;
    if (bytes > SIZE_MAX - sizeof(*old)) return NULL;
    size_t charge = bytes + sizeof(*old);
    uint64_t remaining = *used - old->bytes;
    if (remaining > limit || charge > limit - remaining) { owner->limited = true; errno = ENOMEM; return NULL; }
    ArchiveCodecAllocation* resized = mem_realloc(old, charge, MEM_CAT_INPUT_OTHER);
    if (!resized) return NULL;
    if (resized->previous) resized->previous->next = resized;
    else owner->allocations = resized;
    if (resized->next) resized->next->previous = resized;
    resized->bytes = charge;
    *used = remaining + charge;
    return resized + 1;
}

char* lambda_archive_strdup(const char* text) {
    size_t bytes = strlen(text) + 1;
    char* copy = lambda_archive_malloc(bytes);
    if (copy) memcpy(copy, text, bytes);
    return copy;
}

static void* archive_codec_alloc_callback(void* context, size_t count, size_t bytes) {
    if (count && bytes > SIZE_MAX / count) return NULL;
    return archive_codec_allocate(context ? context : active_archive_budget, count * bytes, true);
}
static void archive_codec_free_callback(void* context, void* pointer) {
    (void)context;
    lambda_archive_free(pointer);
}
static const lzma_allocator archive_lzma_allocator = {
    archive_codec_alloc_callback, archive_codec_free_callback, NULL
};
static voidpf archive_zlib_alloc(voidpf context, uInt count, uInt bytes) {
    return archive_codec_alloc_callback(context, count, bytes);
}

int lambda_archive_inflate_init(z_streamp stream, int window_bits, const char* version, int size) {
    stream->zalloc = archive_zlib_alloc;
    stream->zfree = archive_codec_free_callback;
    stream->opaque = active_archive_budget;
    return inflateInit2_(stream, window_bits, version, size);
}
int lambda_archive_inflate(z_streamp stream, int flush) {
    uInt before = stream->avail_out;
    int result = inflate(stream, flush);
    if (active_archive_budget && !active_archive_budget->format_output &&
            !archive_codec_record(active_archive_budget, before - stream->avail_out, true)) return Z_MEM_ERROR;
    return result;
}

lzma_ret lambda_archive_lzma_properties(lzma_filter* filter, const lzma_allocator* allocator,
        const uint8_t* properties, size_t size) {
    (void)allocator;
    return lzma_properties_decode(filter, &archive_lzma_allocator, properties, size);
}
lzma_ret lambda_archive_lzma_raw(lzma_stream* stream, const lzma_filter* filters) {
    stream->allocator = &archive_lzma_allocator;
    return lzma_raw_decoder(stream, filters);
}
lzma_ret lambda_archive_lzma_stream(lzma_stream* stream, uint64_t limit, uint32_t flags) {
    stream->allocator = &archive_lzma_allocator;
    if (active_archive_budget && limit > active_archive_budget->decoder_limit)
        limit = active_archive_budget->decoder_limit;
    return lzma_stream_decoder(stream, limit, flags);
}
lzma_ret lambda_archive_lzma_alone(lzma_stream* stream, uint64_t limit) {
    stream->allocator = &archive_lzma_allocator;
    if (active_archive_budget && limit > active_archive_budget->decoder_limit)
        limit = active_archive_budget->decoder_limit;
    return lzma_alone_decoder(stream, limit);
}
lzma_ret lambda_archive_lzma_code(lzma_stream* stream, lzma_action action) {
    size_t before = stream->avail_out;
    lzma_ret result = lzma_code(stream, action);
    if (active_archive_budget && !active_archive_budget->format_output &&
            !archive_codec_record(active_archive_budget, before - stream->avail_out, true)) return LZMA_MEMLIMIT_ERROR;
    return result;
}

static void* archive_bzip_alloc(void* context, int count, int bytes) {
    return count < 0 || bytes < 0 ? NULL : archive_codec_alloc_callback(context, (size_t)count, (size_t)bytes);
}
int lambda_archive_bzip_init(bz_stream* stream, int verbosity, int small) {
    stream->bzalloc = archive_bzip_alloc;
    stream->bzfree = archive_codec_free_callback;
    stream->opaque = active_archive_budget;
    return BZ2_bzDecompressInit(stream, verbosity, small);
}
int lambda_archive_bzip_decode(bz_stream* stream) {
    unsigned int before = stream->avail_out;
    int result = BZ2_bzDecompress(stream);
    if (active_archive_budget && !active_archive_budget->format_output &&
            !archive_codec_record(active_archive_budget, before - stream->avail_out, true)) return BZ_MEM_ERROR;
    return result;
}

static void* archive_zstd_alloc(void* context, size_t bytes) {
    return archive_codec_alloc_callback(context, 1, bytes);
}
ZSTD_DStream* lambda_archive_zstd_create(void) {
    ZSTD_customMem allocator = {archive_zstd_alloc, archive_codec_free_callback, active_archive_budget};
    return ZSTD_createDStream_advanced(allocator);
}
size_t lambda_archive_zstd_decode(ZSTD_DStream* stream, ZSTD_outBuffer* output, ZSTD_inBuffer* input) {
    size_t before = output->pos;
    size_t result = ZSTD_decompressStream(stream, output, input);
    if (active_archive_budget && !active_archive_budget->format_output &&
            !archive_codec_record(active_archive_budget, output->pos - before, true)) return (size_t)-1;
    return result;
}


typedef struct ArchiveCodecResource {
    struct ArchiveCodecResource* next;
    ArchiveCodecBudget* owner;
    iconv_t descriptor;
} ArchiveCodecResource;

iconv_t lambda_archive_iconv_open(const char* to, const char* from) {
    ArchiveCodecBudget* owner = active_archive_budget;
    if (!owner) return (iconv_t)-1;
    ArchiveCodecResource* resource = lambda_archive_malloc(sizeof(*resource));
    if (!resource) return (iconv_t)-1;
    iconv_t descriptor = iconv_open(to, from);
    if (descriptor == (iconv_t)-1) { lambda_archive_free(resource); return descriptor; }
    resource->owner = owner;
    resource->descriptor = descriptor;
    resource->next = owner->resources;
    owner->resources = resource;
    return descriptor;
}

int lambda_archive_iconv_close(iconv_t descriptor) {
    ArchiveCodecBudget* owner = active_archive_budget;
    ArchiveCodecResource** at = owner ? (ArchiveCodecResource**)&owner->resources : NULL;
    while (at && *at && (*at)->descriptor != descriptor) at = &(*at)->next;
    if (at && *at) {
        ArchiveCodecResource* resource = *at;
        *at = resource->next;
        lambda_archive_free(resource);
    }
    return iconv_close(descriptor);
}

void archive_codec_discard(ArchiveCodecBudget* budget) {
    // fatal reader unwinds may leave partial C objects; release their complete owner list.
    for (ArchiveCodecResource* resource = budget->resources; resource; resource = resource->next)
        iconv_close(resource->descriptor);
    budget->resources = NULL;
    while (budget->allocations) {
        ArchiveCodecAllocation* allocation = budget->allocations;
        lambda_archive_free(allocation + 1);
    }
}
