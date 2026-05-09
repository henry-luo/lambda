/**
 * Lambda Unified Font Module - DirectWrite backend
 *
 * Windows-native font metrics behind the shared font backend facade.  This
 * file intentionally exposes only opaque C functions so Radiant text/layout
 * code stays platform-independent.
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <stddef.h>
extern "C" int memcmp(const void* left, const void* right, size_t size);
extern "C" void* memset(void* dst, int value, size_t size);

#include "font_internal.h"
#include "../memtrack.h"

#ifdef LAMBDA_HAS_DWRITE

#include <windows.h>
#include <dwrite.h>
#include <dwrite_2.h>
#include <math.h>

typedef struct DWriteFontData {
    const uint8_t* data;
    size_t len;
} DWriteFontData;

class DWriteMemoryFontFileStream : public IDWriteFontFileStream {
public:
    explicit DWriteMemoryFontFileStream(const DWriteFontData* font_data)
        : ref_count(1), font_data(font_data) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override {
        if (!object) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IDWriteFontFileStream)) {
            *object = static_cast<IDWriteFontFileStream*>(this);
            AddRef();
            return S_OK;
        }
        *object = NULL;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return (ULONG)InterlockedIncrement(&ref_count);
    }

    ULONG STDMETHODCALLTYPE Release() override {
        ULONG count = (ULONG)InterlockedDecrement(&ref_count);
        if (count == 0) delete this;
        return count;
    }

    HRESULT STDMETHODCALLTYPE ReadFileFragment(const void** fragment_start,
                                               UINT64 offset,
                                               UINT64 fragment_size,
                                               void** fragment_context) override {
        if (!fragment_start || !fragment_context || !font_data) return E_POINTER;
        if (offset > (UINT64)font_data->len || fragment_size > (UINT64)font_data->len - offset) {
            *fragment_start = NULL;
            *fragment_context = NULL;
            return E_FAIL;
        }
        *fragment_start = font_data->data + offset;
        *fragment_context = NULL;
        return S_OK;
    }

    void STDMETHODCALLTYPE ReleaseFileFragment(void* fragment_context) override {
        (void)fragment_context;
    }

    HRESULT STDMETHODCALLTYPE GetFileSize(UINT64* size) override {
        if (!size || !font_data) return E_POINTER;
        *size = (UINT64)font_data->len;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetLastWriteTime(UINT64* last_write_time) override {
        if (!last_write_time) return E_POINTER;
        *last_write_time = 0;
        return S_OK;
    }

private:
    volatile LONG ref_count;
    const DWriteFontData* font_data;
};

class DWriteMemoryFontFileLoader : public IDWriteFontFileLoader {
public:
    DWriteMemoryFontFileLoader() : ref_count(1) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override {
        if (!object) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IDWriteFontFileLoader)) {
            *object = static_cast<IDWriteFontFileLoader*>(this);
            AddRef();
            return S_OK;
        }
        *object = NULL;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return (ULONG)InterlockedIncrement(&ref_count);
    }

    ULONG STDMETHODCALLTYPE Release() override {
        ULONG count = (ULONG)InterlockedDecrement(&ref_count);
        if (count == 0) delete this;
        return count;
    }

    HRESULT STDMETHODCALLTYPE CreateStreamFromKey(const void* key,
                                                  UINT32 key_size,
                                                  IDWriteFontFileStream** stream) override {
        if (!key || !stream || key_size != sizeof(DWriteFontData*)) return E_INVALIDARG;
        const DWriteFontData* const* font_data = (const DWriteFontData* const*)key;
        if (!font_data || !*font_data || !(*font_data)->data || (*font_data)->len == 0) {
            return E_INVALIDARG;
        }
        *stream = new DWriteMemoryFontFileStream(*font_data);
        return *stream ? S_OK : E_OUTOFMEMORY;
    }

private:
    volatile LONG ref_count;
};

typedef struct DWriteFontRef {
    IDWriteFactory* factory;
    DWriteMemoryFontFileLoader* loader;
    IDWriteFontFile* font_file;
    IDWriteFontFace* font_face;
    DWriteFontData font_data;
    DWRITE_FONT_METRICS metrics;
    float size_px;
    bool loader_registered;
} DWriteFontRef;

static void release_dwrite_ref(DWriteFontRef* ref) {
    if (!ref) return;
    if (ref->font_face) {
        ref->font_face->Release();
        ref->font_face = NULL;
    }
    if (ref->font_file) {
        ref->font_file->Release();
        ref->font_file = NULL;
    }
    if (ref->factory && ref->loader && ref->loader_registered) {
        ref->factory->UnregisterFontFileLoader(ref->loader);
    }
    if (ref->loader) {
        ref->loader->Release();
        ref->loader = NULL;
    }
    if (ref->factory) {
        ref->factory->Release();
        ref->factory = NULL;
    }
    delete ref;
}

void* font_backend_dwrite_create(const uint8_t* data, size_t len,
                                 int face_index, float size_px) {
    if (!data || len == 0 || size_px <= 0.0f) return NULL;

    DWriteFontRef* ref = new DWriteFontRef();
    if (!ref) return NULL;
    memset(ref, 0, sizeof(DWriteFontRef));
    ref->font_data.data = data;
    ref->font_data.len = len;
    ref->size_px = size_px;

    HRESULT hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                                     __uuidof(IDWriteFactory),
                                     (IUnknown**)&ref->factory);
    if (FAILED(hr) || !ref->factory) {
        log_debug("font_dwrite: DWriteCreateFactory failed hr=0x%08lx", (unsigned long)hr);
        release_dwrite_ref(ref);
        return NULL;
    }

    ref->loader = new DWriteMemoryFontFileLoader();
    if (!ref->loader) {
        release_dwrite_ref(ref);
        return NULL;
    }

    hr = ref->factory->RegisterFontFileLoader(ref->loader);
    if (FAILED(hr)) {
        log_debug("font_dwrite: RegisterFontFileLoader failed hr=0x%08lx", (unsigned long)hr);
        release_dwrite_ref(ref);
        return NULL;
    }
    ref->loader_registered = true;

    const DWriteFontData* key = &ref->font_data;
    hr = ref->factory->CreateCustomFontFileReference(&key, sizeof(key),
                                                     ref->loader, &ref->font_file);
    if (FAILED(hr) || !ref->font_file) {
        log_debug("font_dwrite: CreateCustomFontFileReference failed hr=0x%08lx",
                  (unsigned long)hr);
        release_dwrite_ref(ref);
        return NULL;
    }

    BOOL supported = FALSE;
    DWRITE_FONT_FILE_TYPE file_type = DWRITE_FONT_FILE_TYPE_UNKNOWN;
    DWRITE_FONT_FACE_TYPE face_type = DWRITE_FONT_FACE_TYPE_UNKNOWN;
    UINT32 face_count = 0;
    hr = ref->font_file->Analyze(&supported, &file_type, &face_type, &face_count);
    (void)file_type;
    if (FAILED(hr) || !supported || face_count == 0) {
        log_debug("font_dwrite: Analyze failed or unsupported hr=0x%08lx supported=%d faces=%u",
                  (unsigned long)hr, (int)supported, (unsigned)face_count);
        release_dwrite_ref(ref);
        return NULL;
    }

    UINT32 dwrite_face_index = face_index >= 0 ? (UINT32)face_index : 0;
    if (dwrite_face_index >= face_count) dwrite_face_index = 0;

    IDWriteFontFile* files[1] = { ref->font_file };
    hr = ref->factory->CreateFontFace(face_type, 1, files, dwrite_face_index,
                                      DWRITE_FONT_SIMULATIONS_NONE,
                                      &ref->font_face);
    if (FAILED(hr) || !ref->font_face) {
        log_debug("font_dwrite: CreateFontFace failed hr=0x%08lx", (unsigned long)hr);
        release_dwrite_ref(ref);
        return NULL;
    }

    ref->font_face->GetMetrics(&ref->metrics);
    if (ref->metrics.designUnitsPerEm == 0) {
        log_debug("font_dwrite: font has zero designUnitsPerEm");
        release_dwrite_ref(ref);
        return NULL;
    }

    return ref;
}

void font_backend_dwrite_destroy(void* dwrite_ref) {
    release_dwrite_ref((DWriteFontRef*)dwrite_ref);
}

bool font_backend_dwrite_metrics(void* dwrite_ref, uint32_t codepoint,
                                 float bitmap_scale, GlyphInfo* out) {
    DWriteFontRef* ref = (DWriteFontRef*)dwrite_ref;
    if (!ref || !ref->font_face || !out || ref->metrics.designUnitsPerEm == 0) return false;

    UINT32 dwrite_codepoint = (UINT32)codepoint;
    UINT16 glyph_index = 0;
    HRESULT hr = ref->font_face->GetGlyphIndices(&dwrite_codepoint, 1, &glyph_index);
    if (FAILED(hr) || glyph_index == 0) return false;

    DWRITE_GLYPH_METRICS glyph_metrics;
    memset(&glyph_metrics, 0, sizeof(glyph_metrics));
    hr = ref->font_face->GetDesignGlyphMetrics(&glyph_index, 1, &glyph_metrics, FALSE);
    if (FAILED(hr)) return false;

    float scale = ref->size_px / (float)ref->metrics.designUnitsPerEm * bitmap_scale;
    int design_width = glyph_metrics.advanceWidth
                     - glyph_metrics.leftSideBearing
                     - glyph_metrics.rightSideBearing;
    int design_height = glyph_metrics.advanceHeight
                      - glyph_metrics.topSideBearing
                      - glyph_metrics.bottomSideBearing;
    if (design_width < 0) design_width = 0;
    if (design_height < 0) design_height = 0;

    memset(out, 0, sizeof(GlyphInfo));
    out->id = (GlyphId)glyph_index;
    out->advance_x = (float)glyph_metrics.advanceWidth * scale;
    out->advance_y = 0.0f;
    out->bearing_x = (float)glyph_metrics.leftSideBearing * scale;
    out->bearing_y = (float)(glyph_metrics.verticalOriginY - glyph_metrics.topSideBearing) * scale;
    out->width = (int)ceilf((float)design_width * scale);
    out->height = (int)ceilf((float)design_height * scale);
    out->is_color = false;
    return true;
}

GlyphBitmap* font_backend_dwrite_render(void* dwrite_ref, uint32_t codepoint,
                                        GlyphRenderMode mode, float bitmap_scale,
                                        float pixel_ratio, Arena* arena) {
    DWriteFontRef* ref = (DWriteFontRef*)dwrite_ref;
    if (!ref || !ref->factory || !ref->font_face || !arena) return NULL;
    if (pixel_ratio <= 0.0f) pixel_ratio = 1.0f;

    GlyphInfo info;
    if (!font_backend_dwrite_metrics(dwrite_ref, codepoint, bitmap_scale, &info)) {
        return NULL;
    }

    UINT16 glyph_index = (UINT16)info.id;
    FLOAT glyph_advance = info.advance_x;
    DWRITE_GLYPH_OFFSET glyph_offset;
    memset(&glyph_offset, 0, sizeof(glyph_offset));

    DWRITE_GLYPH_RUN run;
    memset(&run, 0, sizeof(run));
    run.fontFace = ref->font_face;
    run.fontEmSize = ref->size_px;
    run.glyphCount = 1;
    run.glyphIndices = &glyph_index;
    run.glyphAdvances = &glyph_advance;
    run.glyphOffsets = &glyph_offset;
    run.isSideways = FALSE;
    run.bidiLevel = 0;

    DWRITE_RENDERING_MODE render_mode =
        (mode == GLYPH_RENDER_MONO) ? DWRITE_RENDERING_MODE_ALIASED
                                    : DWRITE_RENDERING_MODE_CLEARTYPE_NATURAL_SYMMETRIC;
    DWRITE_TEXTURE_TYPE texture_type =
        (mode == GLYPH_RENDER_MONO) ? DWRITE_TEXTURE_ALIASED_1x1
                                    : DWRITE_TEXTURE_CLEARTYPE_3x1;

    IDWriteGlyphRunAnalysis* analysis = NULL;
    HRESULT hr = ref->factory->CreateGlyphRunAnalysis(&run, pixel_ratio, NULL,
                                                      render_mode,
                                                      DWRITE_MEASURING_MODE_NATURAL,
                                                      0.0f, 0.0f,
                                                      &analysis);
    if (FAILED(hr) || !analysis) {
        log_debug("font_dwrite: CreateGlyphRunAnalysis failed hr=0x%08lx cp=U+%04X",
                  (unsigned long)hr, codepoint);
        return NULL;
    }

    RECT bounds;
    memset(&bounds, 0, sizeof(bounds));
    hr = analysis->GetAlphaTextureBounds(texture_type, &bounds);
    if (FAILED(hr)) {
        log_debug("font_dwrite: GetAlphaTextureBounds failed hr=0x%08lx cp=U+%04X",
                  (unsigned long)hr, codepoint);
        analysis->Release();
        return NULL;
    }

    int width = (int)(bounds.right - bounds.left);
    int height = (int)(bounds.bottom - bounds.top);

    GlyphBitmap* bmp = (GlyphBitmap*)arena_calloc(arena, sizeof(GlyphBitmap));
    if (!bmp) {
        analysis->Release();
        return NULL;
    }

    bmp->width = width > 0 ? width : 0;
    bmp->height = height > 0 ? height : 0;
    bmp->pitch = bmp->width;
    bmp->bearing_x = (int)bounds.left;
    bmp->bearing_y = (int)(-bounds.top);
    bmp->bitmap_scale = 1.0f;
    bmp->mode = mode;
    bmp->pixel_mode = GLYPH_PIXEL_GRAY;

    if (bmp->width == 0 || bmp->height == 0) {
        analysis->Release();
        return bmp;
    }

    size_t pixel_count = (size_t)bmp->width * (size_t)bmp->height;
    bmp->buffer = (uint8_t*)arena_alloc(arena, pixel_count);
    if (!bmp->buffer) {
        analysis->Release();
        return NULL;
    }

    if (texture_type == DWRITE_TEXTURE_ALIASED_1x1) {
        hr = analysis->CreateAlphaTexture(texture_type, &bounds, bmp->buffer,
                                          (UINT32)pixel_count);
    } else {
        size_t clear_type_size = pixel_count * 3;
        uint8_t* clear_type = (uint8_t*)arena_alloc(arena, clear_type_size);
        if (!clear_type) {
            analysis->Release();
            return NULL;
        }
        hr = analysis->CreateAlphaTexture(texture_type, &bounds, clear_type,
                                          (UINT32)clear_type_size);
        if (SUCCEEDED(hr)) {
            for (size_t i = 0; i < pixel_count; i++) {
                uint32_t r = clear_type[i * 3 + 0];
                uint32_t g = clear_type[i * 3 + 1];
                uint32_t b = clear_type[i * 3 + 2];
                bmp->buffer[i] = (uint8_t)((r + g + b + 1) / 3);
            }
        }
    }

    analysis->Release();
    if (FAILED(hr)) {
        log_debug("font_dwrite: CreateAlphaTexture failed hr=0x%08lx cp=U+%04X",
                  (unsigned long)hr, codepoint);
        return NULL;
    }

    return bmp;
}

float font_backend_dwrite_glyph_advance(void* dwrite_ref, uint32_t codepoint,
                                        float bitmap_scale) {
    GlyphInfo info;
    if (!font_backend_dwrite_metrics(dwrite_ref, codepoint, bitmap_scale, &info)) {
        return -1.0f;
    }
    return info.advance_x;
}

class DWriteSingleTextSource : public IDWriteTextAnalysisSource {
public:
    DWriteSingleTextSource(const WCHAR* text, UINT32 text_len)
        : ref_count(1), text(text), text_len(text_len) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override {
        if (!object) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IDWriteTextAnalysisSource)) {
            *object = static_cast<IDWriteTextAnalysisSource*>(this);
            AddRef();
            return S_OK;
        }
        *object = NULL;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return (ULONG)InterlockedIncrement(&ref_count);
    }

    ULONG STDMETHODCALLTYPE Release() override {
        ULONG count = (ULONG)InterlockedDecrement(&ref_count);
        if (count == 0) delete this;
        return count;
    }

    HRESULT STDMETHODCALLTYPE GetTextAtPosition(UINT32 position,
                                                const WCHAR** out_text,
                                                UINT32* out_text_len) override {
        if (!out_text || !out_text_len) return E_POINTER;
        if (position >= text_len) {
            *out_text = NULL;
            *out_text_len = 0;
        } else {
            *out_text = text + position;
            *out_text_len = text_len - position;
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetTextBeforePosition(UINT32 position,
                                                    const WCHAR** out_text,
                                                    UINT32* out_text_len) override {
        if (!out_text || !out_text_len) return E_POINTER;
        if (position == 0 || position > text_len) {
            *out_text = NULL;
            *out_text_len = 0;
        } else {
            *out_text = text;
            *out_text_len = position;
        }
        return S_OK;
    }

    DWRITE_READING_DIRECTION STDMETHODCALLTYPE GetParagraphReadingDirection() override {
        return DWRITE_READING_DIRECTION_LEFT_TO_RIGHT;
    }

    HRESULT STDMETHODCALLTYPE GetLocaleName(UINT32 position,
                                            UINT32* out_text_len,
                                            const WCHAR** locale) override {
        (void)position;
        if (!out_text_len || !locale) return E_POINTER;
        *out_text_len = text_len;
        *locale = L"en-us";
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetNumberSubstitution(UINT32 position,
                                                    UINT32* out_text_len,
                                                    IDWriteNumberSubstitution** substitution) override {
        (void)position;
        if (!out_text_len || !substitution) return E_POINTER;
        *out_text_len = text_len;
        *substitution = NULL;
        return S_OK;
    }

private:
    volatile LONG ref_count;
    const WCHAR* text;
    UINT32 text_len;
};

static UINT32 encode_codepoint_utf16(uint32_t codepoint, WCHAR* out, bool emoji_presentation) {
    if (!out) return 0;
    UINT32 len = 0;
    if (codepoint <= 0xFFFF) {
        out[len++] = (WCHAR)codepoint;
    } else if (codepoint <= 0x10FFFF) {
        uint32_t scalar = codepoint - 0x10000;
        out[len++] = (WCHAR)(0xD800 + (scalar >> 10));
        out[len++] = (WCHAR)(0xDC00 + (scalar & 0x3FF));
    } else {
        return 0;
    }
    if (emoji_presentation && len < 3) {
        out[len++] = 0xFE0F;
    }
    return len;
}

static char* local_font_file_path_from_face(IDWriteFontFace* face) {
    if (!face) return NULL;

    UINT32 file_count = 0;
    HRESULT hr = face->GetFiles(&file_count, NULL);
    if (FAILED(hr) || file_count == 0) return NULL;

    IDWriteFontFile** files = (IDWriteFontFile**)mem_alloc(
        sizeof(IDWriteFontFile*) * file_count, MEM_CAT_FONT);
    if (!files) return NULL;
    memset(files, 0, sizeof(IDWriteFontFile*) * file_count);

    hr = face->GetFiles(&file_count, files);
    if (FAILED(hr) || !files[0]) {
        for (UINT32 i = 0; i < file_count; i++) {
            if (files[i]) files[i]->Release();
        }
        mem_free(files);
        return NULL;
    }

    IDWriteFontFileLoader* loader = NULL;
    IDWriteLocalFontFileLoader* local_loader = NULL;
    const void* key = NULL;
    UINT32 key_size = 0;
    UINT32 path_len = 0;
    WCHAR* wide_path = NULL;
    char* utf8_path = NULL;

    hr = files[0]->GetLoader(&loader);
    if (SUCCEEDED(hr) && loader) {
        hr = loader->QueryInterface(__uuidof(IDWriteLocalFontFileLoader),
                                    (void**)&local_loader);
    }
    if (SUCCEEDED(hr) && local_loader) {
        hr = files[0]->GetReferenceKey(&key, &key_size);
    }
    if (SUCCEEDED(hr) && key) {
        hr = local_loader->GetFilePathLengthFromKey(key, key_size, &path_len);
    }
    if (SUCCEEDED(hr) && path_len > 0) {
        wide_path = (WCHAR*)mem_alloc(sizeof(WCHAR) * (path_len + 1), MEM_CAT_FONT);
        if (wide_path) {
            hr = local_loader->GetFilePathFromKey(key, key_size, wide_path, path_len + 1);
        } else {
            hr = E_OUTOFMEMORY;
        }
    }
    if (SUCCEEDED(hr) && wide_path) {
        int utf8_len = WideCharToMultiByte(CP_UTF8, 0, wide_path, -1, NULL, 0, NULL, NULL);
        if (utf8_len > 0) {
            utf8_path = (char*)mem_alloc((size_t)utf8_len, MEM_CAT_FONT);
            if (utf8_path) {
                WideCharToMultiByte(CP_UTF8, 0, wide_path, -1,
                                    utf8_path, utf8_len, NULL, NULL);
            }
        }
    }

    if (wide_path) mem_free(wide_path);
    if (local_loader) local_loader->Release();
    if (loader) loader->Release();
    for (UINT32 i = 0; i < file_count; i++) {
        if (files[i]) files[i]->Release();
    }
    mem_free(files);
    return utf8_path;
}

char* font_backend_dwrite_find_codepoint_font(uint32_t codepoint,
                                              bool emoji_presentation,
                                              int* out_face_index) {
    if (out_face_index) *out_face_index = 0;

    WCHAR text[4];
    UINT32 text_len = encode_codepoint_utf16(codepoint, text, emoji_presentation);
    if (text_len == 0) return NULL;

    IDWriteFactory* factory = NULL;
    IDWriteFactory2* factory2 = NULL;
    IDWriteFontFallback* fallback = NULL;
    IDWriteFontCollection* collection = NULL;
    IDWriteFont* mapped_font = NULL;
    IDWriteFontFace* face = NULL;
    DWriteSingleTextSource* source = NULL;
    char* path = NULL;
    UINT32 mapped_len = 0;
    FLOAT scale = 1.0f;

    HRESULT hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                                     __uuidof(IDWriteFactory),
                                     (IUnknown**)&factory);
    if (FAILED(hr) || !factory) goto cleanup;

    hr = factory->QueryInterface(__uuidof(IDWriteFactory2), (void**)&factory2);
    if (FAILED(hr) || !factory2) goto cleanup;

    hr = factory2->GetSystemFontFallback(&fallback);
    if (FAILED(hr) || !fallback) goto cleanup;

    factory->GetSystemFontCollection(&collection, FALSE);

    source = new DWriteSingleTextSource(text, text_len);
    if (!source) goto cleanup;

    hr = fallback->MapCharacters(source, 0, text_len,
                                 collection,
                                 emoji_presentation ? L"Segoe UI Emoji" : L"Segoe UI",
                                 DWRITE_FONT_WEIGHT_NORMAL,
                                 DWRITE_FONT_STYLE_NORMAL,
                                 DWRITE_FONT_STRETCH_NORMAL,
                                 &mapped_len, &mapped_font, &scale);
    (void)scale;
    if (FAILED(hr) || !mapped_font || mapped_len == 0) goto cleanup;

    hr = mapped_font->CreateFontFace(&face);
    if (FAILED(hr) || !face) goto cleanup;

    if (out_face_index) *out_face_index = (int)face->GetIndex();
    path = local_font_file_path_from_face(face);
    if (path) {
        log_debug("font_dwrite_fallback: U+%04X -> '%s' face=%d emoji=%d",
                  codepoint, path, out_face_index ? *out_face_index : 0,
                  emoji_presentation ? 1 : 0);
    }

cleanup:
    if (!path && FAILED(hr)) {
        log_debug("font_dwrite_fallback: lookup failed for U+%04X hr=0x%08lx",
                  codepoint, (unsigned long)hr);
    }
    if (face) face->Release();
    if (mapped_font) mapped_font->Release();
    if (source) source->Release();
    if (collection) collection->Release();
    if (fallback) fallback->Release();
    if (factory2) factory2->Release();
    if (factory) factory->Release();
    return path;
}

#endif // LAMBDA_HAS_DWRITE
