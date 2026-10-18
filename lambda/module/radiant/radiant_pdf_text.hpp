#pragma once
#include "radiant_pdf_graphic.hpp"

struct PdfFontFace {
    FontHandle* handle;
    uint64_t fingerprint;
    int64_t resource;
};

// cache snapshots only for this export; no font bytes borrow a platform cache.
struct PdfTextResources {
    Rooted<Item>* fonts = nullptr;
    lam::ArrayList<PdfFontFace> faces{MEM_CAT_RENDER};
    lam::ArrayList<FontContext*> contexts{MEM_CAT_RENDER};
    void clear() {
        for (size_t i = 0; i < faces.size(); i++) font_handle_release(faces[i].handle);
        faces.clear();
        for (size_t i = 0; i < contexts.size(); i++) font_context_destroy(contexts[i]);
        contexts.clear();
    }
    ~PdfTextResources() { clear(); }
};

Item radiant_pdf_text(const PaintGlyphRun* run, UiContext* ui, PdfTextResources* resources);
