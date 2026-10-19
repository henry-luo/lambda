#pragma once

#include "../lib/file.h"
#include "../lib/shell.h"
#include "../lib/mem.h"
#include "../lib/hashmap.h"
#include "../lib/lru_cache.h"
#include "../lib/image.h"
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <dlfcn.h>
#endif

// optional in-process adapters for the same independent engines as the CLI oracles.
class PdfReferenceRasterizer {
    using PngWriter = bool (*)(const char*, const unsigned char*, int, int);
    struct Rectangle { double x, y, width, height; };
    void* (*surface_new)(int, int, int) = nullptr;
    void (*surface_free)(void*) = nullptr;
    int (*surface_status)(void*) = nullptr;
    int (*surface_save)(void*, const char*) = nullptr;
    unsigned char* (*surface_data)(void*) = nullptr;
    int (*surface_width)(void*) = nullptr;
    int (*surface_height)(void*) = nullptr;
    int (*surface_stride)(void*) = nullptr;
    void (*surface_flush)(void*) = nullptr;
    PngWriter png_writer = nullptr;
    void* (*context_new)(void*) = nullptr;
    void (*context_free)(void*) = nullptr;
    int (*context_status)(void*) = nullptr;
    void (*set_rgb)(void*, double, double, double) = nullptr;
    void (*paint)(void*) = nullptr;
    void (*scale)(void*, double, double) = nullptr;
    void (*antialias)(void*, int) = nullptr;
    void* (*font_options_new)() = nullptr;
    void (*font_options_free)(void*) = nullptr;
    void (*font_options_antialias)(void*, int) = nullptr;
    void (*set_font_options)(void*, const void*) = nullptr;
    void (*unref)(void*) = nullptr;
    void (*error_free)(void*) = nullptr;
    void (*glib_free)(void*) = nullptr;
    char* (*filename_uri)(const char*, const char*, void**) = nullptr;
    void* (*pdf_new)(const char*, const char*, void**) = nullptr;
    void* (*pdf_page)(void*, int) = nullptr;
    void (*pdf_size)(void*, double*, double*) = nullptr;
    void (*pdf_paint)(void*, void*) = nullptr;
    void* (*svg_new)(const char*, void**) = nullptr;
    int (*svg_size)(void*, double*, double*) = nullptr;
    int (*svg_paint)(void*, void*, const Rectangle*, void**) = nullptr;
    bool cairo_ready = false, pdf_ready = false, svg_ready = false;
    struct Raster {
        char* source;
        size_t source_size;
        char* png;
        size_t png_size;
    };
    LruCache* rasters = nullptr;
    size_t hits = 0;

    bool save_png(void* surface, const char* destination) {
        if (!png_writer || !surface_data || !surface_width || !surface_height || !surface_stride || !surface_flush)
            return !surface_save(surface, destination);
        int width = surface_width(surface), height = surface_height(surface), stride = surface_stride(surface);
        if (width <= 0 || height <= 0 || (size_t)width > SIZE_MAX / 4 / (size_t)height) return false;
        unsigned char* rgba = (unsigned char*)malloc((size_t)width * height * 4);
        if (!rgba) return false;
        surface_flush(surface);
        const unsigned char* data = surface_data(surface);
        // Cairo ARGB32 uses native-endian premultiplied words; preserve its straight-alpha PNG pixels.
        for (int y = 0; y < height; y++) for (int x = 0; x < width; x++) {
            uint32_t color; memcpy(&color, data + (size_t)y * stride + (size_t)x * 4, 4);
            unsigned char* pixel = rgba + ((size_t)y * width + x) * 4;
            pixel[3] = (uint8_t)(color >> 24);
            for (unsigned channel = 0; channel < 3; channel++) {
                uint8_t component = (uint8_t)(color >> (16 - channel * 8));
                pixel[channel] = pixel[3] == 255 ? component : image_unpremultiply_channel(component, pixel[3]);
            }
        }
        bool ok = png_writer(destination, rgba, width, height);
        free(rgba);
        return ok;
    }

    static void free_raster(const char*, void* value, size_t, void*) {
        Raster* raster = (Raster*)value;
        mem_free(raster->source); mem_free(raster->png); free(raster);
    }

    static void* open_library(const char* tool, const char* name) {
#ifndef _WIN32
        // system loaders handle normal installations; tool-relative lookup covers package-manager prefixes.
        void* library = dlopen(name, RTLD_LAZY | RTLD_LOCAL);
        if (library) return library;
        char* program = shell_which(tool);
        if (!program) return nullptr;
        char* slash = strrchr(program, '/');
        char path[PATH_MAX];
        if (slash) { *slash = '\0'; snprintf(path, sizeof(path), "%s/../lib/%s", program, name); }
        bool nested = slash != nullptr;
        mem_free(program);
        return nested ? dlopen(path, RTLD_LAZY | RTLD_LOCAL) : nullptr;
#else
        return nullptr;
#endif
    }

    template<class F> static bool bind(void* library, const char* name, F* target) {
#ifndef _WIN32
        *target = library ? (F)dlsym(library, name) : nullptr;
#endif
        return *target != nullptr;
    }

    struct Surface {
        PdfReferenceRasterizer* api;
        void* image = nullptr;
        void* context = nullptr;
        void clear() {
            if (context) api->context_free(context);
            if (image) api->surface_free(image);
            context = image = nullptr;
        }
        ~Surface() { clear(); }
        bool init(double width, double height) {
            if (!isfinite(width) || !isfinite(height) || width <= 0 || height <= 0 || width >= INT_MAX || height >= INT_MAX) return false;
            image = api->surface_new(0, (int)ceil(width), (int)ceil(height));
            if (!image || api->surface_status(image)) return false;
            context = api->context_new(image);
            return context && !api->context_status(context);
        }
    };

public:
    PdfReferenceRasterizer(PngWriter writer = nullptr) : png_writer(writer) {
#ifdef __APPLE__
        const char* cairo_name = "libcairo.2.dylib";
        const char* pdf_name = "libpoppler-glib.8.dylib";
        const char* svg_name = "librsvg-2.2.dylib";
#else
        const char* cairo_name = "libcairo.so.2";
        const char* pdf_name = "libpoppler-glib.so.8";
        const char* svg_name = "librsvg-2.so.2";
#endif
        void* cairo = open_library("pdftocairo", cairo_name);
        void* pdf = open_library("pdftocairo", pdf_name);
        void* svg = open_library("rsvg-convert", svg_name);
        bind(cairo, "cairo_image_surface_get_data", &surface_data);
        bind(cairo, "cairo_image_surface_get_width", &surface_width);
        bind(cairo, "cairo_image_surface_get_height", &surface_height);
        bind(cairo, "cairo_image_surface_get_stride", &surface_stride);
        bind(cairo, "cairo_surface_flush", &surface_flush);
        cairo_ready = bind(cairo, "cairo_image_surface_create", &surface_new) &&
            bind(cairo, "cairo_surface_destroy", &surface_free) && bind(cairo, "cairo_surface_status", &surface_status) &&
            bind(cairo, "cairo_surface_write_to_png", &surface_save) && bind(cairo, "cairo_create", &context_new) &&
            bind(cairo, "cairo_destroy", &context_free) && bind(cairo, "cairo_status", &context_status) &&
            bind(cairo, "cairo_set_source_rgb", &set_rgb) && bind(cairo, "cairo_paint", &paint) && bind(cairo, "cairo_scale", &scale) &&
            bind(cairo, "cairo_set_antialias", &antialias) && bind(cairo, "cairo_font_options_create", &font_options_new) &&
            bind(cairo, "cairo_font_options_destroy", &font_options_free) && bind(cairo, "cairo_font_options_set_antialias", &font_options_antialias) &&
            bind(cairo, "cairo_set_font_options", &set_font_options);
        bool glib = bind(pdf, "g_object_unref", &unref) && bind(pdf, "g_error_free", &error_free) &&
            bind(pdf, "g_free", &glib_free) && bind(pdf, "g_filename_to_uri", &filename_uri);
        pdf_ready = cairo_ready && glib && bind(pdf, "poppler_document_new_from_file", &pdf_new) &&
            bind(pdf, "poppler_document_get_page", &pdf_page) && bind(pdf, "poppler_page_get_size", &pdf_size) &&
            bind(pdf, "poppler_page_render", &pdf_paint);
        svg_ready = cairo_ready && glib && bind(svg, "rsvg_handle_new_from_file", &svg_new) &&
            bind(svg, "rsvg_handle_get_intrinsic_size_in_pixels", &svg_size) && bind(svg, "rsvg_handle_render_document", &svg_paint);
        // libraries live with the process; individual documents, surfaces and contexts do not.
    }

    bool available(bool pdf) const { return pdf ? pdf_ready : svg_ready; }
    PdfReferenceRasterizer(const PdfReferenceRasterizer&) = delete;
    PdfReferenceRasterizer& operator=(const PdfReferenceRasterizer&) = delete;
    ~PdfReferenceRasterizer() { lru_cache_free(rasters); }
    size_t cache_hits() const { return hits; }
    size_t cache_entries() const { return lru_cache_count(rasters); }
    size_t cache_bytes() const { return lru_cache_bytes(rasters); }

    bool render_export_pdf(const char* source, const char* destination, double density) {
        // exported PDFs are self-contained; compare complete bytes before reusing independent reader pixels.
        constexpr size_t budget = 32 * 1024 * 1024;
        char* bytes = nullptr; size_t size = 0;
        if (!file_read_all_limit(source, MEM_CAT_TEMP, budget, &bytes, &size))
            return render(true, source, destination, density);
        if (!rasters) {
            LruCacheConfig config = {};
            config.max_entries = 32; config.max_bytes = budget; config.on_evict = free_raster;
            rasters = lru_cache_new(&config);
        }
        char key[96];
        snprintf(key, sizeof(key), "%016llx:%a", (unsigned long long)hashmap_xxhash3(bytes, size, 0, 0), density);
        Raster* raster = (Raster*)lru_cache_get(rasters, key);
        if (raster && raster->source_size == size && !memcmp(raster->source, bytes, size)) {
            mem_free(bytes); hits++;
            return write_binary_file(destination, raster->png, raster->png_size) == 0;
        }
        bool ok = render(true, source, destination, density);
        char* png = nullptr; size_t png_size = 0;
        if (ok && file_read_all_limit(destination, MEM_CAT_TEMP, budget, &png, &png_size)) {
            size_t cost = sizeof(Raster) + size + png_size;
            raster = cost <= budget ? (Raster*)malloc(sizeof(Raster)) : nullptr;
            if (raster) {
                *raster = {bytes, size, png, png_size};
                if (lru_cache_put(rasters, key, raster, cost)) return ok;
                free(raster);
            }
        }
        mem_free(bytes); mem_free(png);
        return ok;
    }

    bool render(bool pdf, const char* source, const char* destination, double density) {
        if (!available(pdf)) return false;
        void* error = nullptr;
        void* document = nullptr;
        if (pdf) {
            char* absolute = file_realpath(source);
            char* uri = absolute ? filename_uri(absolute, nullptr, &error) : nullptr;
            mem_free(absolute);
            if (uri) { document = pdf_new(uri, nullptr, &error); glib_free(uri); }
        } else document = svg_new(source, &error);
        if (error) error_free(error);
        if (!document) return false;
        void* page = pdf ? pdf_page(document, 0) : document;
        double width = 0, height = 0;
        bool ok = page != nullptr;
        if (ok && pdf) pdf_size(page, &width, &height);
        else if (ok) ok = svg_size(page, &width, &height) != 0;
        Surface surface = {this};
        ok = ok && surface.init(width * density, height * density);
        if (ok) {
            if (pdf) {
                set_rgb(surface.context, 1, 1, 1); paint(surface.context);
                antialias(surface.context, 2); // Cairo's grayscale mode, matching pdftocairo -antialias gray.
                void* options = font_options_new();
                font_options_antialias(options, 2); set_font_options(surface.context, options); font_options_free(options);
                scale(surface.context, density, density);
                pdf_paint(page, surface.context);
            } else {
                Rectangle viewport = {0, 0, width * density, height * density};
                error = nullptr;
                ok = svg_paint(page, surface.context, &viewport, &error) != 0;
                if (error) error_free(error);
            }
            ok = ok && !context_status(surface.context) && save_png(surface.image, destination);
        }
        surface.clear();
        if (pdf && page) unref(page);
        unref(document);
        return ok;
    }
};
